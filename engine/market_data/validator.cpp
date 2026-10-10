#include "market_data/validator.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <sstream>

#include "market_data/replay.hpp"
#include "order_book/order_book.hpp"

namespace qmm {

namespace {

// Build a view of modelled book's top-levels of one side as (price,size) arrays
// and store in the specified out vectors
// Padded with LOBSTER sentinels, directly comparable to a ground-truth row
void snapshot_side(const OrderBook& book, Side side, std::size_t levels,
                   std::vector<Price>& out_price, std::vector<Size>& out_size) {
    const Price sentinel = (side == Side::Sell) ? kAskSentinelPrice : kBidSentinelPrice;
    out_price.assign(levels, sentinel);
    out_size.assign(levels, 0);

    const std::vector<LevelView> top = book.top_levels(side, levels);
    for (std::size_t i = 0; i < top.size(); i++) {
        out_price[i] = top[i].price;
        out_size[i]  = top[i].size;
    }
}

// For a given message row, output the L2
// Mismatches between the top `levels` levels in `got` and `exp`
std::string describe_mismatch(std::size_t row, const BookSnapshot& got,
                              const BookSnapshot& exp, std::size_t levels) {
    std::ostringstream os;
    os << "row " << row << ": book mismatch\n";
    os << "  lvl |      ask_p(exp/got)    ask_sz(exp/got) |      bid_p(exp/got)    bid_sz(exp/got)\n";
    for (std::size_t i = 0; i < levels; i++) {
        const bool ap = got.ask_price[i] != exp.ask_price[i];
        const bool az = got.ask_size[i]  != exp.ask_size[i];
        const bool bp = got.bid_price[i] != exp.bid_price[i];
        const bool bz = got.bid_size[i]  != exp.bid_size[i];
        if (!(ap || az || bp || bz)) continue;  // only show differing levels
        os << "  " << i << "   | "
           << exp.ask_price[i] << "/" << got.ask_price[i] << (ap ? " X " : "   ")
           << exp.ask_size[i]  << "/" << got.ask_size[i]  << (az ? " X " : "   ")
           << " | "
           << exp.bid_price[i] << "/" << got.bid_price[i] << (bp ? " X " : "   ")
           << exp.bid_size[i]  << "/" << got.bid_size[i]  << (bz ? " X " : "   ")
           << "\n";
    }
    return os.str();
}

// Best-price agreement on one side
// An empty side on both counts as agreeing
void touch_agreement(const OrderBook& book, Side side, Price true_price, Size true_size,
                     std::size_t& price_match, std::size_t& touch_match) {
    const Price sentinel = (side == Side::Buy) ? kBidSentinelPrice : kAskSentinelPrice;
    const bool true_empty = (true_price == sentinel);
    const std::optional<LevelView> ours =
        (side == Side::Buy) ? book.best_bid_level() : book.best_ask_level();

    if (!ours || true_empty) {
        if (!ours && true_empty) { price_match++; touch_match++; }
        return;
    }
    if (ours->price == true_price) {
        price_match++;
        if (ours->size == true_size) touch_match++;
    }
}

// Sum of |model - truth| over the visible band of one side, keyed by price
// First by PL in snapshot of truth, then by PL in modelled order book not found in truth
// Note: never grade a PL in order book which does not fall within the top 10 in truth
void depth_agreement(const OrderBook& book, Side side, const std::vector<LevelView>& visible,
                     std::size_t depth, Size& abs_error, Size& true_depth) {
    for (const LevelView& v : visible) {
        true_depth += v.size;
        abs_error += std::abs(book.size_at(side, v.price) - v.size);
    }

    std::optional<Price> worst;
    if (visible.size() >= depth && !visible.empty()) worst = visible.back().price;

    for (const LevelView& o : book.levels_through(side, worst)) {
        const bool listed = std::any_of(visible.begin(), visible.end(),
                                        [&](const LevelView& v) { return v.price == o.price; });
        if (!listed) abs_error += o.size;
    }
}

// Genesis and total size at the current best level of one side
void touch_genesis(const OrderBook& book, Side side, Size& genesis, Size& size) {
    const std::optional<LevelView> best =
        (side == Side::Buy) ? book.best_bid_level() : book.best_ask_level();
    if (!best) return;
    genesis += book.genesis_at(side, best->price);
    size += best->size;
}

}  // namespace

void AgreementBucket::add(const AgreementBucket& o) {
    rows                += o.rows;
    rows_zero_reconcile += o.rows_zero_reconcile;
    bid_price_match     += o.bid_price_match;
    ask_price_match     += o.ask_price_match;
    bid_touch_match     += o.bid_touch_match;
    ask_touch_match     += o.ask_touch_match;
    depth_abs_error     += o.depth_abs_error;
    depth_true          += o.depth_true;
    bid_touch_genesis   += o.bid_touch_genesis;
    bid_touch_size      += o.bid_touch_size;
    ask_touch_genesis   += o.ask_touch_genesis;
    ask_touch_size      += o.ask_touch_size;
}

AgreementBucket ValidationResult::agreement_total() const {
    AgreementBucket total;
    if (!buckets.empty()) total.start = buckets.front().start;
    for (const AgreementBucket& b : buckets) total.add(b);
    return total;
}

ValidationResult validate_reconstruction(const std::vector<Message>& messages,
                                         const std::vector<BookSnapshot>& ground,
                                         std::size_t levels,
                                         Timestamp bucket_width) {
    ValidationResult result;
    result.total_rows = ground.size();
    result.bucket_width = bucket_width > 0 ? bucket_width : kDefaultBucketWidth;

    if (messages.size() != ground.size()) {
        result.matched = false;
        std::ostringstream os;
        os << "row count mismatch: " << messages.size()
           << " messages vs " << ground.size() << " orderbook rows\n";
        result.detail = os.str();
        return result;
    }

    OrderBook book;
    bool failed = false;

    BookSnapshot got;
    got.ask_price.resize(levels);
    got.ask_size.resize(levels);
    got.bid_price.resize(levels);
    got.bid_size.resize(levels);

    // Reused per row to avoid reallocating (snapshot's real levels per side)
    std::vector<LevelView> ask_visible;
    std::vector<LevelView> bid_visible;
    ask_visible.reserve(levels);
    bid_visible.reserve(levels);

    // Bucket k covers [ (base + k) * width, (base + k + 1) * width )
    Timestamp base_index = 0; // represents first bucket number from midnight
    bool have_base = false;
    auto bucket_for = [&](Timestamp t) -> AgreementBucket& {
        const Timestamp idx = t / result.bucket_width;
        if (!have_base) { base_index = idx; have_base = true; }

        if (idx < base_index) {
            throw std::runtime_error("Timestamp precedes first bucket");
        }

        const std::size_t k = static_cast<std::size_t>(idx - base_index);

        // Bit unnecessary: in case a large gap in time exists, fill in empty buckets
        while (result.buckets.size() <= k) {
            AgreementBucket nb;
            nb.start = (base_index + static_cast<Timestamp>(result.buckets.size())) * result.bucket_width;
            result.buckets.push_back(nb);
        }

        return result.buckets[k];
    };

    replay(messages, book, [&](std::size_t i, const Message& msg) {
        if (failed) return;  // already found the first mismatch; ignore the rest

        const BookSnapshot& exp = ground[i];
        AgreementBucket& b = bucket_for(msg.time);
        b.rows++;

        // Snapshot's real levels per side
        // Skip sentinel pads, which mean "no level there"
        ask_visible.clear();
        bid_visible.clear();
        for (std::size_t l = 0; l < levels; l++) {
            if (exp.ask_price[l] != kAskSentinelPrice) {
                ask_visible.push_back(LevelView{exp.ask_price[l], exp.ask_size[l]});
            }
            if (exp.bid_price[l] != kBidSentinelPrice) {
                bid_visible.push_back(LevelView{exp.bid_price[l], exp.bid_size[l]});
            }
        }

        // Agreement BEFORE reconciliation: the model as the message left it
        touch_agreement(book, Side::Buy,  exp.bid_price[0], exp.bid_size[0],
                        b.bid_price_match, b.bid_touch_match);
        touch_agreement(book, Side::Sell, exp.ask_price[0], exp.ask_size[0],
                        b.ask_price_match, b.ask_touch_match);
        depth_agreement(book, Side::Buy,  bid_visible, levels, b.depth_abs_error, b.depth_true);
        depth_agreement(book, Side::Sell, ask_visible, levels, b.depth_abs_error, b.depth_true);

        // Reconcile every level the snapshot shows
        // Zero-reconcile row = no fix across any level (pure message-driven)
        const std::size_t fixes_before = book.reconcile_stats().fixes;
        book.reconcile_side(Side::Sell, ask_visible, levels);
        book.reconcile_side(Side::Buy,  bid_visible, levels);
        if (book.reconcile_stats().fixes == fixes_before) {
            result.rows_zero_reconcile++;
            b.rows_zero_reconcile++;
        }

        // AFTER reconciliation: how much of the touch is unknown-origin volume
        touch_genesis(book, Side::Buy,  b.bid_touch_genesis, b.bid_touch_size);
        touch_genesis(book, Side::Sell, b.ask_touch_genesis, b.ask_touch_size);
        
        // Verifies that after reconciliation, everything matches
        // Theoretically, should always match after reconciliation
        snapshot_side(book, Side::Sell, levels, got.ask_price, got.ask_size);
        snapshot_side(book, Side::Buy,  levels, got.bid_price, got.bid_size);

        const bool match =
            got.ask_price == exp.ask_price && got.ask_size == exp.ask_size &&
            got.bid_price == exp.bid_price && got.bid_size == exp.bid_size;

        if (match) {
            result.rows_checked = i + 1;
        } else {
            failed = true;
            result.first_mismatch_row = i;
            result.detail = describe_mismatch(i, got, exp, levels);
        }
    });

    result.matched = !failed;
    result.stats = book.reconcile_stats();
    return result;
}

}  // namespace qmm
