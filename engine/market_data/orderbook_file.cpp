#include "market_data/orderbook_file.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace qmm {

namespace {

std::int64_t to_i64(std::string_view sv) {
    std::int64_t value = 0;
    const char* begin = sv.data();
    const char* end = sv.data() + sv.size();
    // Allow a leading '-' (bid sentinel is negative); from_chars handles it
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        throw std::runtime_error("invalid integer in orderbook file: " + std::string(sv));
    }
    return value;
}

}  // namespace

std::vector<BookSnapshot> parse_orderbook_file(const std::string& path,
                                               std::size_t levels) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open orderbook file: " + path);
    }

    const std::size_t cols = 4 * levels;
    std::vector<BookSnapshot> rows;
    rows.reserve(1u << 20);

    std::string line;
    std::vector<std::int64_t> fields;
    fields.reserve(cols);

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        fields.clear();
        std::size_t start = 0;
        for (std::size_t i = 0; i <= line.size(); i++) {
            if (i == line.size() || line[i] == ',') {
                fields.push_back(to_i64(std::string_view(line).substr(start, i - start)));
                start = i + 1;
            }
        }
        if (fields.size() != cols) {
            throw std::runtime_error("orderbook row has wrong column count");
        }

        BookSnapshot snap;
        snap.ask_price.resize(levels);
        snap.ask_size.resize(levels);
        snap.bid_price.resize(levels);
        snap.bid_size.resize(levels);

        // Columns repeat as [AskP, AskSz, BidP, BidSz] per level
        for (std::size_t lvl = 0; lvl < levels; lvl++) {
            const std::size_t base = lvl * 4;
            snap.ask_price[lvl] = fields[base + 0];
            snap.ask_size[lvl]  = fields[base + 1];
            snap.bid_price[lvl] = fields[base + 2];
            snap.bid_size[lvl]  = fields[base + 3];
        }
        rows.push_back(std::move(snap));
    }
    return rows;
}

}  // namespace qmm
