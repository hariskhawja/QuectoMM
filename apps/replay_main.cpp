#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "core/version.hpp"
#include "market_data/lobster_parser.hpp"
#include "market_data/orderbook_file.hpp"
#include "market_data/validator.hpp"

#if defined(_WIN32)
#  include <io.h>
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace {

constexpr qmm::Timestamp kNsPerSec = 1'000'000'000LL;

// Percentage with a zero-denominator guard.
double pct(double num, double den) { return den > 0 ? 100.0 * num / den : 0.0; }

// ---- ANSI colour, auto-disabled when stdout is not a terminal ----------------
// Colour is on only when stdout is a TTY, NO_COLOR is unset, and (on Windows)
// virtual-terminal processing can be enabled. This keeps piped/redirected output
// clean (e.g. when a script captures it to a file).
bool g_color = false;

void init_color() {
    if (std::getenv("NO_COLOR") != nullptr) { g_color = false; return; }

    // The qmm helper scripts capture the exe's stdout (so it is not a TTY from
    // the exe's view) but are themselves attached to a terminal. They set
    // QMM_FORCE_COLOR=1 so colour still shows. Honour it, but still try to
    // enable VT processing on Windows so the codes render.
    const bool forced = std::getenv("QMM_FORCE_COLOR") != nullptr;

#if defined(_WIN32)
    const bool tty = _isatty(_fileno(stdout)) != 0;
    if (!tty && !forced) { g_color = false; return; }
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    g_color = tty || forced;
#else
    g_color = (isatty(fileno(stdout)) != 0) || forced;
#endif
}

const char* C_RESET() { return g_color ? "\033[0m"  : ""; }
const char* C_DIM()   { return g_color ? "\033[2m"  : ""; }
const char* C_BOLD()  { return g_color ? "\033[1m"  : ""; }
const char* C_GREEN() { return g_color ? "\033[32m" : ""; }
const char* C_RED()   { return g_color ? "\033[31m" : ""; }
const char* C_YELLOW(){ return g_color ? "\033[33m" : ""; }
const char* C_CYAN()  { return g_color ? "\033[36m" : ""; }

// A labelled section header with a blank line before it.
void section(const std::string& title) {
    std::cout << '\n' << C_BOLD() << C_CYAN() << title << C_RESET() << '\n';
}

// ns since midnight -> "HH:MM:SS"
std::string clock_time(qmm::Timestamp ns) {
    const long long s = ns / kNsPerSec;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

// One "label: value%" pair, label dimmed and padded for alignment.
void kv_pct(const std::string& label, double value, int label_w = 22) {
    std::cout << "    " << C_DIM() << std::left << std::setw(label_w) << label << C_RESET()
              << std::right << std::setw(7) << value << "%\n";
}

void print_agreement(const qmm::ValidationResult& r) {
    const qmm::AgreementBucket t = r.agreement_total();
    const double rows = static_cast<double>(t.rows);

    std::cout << std::fixed << std::setprecision(2);

    section("Agreement before reconciliation");
    std::cout << "    " << C_DIM() << "(one message applied on top of an already-reconciled book)"
              << C_RESET() << "\n";
    kv_pct("best bid price",      pct(t.bid_price_match, rows));
    kv_pct("best ask price",      pct(t.ask_price_match, rows));
    kv_pct("best bid price+size", pct(t.bid_touch_match, rows));
    kv_pct("best ask price+size", pct(t.ask_touch_match, rows));
    kv_pct("depth error",         pct(t.depth_abs_error, t.depth_true));

    section("Genesis share of touch volume (after reconciliation)");
    kv_pct("best bid", pct(t.bid_touch_genesis, t.bid_touch_size));
    kv_pct("best ask", pct(t.ask_touch_genesis, t.ask_touch_size));

    section("Agreement over time (30-min groups)");
    std::cout << "    " << C_DIM()
              << std::left
              << std::setw(10) << "start"  << std::right
              << std::setw(9)  << "rows"
              << std::setw(9)  << "pure%"
              << std::setw(10) << "bidprc%"
              << std::setw(10) << "askprc%"
              << std::setw(11) << "deptherr%"
              << std::setw(9)  << "gen_bid%"
              << std::setw(9)  << "gen_ask%"
              << C_RESET() << "\n";

    const qmm::Timestamp group_width = 30 * 60 * kNsPerSec;
    std::size_t i = 0;
    while (i < r.buckets.size()) {
        qmm::AgreementBucket g;
        g.start = r.buckets[i].start;
        const qmm::Timestamp group = g.start / group_width;
        while (i < r.buckets.size() && r.buckets[i].start / group_width == group) {
            g.add(r.buckets[i]);
            i++;
        }
        if (g.rows == 0) continue;
        const double gr = static_cast<double>(g.rows);
        std::cout << "    " << C_YELLOW() << std::left << std::setw(10)
                  << clock_time(group * group_width) << C_RESET() << std::right
                  << std::setw(9)  << g.rows
                  << std::setw(9)  << pct(g.rows_zero_reconcile, gr)
                  << std::setw(10) << pct(g.bid_price_match, gr)
                  << std::setw(10) << pct(g.ask_price_match, gr)
                  << std::setw(11) << pct(g.depth_abs_error, g.depth_true)
                  << std::setw(9)  << pct(g.bid_touch_genesis, g.bid_touch_size)
                  << std::setw(9)  << pct(g.ask_touch_genesis, g.ask_touch_size) << "\n";
    }
    std::cout << std::defaultfloat << std::setprecision(6);
}

// Raw bucket sums, one row per bucket, so ratios can be computed in analytics.
bool write_agreement_csv(const qmm::ValidationResult& r, const std::string& path) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(p);
    if (!out) return false;

    out << "bucket_start_ns,bucket_start,bucket_width_ns,rows,rows_zero_reconcile,"
           "bid_price_match,ask_price_match,bid_touch_match,ask_touch_match,"
           "depth_abs_error,depth_true,bid_touch_genesis,bid_touch_size,"
           "ask_touch_genesis,ask_touch_size\n";
    for (const qmm::AgreementBucket& b : r.buckets) {
        out << b.start << ',' << clock_time(b.start) << ',' << r.bucket_width << ','
            << b.rows << ',' << b.rows_zero_reconcile << ','
            << b.bid_price_match << ',' << b.ask_price_match << ','
            << b.bid_touch_match << ',' << b.ask_touch_match << ','
            << b.depth_abs_error << ',' << b.depth_true << ','
            << b.bid_touch_genesis << ',' << b.bid_touch_size << ','
            << b.ask_touch_genesis << ',' << b.ask_touch_size << '\n';
    }
    return static_cast<bool>(out);
}

// Validate one ticker: parse its message + orderbook files, reconstruct the
// book, and compare against ground truth. Returns true on full match.
bool validate_ticker(const std::string& message_path,
                     const std::string& orderbook_path,
                     std::size_t levels,
                     const std::string& csv_path) {
    using clock = std::chrono::steady_clock;

    const auto t0 = clock::now();
    const std::vector<qmm::Message> messages = qmm::parse_message_file(message_path);
    const std::vector<qmm::BookSnapshot> ground =
        qmm::parse_orderbook_file(orderbook_path, levels);
    const auto t1 = clock::now();

    const qmm::ValidationResult r =
        qmm::validate_reconstruction(messages, ground, levels);
    const auto t2 = clock::now();

    const auto parse_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    const auto validate_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    std::cout << "    " << C_DIM() << "messages " << messages.size()
              << "   orderbook rows " << ground.size()
              << "   (parse " << parse_ms << " ms, validate " << validate_ms << " ms)"
              << C_RESET() << "\n";

    // Verdict line.
    if (r.matched) {
        std::cout << "    " << C_GREEN() << C_BOLD() << "PASS" << C_RESET()
                  << "  all " << r.rows_checked << " rows match ground truth\n";
    } else {
        std::cout << "    " << C_RED() << C_BOLD() << "FAIL" << C_RESET()
                  << "  at row " << r.first_mismatch_row
                  << " (" << r.rows_checked << " rows ok before it)\n";
        std::cout << r.detail;
    }

    // Reconciliation / drift diagnostics.
    const qmm::ReconcileStats& s = r.stats;
    // Denominator = rows actually processed (validation stops at first mismatch).
    const std::size_t processed = r.matched ? r.rows_checked : r.first_mismatch_row + 1;

    section("Reconciliation");
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "pure message-driven rows"
              << C_RESET() << r.rows_zero_reconcile << " / " << processed
              << "  (" << std::fixed << std::setprecision(2)
              << pct(static_cast<double>(r.rows_zero_reconcile), static_cast<double>(processed))
              << "%)" << std::defaultfloat << std::setprecision(6) << "\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "reconcile calls"
              << C_RESET() << s.calls << "\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "fixes (delta != 0)"
              << C_RESET() << s.fixes
              << "  (+" << s.delta_positive << " / -" << s.delta_negative
              << ", unchanged " << s.delta_zero << ")\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "levels created"
              << C_RESET() << s.levels_created << "\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "genesis added / removed"
              << C_RESET() << s.genesis_added << " / " << s.genesis_removed << "\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "levels pruned"
              << C_RESET() << s.levels_pruned << "\n";

    section("Ghost removals (known volume removed to match the snapshot)");
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "total"
              << C_RESET() << s.ghost_events << " events, " << s.ghost_volume << " shares\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "at a visible level"
              << C_RESET() << s.ghost_events_visible << " events, "
              << s.ghost_volume_visible << " shares\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "at a re-entering level"
              << C_RESET() << (s.ghost_events - s.ghost_events_visible) << " events, "
              << (s.ghost_volume - s.ghost_volume_visible) << " shares\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "genesis shortfalls"
              << C_RESET() << s.genesis_shortfall_events << " events, "
              << s.genesis_shortfall_volume << " shares\n";
    std::cout << "    " << C_DIM() << std::left << std::setw(26) << "msgs for ghosted ids"
              << C_RESET() << s.msgs_for_ghosted_ids << "\n";

    print_agreement(r);

    if (!csv_path.empty()) {
        if (write_agreement_csv(r, csv_path)) {
            std::cout << "\n    " << C_DIM() << "wrote " << csv_path << C_RESET() << "\n";
        } else {
            std::cerr << "\n    " << C_RED() << "could not write " << csv_path << C_RESET() << "\n";
        }
    }

    return r.matched;
}

}  // namespace

// Last path component without extension, e.g. ".../AAPL_..._message_10.csv" -> the file stem.
std::string short_name(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

int main(int argc, char** argv) {
    // Collect positional args, pulling out the --no-color flag anywhere.
    std::vector<std::string> args;
    bool force_no_color = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-color" || a == "--no-colour") force_no_color = true;
        else args.push_back(a);
    }

    init_color();
    if (force_no_color) g_color = false;

    std::cout << C_BOLD() << qmm::engine_version() << C_RESET() << '\n';

    // Usage: qmm_replay [--no-color] <message.csv> <orderbook.csv> [levels] [agreement.csv]
    // Defaults to the AAPL sample at level 10 if no args are given.
    // agreement.csv (optional) receives the per-5-minute agreement metrics.
    const std::string base =
        "datasets/LOBSTER_SampleFile_AAPL_2012-06-21_10/"
        "AAPL_2012-06-21_34200000_57600000_";
    std::string message_path = base + "message_10.csv";
    std::string orderbook_path = base + "orderbook_10.csv";
    std::size_t levels = 10;

    if (args.size() >= 2) {
        message_path = args[0];
        orderbook_path = args[1];
    }
    if (args.size() >= 3) {
        levels = static_cast<std::size_t>(std::stoul(args[2]));
    }
    std::string csv_path;
    if (args.size() >= 4) {
        csv_path = args[3];
    }

    // Per-ticker banner.
    std::cout << '\n' << C_BOLD() << "=== " << short_name(message_path) << " ===" << C_RESET() << '\n';
    std::cout << "    " << C_DIM() << message_path << "\n    " << orderbook_path << C_RESET() << '\n';

    const bool ok = validate_ticker(message_path, orderbook_path, levels, csv_path);

    std::cout << '\n';
    return ok ? 0 : 1;
}
