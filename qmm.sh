#!/usr/bin/env bash
# QuectoMM helper for Linux, macOS, and Git Bash on Windows.
#
#   ./qmm.sh build          # configure (first time) + compile
#   ./qmm.sh run AAPL       # validate one ticker
#   ./qmm.sh all            # validate every ticker in datasets/
#   ./qmm.sh list           # show available tickers
#   ./qmm.sh clean          # delete the build directory
#
# If it isn't executable yet:  chmod +x qmm.sh   (or run: bash qmm.sh all)
# Written for bash 3.2+ so it also works with macOS's default bash.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
DATASETS="$ROOT/datasets"

# The engine disables colour when its stdout is not a TTY. When this script runs
# interactively, tell the engine to keep colour on (unless NO_COLOR is set).
if [ -z "${NO_COLOR:-}" ] && [ -t 1 ]; then
    export QMM_FORCE_COLOR=1
    C_GREEN=$'\033[32m'; C_RED=$'\033[31m'; C_CYAN=$'\033[1;36m'; C_RESET=$'\033[0m'
else
    C_GREEN=""; C_RED=""; C_CYAN=""; C_RESET=""
fi

die() { echo "error: $*" >&2; exit 1; }

configure() {
    [ -f "$BUILD_DIR/CMakeCache.txt" ] && return 0
    local args=(-S "$ROOT" -B "$BUILD_DIR")
    # Ninja is optional; use it when installed, otherwise CMake picks a default.
    if command -v ninja >/dev/null 2>&1; then args+=(-G Ninja); fi
    echo "Configuring: cmake ${args[*]}"
    cmake "${args[@]}"
}

build() {
    command -v cmake >/dev/null 2>&1 || die "cmake not found on PATH. Install CMake 3.20+."
    configure
    cmake --build "$BUILD_DIR" --config Release
}

find_exe() {
    local name
    for name in qmm_replay qmm_replay.exe; do
        if [ -x "$BUILD_DIR/bin/$name" ]; then echo "$BUILD_DIR/bin/$name"; return 0; fi
    done
    return 1
}

exe_or_build() {
    local exe
    if ! exe="$(find_exe)"; then
        build >&2
        exe="$(find_exe)" || die "qmm_replay executable not found after build."
    fi
    echo "$exe"
}

# Folders are named LOBSTER_SampleFile_<TICKER>_<DATE>_<LEVELS>.
ticker_dirs() {
    [ -d "$DATASETS" ] || return 0
    local d
    for d in "$DATASETS"/LOBSTER_SampleFile_*/; do
        [ -d "$d" ] && echo "${d%/}"
    done
}

ticker_name() { basename "$1" | cut -d_ -f3; }

find_ticker_dir() {
    local want d
    want="$(echo "$1" | tr '[:lower:]' '[:upper:]')"
    while IFS= read -r d; do
        [ "$(ticker_name "$d")" = "$want" ] && { echo "$d"; return 0; }
    done < <(ticker_dirs)
    return 1
}

run_ticker() {
    local dir="$1" exe="$2" msg ob base levels
    msg="$(ls "$dir"/*_message_*.csv 2>/dev/null | head -n 1)"
    ob="$(ls "$dir"/*_orderbook_*.csv 2>/dev/null | head -n 1)"
    [ -n "$msg" ] && [ -n "$ob" ] || die "missing message/orderbook CSV in $dir"
    # Depth comes from the file name suffix, e.g. ..._message_10.csv -> 10.
    base="$(basename "$msg" .csv)"
    levels="${base##*_}"
    echo "===== $(ticker_name "$dir") (levels=$levels) ====="
    # Per-5-minute agreement metrics (see metrics/engineering/README.md).
    "$exe" "$msg" "$ob" "$levels" "$ROOT/metrics/engineering/$(ticker_name "$dir")_agreement.csv"
}

usage() {
    cat <<'EOF'
QuectoMM helper

Usage: ./qmm.sh <command> [ticker]

Commands:
  build          Configure (first time) and compile
  run <TICKER>   Validate one ticker, e.g. ./qmm.sh run AAPL
  all            Validate every ticker found in datasets/
  list           List available tickers
  clean          Delete the build directory
  help           Show this message
EOF
}

cmd="${1:-help}"
case "$cmd" in
    build) build ;;
    run)
        [ $# -ge 2 ] || die "usage: ./qmm.sh run <TICKER>   (see ./qmm.sh list)"
        dir="$(find_ticker_dir "$2")" || die "no dataset for '$2'. Available: $(ticker_dirs | while read -r d; do ticker_name "$d"; done | tr '\n' ' ')"
        exe="$(exe_or_build)"
        run_ticker "$dir" "$exe"
        ;;
    all)
        exe="$(exe_or_build)"
        summary=""
        failed=0
        found=0
        while IFS= read -r d; do
            found=1
            if run_ticker "$d" "$exe"; then
                summary="${summary}$(printf '%-8s' "$(ticker_name "$d")")${C_GREEN}PASS${C_RESET}"$'\n'
            else
                summary="${summary}$(printf '%-8s' "$(ticker_name "$d")")${C_RED}FAIL${C_RESET}"$'\n'
                failed=1
            fi
        done < <(ticker_dirs)
        [ "$found" -eq 1 ] || die "no datasets found in $DATASETS"
        echo
        echo "${C_CYAN}===== Summary =====${C_RESET}"
        printf '%-8s%s\n' "Ticker" "Result"
        printf '%-8s%s\n' "------" "------"
        printf "%s" "$summary"
        exit "$failed"
        ;;
    list) ticker_dirs | while read -r d; do ticker_name "$d"; done ;;
    clean)
        if [ -d "$BUILD_DIR" ]; then rm -rf "$BUILD_DIR"; echo "Removed $BUILD_DIR"; else echo "Nothing to clean."; fi
        ;;
    help|-h|--help) usage ;;
    *) usage; exit 1 ;;
esac
