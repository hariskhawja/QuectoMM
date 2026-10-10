#include "market_data/lobster_parser.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <stdexcept>

namespace qmm {

namespace {

// Split a line into exactly N fields on ','
// Throws if the count is wrong
template <std::size_t N>
std::array<std::string_view, N> split_fields(std::string_view line) {
    std::array<std::string_view, N> out{};
    std::size_t field = 0;
    std::size_t start = 0;

    for (std::size_t i = 0; i <= line.size(); i++) {
        if (i == line.size() || line[i] == ',') {
            if (field >= N) {
                throw std::runtime_error("too many fields in message line");
            }
            out[field++] = line.substr(start, i - start);  // 2nd param is number of chars
            start = i + 1;
        }
    }

    if (field != N) {
        throw std::runtime_error("wrong field count in message line");
    }

    return out;
}

// Parse a non-negative integer from a string_view using std::from_chars
std::int64_t to_i64(std::string_view sv) {
    std::int64_t value = 0;
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);

    if (ec != std::errc{} || ptr != end) {
        throw std::runtime_error("invalid integer field: " + std::string(sv));
    }

    return value;
}

}  // namespace


// Convert "<seconds>.<fraction>" -> integer nanoseconds since midnight
Timestamp parse_timestamp(std::string_view field) {
    const std::size_t dot = field.find('.');

    // No fractional part: whole seconds only
    if (dot == std::string_view::npos) {
        return to_i64(field) * 1'000'000'000;
    }

    const std::int64_t whole = to_i64(field.substr(0, dot)); // right exclusive

    std::string frac(field.substr(dot + 1));
    frac.resize(9, '0');

    return whole * 1'000'000'000 + to_i64(frac);
}

// Parse one 6-field message line into a Message
Message parse_message_line(std::string_view line) {
    auto f = split_fields<6>(line);

    return Message{
        parse_timestamp(f[0]),
        static_cast<EventType>(to_i64(f[1])),  // LOBSTER codes map directly
        to_i64(f[2]),
        to_i64(f[3]),
        to_i64(f[4]),
        (to_i64(f[5]) == 1) ? Side::Buy : Side::Sell,
    };
}

// ----------------------------------------------------------------------------
// File Plumbing
// ----------------------------------------------------------------------------

std::vector<Message> parse_message_file(const std::string& path) {
    std::ifstream in(path);

    if (!in) {
        throw std::runtime_error("cannot open message file: " + path);
    }

    std::vector<Message> messages;
    messages.reserve(1u << 20);  // ~1M rows; grows if needed

    std::string line;
    while (std::getline(in, line)) {
        // Tolerate a trailing '\r' on Windows-written files and blank lines
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        messages.push_back(parse_message_line(line));
    }
    return messages;
}

}  // namespace qmm
