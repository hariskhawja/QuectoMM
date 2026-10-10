#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace qmm {

// Parsing of LOBSTER "message" CSV files into typed Message events
//
// A message row has 6 comma-separated columns:
//   Time, Type, OrderID, Size, Price, Direction
// e.g.  34200.004241176,1,16113575,18,5853300,1
//
// Time  : seconds after midnight with fractional (ms..ns) precision
// Type  : 1 new, 2 partial-cancel, 3 delete, 4 visible-exec, 5 hidden-exec, 7 halt
// Price : dollars * 10000 (integer units) - kept as-is in Message::price
// Dir   : 1 = buy limit, -1 = sell limit (sentinel -1 for halts; ignored by the book)

// Convert a LOBSTER time field ("<seconds>.<fraction>") to integer nanoseconds
// since midnight, parsing the whole and fractional parts as integers to avoid
// floating-point rounding error
[[nodiscard]] Timestamp parse_timestamp(std::string_view field);

// Parse a single message CSV line into a Message
// Expects exactly 6 fields
[[nodiscard]] Message parse_message_line(std::string_view line);

// Read and parse an entire LOBSTER message file into a vector of Messages, in file order (which is time order)
// Throws std::runtime_error if the file cannot be opened
[[nodiscard]] std::vector<Message> parse_message_file(const std::string& path);

}  // namespace qmm
