#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace qmm {

// LOBSTER sentinel prices for unoccupied levels (see sample ReadMe):
// asks pad with +9999999999, bids pad with -9999999999, sizes with 0
inline constexpr Price kAskSentinelPrice = 9999999999LL;
inline constexpr Price kBidSentinelPrice = -9999999999LL;

// Real order-book snapshot row
struct BookSnapshot {
    // Index 0 = best level
    // Sentinels/0 preserved exactly as in the file
    std::vector<Price> ask_price;
    std::vector<Size>  ask_size;
    std::vector<Price> bid_price;
    std::vector<Size>  bid_size;
};


// Parse orderbook file
// Throws std::runtime_error on open failure or malformed rows
[[nodiscard]] std::vector<BookSnapshot> parse_orderbook_file(const std::string& path,
                                                             std::size_t levels);

}  // namespace qmm
