#include "order_book/order_book.hpp"

#include <algorithm>

namespace qmm {

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

std::map<Price, PriceLevel>& OrderBook::book_for(Side side) {
    return side == Side::Buy ? bids_ : asks_;
}

const std::map<Price, PriceLevel>& OrderBook::book_for(Side side) const {
    return side == Side::Buy ? bids_ : asks_;
}

// ----------------------------------------------------------------------------
// Dispatch
// ----------------------------------------------------------------------------

void OrderBook::apply(const Message& m) {
    switch (m.type) {
        case EventType::NewLimitOrder:      add(m);             break;
        case EventType::PartialCancel:      partial_cancel(m);  break;
        case EventType::Delete:             delete_order(m);    break;
        case EventType::VisibleExecution:   execute(m);         break;
        case EventType::HiddenExecution:                        break;
        case EventType::TradingHalt:                            break;
    }
}

// ----------------------------------------------------------------------------
// Per-message-type handlers
// ----------------------------------------------------------------------------

// Type 1: New Limit Order
void OrderBook::add(const Message& m) {
    auto& side = book_for(m.side);
    auto& level = side[m.price];

    auto it = level.orders.insert(
        level.orders.end(),
        Order{m.id, m.side, m.price, m.size, m.time});

    level.total_size += m.size;
    index_[m.id] = OrderLocation{m.side, m.price, it};
}

// Remove qty of unknown-origin volume from genesis at (side, price)
// Used when a cancel/delete/execute references an id we never saw a type-1 for
// The volume belongs to the aggregate genesis block, not a tracked order
// Clamps genesis at 0, drops the level if it becomes empty
void OrderBook::reduce_genesis(Side side, Price price, Size qty) {
    auto& book = book_for(side);
    auto it = book.find(price);
    if (it == book.end()) {        // nothing known here; snapshot reconcile will fix if needed
        rstats_.genesis_shortfall_events++;
        rstats_.genesis_shortfall_volume += qty;
        return;
    }
    PriceLevel& level = it->second;

    const Size take = std::min(qty, level.genesis);
    level.genesis -= take;
    level.total_size -= take;
    if (take < qty) {
        rstats_.genesis_shortfall_events++;
        rstats_.genesis_shortfall_volume += qty - take;
    }

    if (level.orders.empty() && level.genesis == 0) book.erase(it);
}

// Type 2: Partial Cancel
void OrderBook::partial_cancel(const Message& m) {
    auto found = index_.find(m.id);
    if (found == index_.end()) {           // unknown id -> genesis
        handle_unknown_id(m);
        return;
    }
    OrderLocation& loc = found->second;

    auto& book = book_for(loc.side);
    auto& level = book[loc.price];

    // Clamp: a ghost reduction may already have shrunk this order below m.size
    const Size take = std::min(m.size, loc.it->size);
    loc.it->size -= take;
    level.total_size -= take;

    if (loc.it->size == 0) {
        level.orders.erase(loc.it);
        if (level.orders.empty() && level.genesis == 0) book.erase(loc.price);
        index_.erase(found);
    }
}

void OrderBook::handle_unknown_id(const Message& m) {
    if (ghosted_ids_.count(m.id) != 0) rstats_.msgs_for_ghosted_ids++;
    reduce_genesis(m.side, m.price, m.size);
}

// Type 3: Delete Order
void OrderBook::delete_order(const Message& m) {
    auto found = index_.find(m.id);
    if (found == index_.end()) {           // unknown id -> genesis (remove its full size)
        handle_unknown_id(m);
        return;
    }
    OrderLocation& loc = found->second;

    auto& book = book_for(loc.side);
    auto& level = book[loc.price];

    level.total_size -= loc.it->size;
    level.orders.erase(loc.it);

    if (level.orders.empty() && level.genesis == 0) book.erase(loc.price);
    index_.erase(found);
}

// Type 4: Visible Execution
void OrderBook::execute(const Message& m) {
    auto found = index_.find(m.id);
    if (found == index_.end()) {           // unknown id -> genesis
        handle_unknown_id(m);
        return;
    }
    OrderLocation& loc = found->second;

    auto& book = book_for(loc.side);
    auto& level = book[loc.price];

    // Clamp: a ghost reduction may already have shrunk this order below m.size
    const Size take = std::min(m.size, loc.it->size);
    loc.it->size -= take;
    level.total_size -= take;

    if (loc.it->size == 0) {
        level.orders.erase(loc.it);
        if (level.orders.empty() && level.genesis == 0) book.erase(loc.price);
        index_.erase(found);
    }
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------

// Highest bid price, or nullopt if there are no bids
std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;
}

// Lowest ask price, or nullopt if there are no asks
std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

// Best bid as (price, size), or nullopt
std::optional<LevelView> OrderBook::best_bid_level() const {
    if (bids_.empty()) return std::nullopt;
    const auto& [price, level] = *bids_.rbegin();
    return LevelView{price, level.total_size};
}

// Best ask as (price, size), or nullopt
std::optional<LevelView> OrderBook::best_ask_level() const {
    if (asks_.empty()) return std::nullopt;
    const auto& [price, level] = *asks_.begin();
    return LevelView{price, level.total_size};
}

// Aggregated resting size at a specific price on a side; 0 if no such level
Size OrderBook::size_at(Side side, Price price) const {
    const auto& book = book_for(side);
    auto it = book.find(price);
    if (it == book.end()) return 0;
    return it->second.total_size;
}

// Up to n levels, best-first
std::vector<LevelView> OrderBook::top_levels(Side side, std::size_t n) const {
    const auto& book = book_for(side);
    std::vector<LevelView> levels;
    levels.reserve(std::min(n, book.size()));

    if (side == Side::Buy) {
        for (auto it = book.rbegin(); it != book.rend() && levels.size() < n; it++) {
            levels.push_back(LevelView{it->first, it->second.total_size});
        }
    } else {
        for (auto it = book.begin(); it != book.end() && levels.size() < n; it++) {
            levels.push_back(LevelView{it->first, it->second.total_size});
        }
    }

    return levels;
}

std::vector<LevelView> OrderBook::levels_through(Side side, std::optional<Price> worst) const {
    const auto& book = book_for(side);
    std::vector<LevelView> out;
    if (side == Side::Buy) {
        for (auto it = book.rbegin(); it != book.rend(); ++it) {
            if (worst && it->first < *worst) break;   // bids: worse = lower
            out.push_back(LevelView{it->first, it->second.total_size});
        }
    } else {
        for (auto it = book.begin(); it != book.end(); ++it) {
            if (worst && it->first > *worst) break;   // asks: worse = higher
            out.push_back(LevelView{it->first, it->second.total_size});
        }
    }
    return out;
}

Size OrderBook::genesis_at(Side side, Price price) const {
    const auto& book = book_for(side);
    auto it = book.find(price);
    return it == book.end() ? 0 : it->second.genesis;
}

// ----------------------------------------------------------------------------
// Diagnostics
// ----------------------------------------------------------------------------

std::size_t OrderBook::level_count(Side side) const {
    return book_for(side).size();
}

std::size_t OrderBook::order_count() const noexcept {
    return index_.size();
}

// ----------------------------------------------------------------------------
// Reconciliation
// ----------------------------------------------------------------------------

// Force this level's aggregate size to equal the L2 snapshot ground truth by
// absorbing the difference into the genesis block
void OrderBook::reconcile_level(Side side, Price price, Size snapshot_size) {
    rstats_.calls++;

    const Size current = size_at(side, price);  // genesis + known orders (0 if no level)
    const Size delta = snapshot_size - current;

    if (delta == 0) {           // model already matches ground truth here
        rstats_.delta_zero++;
        return;
    }

    rstats_.fixes++;
    if (delta > 0) rstats_.delta_positive++;
    else           rstats_.delta_negative++;

    auto& book = book_for(side);
    const bool existed = book.find(price) != book.end();
    PriceLevel& level = book[price];  // creates an empty level if absent
    if (!existed) rstats_.levels_created++;

    if (delta > 0) {
        // Model is missing volume: grow the genesis block
        level.genesis    += delta;
        level.total_size += delta;
        rstats_.genesis_added += delta;
    } else {
        // Model is stale-high: remove -delta, genesis first
        Size need = -delta;

        const Size take = std::min(need, level.genesis);
        level.genesis    -= take;
        level.total_size -= take;
        rstats_.genesis_removed += take;
        need -= take;

        // Ghost case: more to remove than genesis held -> dip into known orders,
        // front-first (oldest = highest priority), cleaning up the id index
        if (need > 0) {
            const Size residual = need;
            auto it = level.orders.begin();
            while (need > 0 && it != level.orders.end()) {
                const Size hit = std::min(need, it->size);
                it->size         -= hit;
                level.total_size -= hit;
                need             -= hit;
                if (it->size == 0) {
                    ghosted_ids_.insert(it->id);
                    index_.erase(it->id);                 // drop stale index entry first
                    it = level.orders.erase(it);          // erase returns next iterator
                } else {
                    it++;
                }
            }
            rstats_.ghost_events++;
            rstats_.ghost_volume += residual;
            if (was_visible(side, price)) {
                rstats_.ghost_events_visible++;
                rstats_.ghost_volume_visible += residual;
            }
        }
    }

    // Drop a level that ended up completely empty so it doesn't linger
    if (level.orders.empty() && level.genesis == 0) {
        book.erase(price);
    }
}

void OrderBook::reconcile_side(Side side, const std::vector<LevelView>& visible,
                               std::size_t depth) {
    // 1. Sync every level the snapshot shows
    for (const LevelView& lv : visible) {
        reconcile_level(side, lv.price, lv.size);
    }

    // 2. Prune stale in-band levels the snapshot omits
    const bool full = visible.size() >= depth;   // full -> deeper levels may exist unseen
    const Price worst = visible.empty() ? 0 : visible.back().price;

    auto in_band = [&](Price p) {
        if (!full) return true;                          // snapshot is the whole side
        return side == Side::Sell ? p <= worst : p >= worst;
    };
    auto listed = [&](Price p) {
        for (const LevelView& lv : visible) {            // <= 10 entries: linear is fine
            if (lv.price == p) return true;
        }
        return false;
    };

    // Collect first: reconcile_level() may erase map entries, so don't mutate
    // the map while iterating it
    std::vector<Price> stale;
    for (const auto& [price, level] : book_for(side)) {
        if (in_band(price) && !listed(price)) stale.push_back(price);
    }
    for (Price p : stale) {
        reconcile_level(side, p, 0);   // drains genesis, then known orders (ghost path)
        rstats_.levels_pruned++;
    }

    // 3. Remember this snapshot's visible prices for the next call
    // This is to check if a PL was previously visible
    auto& prev = (side == Side::Buy) ? prev_visible_bids_ : prev_visible_asks_;
    prev.clear();
    for (const LevelView& lv : visible) prev.push_back(lv.price);
}

bool OrderBook::was_visible(Side side, Price price) const {
    const auto& prev = (side == Side::Buy) ? prev_visible_bids_ : prev_visible_asks_;
    return std::find(prev.begin(), prev.end(), price) != prev.end();
}

}  // namespace qmm
