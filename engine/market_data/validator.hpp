#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "market_data/orderbook_file.hpp"
#include "order_book/order_book.hpp"

namespace qmm {

// Default time-bucket width for agreement metrics: 5 minutes, in nanoseconds
inline constexpr Timestamp kDefaultBucketWidth = 300'000'000'000LL;

// Agreement between the model and LOBSTER ground truth over one time bucket
// Every field is a SUM, so buckets can be merged by adding them and any ratio
// can be computed afterwards (e.g. bid_price_match / rows)
//
// "Before reconciliation" fields compare the model right after a message is
// applied, before it is synced to that row's snapshot
// Because every earlier row was reconciled, they measure the error introduced by ONE message on top
// of a corrected book, not drift accumulated over time
// "After reconciliation" fields describe the book a quoting strategy would see
struct AgreementBucket {
    Timestamp start = 0;                  // bucket start, ns since midnight
    std::size_t rows = 0;
    std::size_t rows_zero_reconcile = 0;  // rows needing no reconciliation at all

    // Before reconciliation
    std::size_t bid_price_match = 0;      // best bid price agreed
    std::size_t ask_price_match = 0;      // best ask price agreed
    std::size_t bid_touch_match = 0;      // best bid price AND size agreed
    std::size_t ask_touch_match = 0;      // best ask price AND size agreed
    Size depth_abs_error = 0;             // sum |model - truth| over the visible band, both sides
    Size depth_true = 0;                  // true visible depth (shares), both sides

    // After reconciliation
    Size bid_touch_genesis = 0;           // genesis shares at the best bid
    Size bid_touch_size = 0;              // total shares at the best bid
    Size ask_touch_genesis = 0;
    Size ask_touch_size = 0;

    void add(const AgreementBucket& o);
};

// Outcome of validating a reconstructed book against LOBSTER ground truth
struct ValidationResult {
    bool matched = false;  // true iff the reconstruction matched ground truth on every row
    std::size_t rows_checked = 0;
    std::size_t total_rows = 0;

    // First divergence (valid only when matched == false)
    std::size_t first_mismatch_row = 0;
    std::string detail;  // human-readable description of the first mismatch

    // Reconciliation forces the book to the snapshot each row
    // So an exact match is partly by construction
    // These quantify reconstruction on its own vs reconciliation carrying it
    std::size_t rows_zero_reconcile = 0;  // rows that needed NO reconciliation (pure message-driven)
    ReconcileStats stats;                 // accumulated reconciliation metrics over the run

    // Agreement metrics, one entry per time bucket (consecutive, oldest first)
    Timestamp bucket_width = kDefaultBucketWidth;
    std::vector<AgreementBucket> buckets;

    // All buckets merged into one
    [[nodiscard]] AgreementBucket agreement_total() const;
};

// Replay messages through a fresh order book
// After each message i, compare top-levels against ground[i]
// Stops at the first mismatch (best signal for debugging reconstruction)
// messages and ground must be row-aligned and the same length
// Verifies everything matches after the reconciliation phase
[[nodiscard]] ValidationResult validate_reconstruction(
    const std::vector<Message>& messages,
    const std::vector<BookSnapshot>& ground,
    std::size_t levels,
    Timestamp bucket_width = kDefaultBucketWidth);

}  // namespace qmm
