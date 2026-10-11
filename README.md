# QuectoMM

A discrete-event market-making research engine. QuectoMM replays historical
LOBSTER order-level events, reconstructs the limit order book to full
(order-by-order) depth, and uses it to study how microstructure signals affect
inventory-aware market making under event-driven execution, queue-position, and
transaction-cost constraints.

---

## 1. Engineering Overview

### What It Does

The engine reconstructs an exchange's **Level-3 (order-by-order) limit order
book** by replaying LOBSTER message events, then validates that reconstruction
against LOBSTER's own ground-truth order-book snapshots, event for event.

```
 LOBSTER message file                           LOBSTER orderbook file
 (every add/cancel/exec)                         (true L2 depth per row)
        │                                                  │
        ▼                                                  │
 ┌──────────────┐    ┌────────────────────────────┐        │
 │   parser     │──▶ │        OrderBook (L3)      │        │
 │ CSV → events │    │  bids/asks  : price levels │        │
 └──────────────┘    │  per level  : FIFO queue   │        │
        │            │  id index   : O(1) lookup  │        │
        ▼            │  genesis    : unknown vol  │        │
 ┌──────────────┐    └────────────────────────────┘        │
 │    replay    │              │                           │
 │  event loop  │──────────────┘                           ▼
 └──────────────┘           per row ───▶ ┌────────────────────────────┐
                                         │        validator           │
                                         │ reconcile to snapshot,     │
                                         │ then compare & score       │
                                         └────────────────────────────┘
                                                        │
                                                        ▼
                                        PASS/FAIL + agreement metrics (CSV)
```

### Reconstruction Methodology

LOBSTER level-10 files only contain events inside the top 10 price levels, so
orders resting deeper are invisible until their price moves into view. Replaying
messages from an empty book therefore cannot reproduce the real book on its own.
QuectoMM handles this with two ideas:

- **Genesis volume.** Liquidity whose individual orders were never observed
  (present before the window, or arriving while a level was out of view) is
  tracked as an aggregate *Genesis* block at the front of each price level.
  Orders seen during the session are tracked individually by ID with true
  FIFO priority behind it.
- **Snapshot reconciliation.** After each event, every visible price level is
  reconciled to LOBSTER's ground-truth size: the difference is absorbed into
  Genesis (or, when a tracked order died out of view, removed from known
  volume). Stale levels the snapshot no longer shows are pruned.

This keeps the Level-2 (aggregate depth) view **exactly** equal to ground truth
while preserving as much real order-by-order structure as the data allows. The
unknown fraction is measured as it
is the quantity a queue-position model must reason about.

### Headline Validation Metrics

The meaningful measure of reconstruction quality is **how close the book is
*before* each row is reconciled to the snapshot** — i.e. how much the message
replay plus Genesis model gets right on its own. (Post-reconciliation the Level-2
book equals ground truth *by construction*, since reconciliation forces each
visible level to the snapshot size; that is by design.)

All metrics below are measured **before reconciliation**, after each event is
applied on top of an already-reconciled previous row — so they capture the error
introduced by one event, not drift accumulated over the day. Across all five
NASDAQ sample tickers (AAPL, AMZN, GOOG, INTC, MSFT), 2012-06-21,
**~2.1 million events total**:

| Ticker | Events | Rows correct from messages alone | Best bid / ask price agreement | Depth error | Genesis share at touch (bid/ask) |
|--------|--------|----------------------------------|--------------------------------|-------------|----------------------------------|
| AAPL   | 400,391 | 95.75% | 99.94% / 99.96% | 0.18% | 15.2% / 7.0% |
| AMZN   | 269,748 | 95.27% | 99.97% / 99.98% | 0.25% | 6.8% / 2.4% |
| GOOG   | 147,916 | 95.44% | 99.95% / 99.99% | 0.29% | 7.8% / 2.1% |
| INTC   | 624,040 | 99.61% | 100.0% / 100.0% | 0.00% | 1.6% / 0.0% |
| MSFT   | 668,765 | 99.46% | 100.0% / 100.0% | 0.00% | 0.5% / 0.0% |

- **Rows correct from messages alone** — share of events after which the book
  already matched the snapshot *before* reconciliation touched it. This is the
  headline number: pure message-driven correctness.
- **Best price agreement** — how often the reconstructed best bid/ask matches
  ground truth after a single event, before that row's reconciliation.
- **Depth error** — total share mismatch across the visible book, before
  reconciliation, as a fraction of true visible depth. (The near-0% on INTC/MSFT
  reflects their very high pre-reconciliation accuracy.)
- **Genesis share at touch** — fraction of volume at the best price whose
  individual orders are unknown; the part a queue model must make assumptions
  about. Measured after reconciliation (it is a property of the matched book),
  and it varies strongly intraday (AAPL bid: 1–33% across the day).

Full column definitions are in
[`metrics/engineering/README.md`](metrics/engineering/README.md); the analysis
notebook is `metrics/engineering/model_analysis.ipynb`.

### Layout

```
engine/
  core/          domain types (integer-tick prices, events)
  order_book/    L3 order book + Genesis + reconciliation
  market_data/   LOBSTER parser, replay loop, validator
apps/            qmm_replay — the validation/metrics driver
metrics/engineering/   agreement CSVs + analysis notebook
```

### Limitations
- **Post-reconciliation L2 match is by design, not a result.** Reconciliation
  forces each visible level to the snapshot, so exact Level-2 agreement is
  guaranteed. The reconstruction's quality lives in the *pre*-reconciliation
  numbers above (rows correct from messages alone, best-price agreement).
- **Narrow data.** A single trading day (2012-06-21), five large-cap NASDAQ
  tickers, top-10 depth only. Results are not claimed to generalize across time,
  regimes, instruments, or deeper books.
- **Unknown liquidity is aggregated.** Genesis volume is modelled as one block
  per level, not as individual orders, so order-by-order structure is only known
  for liquidity observed during the session. The unknown fraction is measured
  (Genesis share) rather than assumed away.
- **Ghost removal uses an oldest-first heuristic.** When a tracked order dies out
  of view, reconciliation removes volume front-first; this is a documented
  assumption, measured but not independently verifiable from the data.

---

## 2. Build and Run

### Requirements

- CMake 3.20 or newer
- C++20 compiler: GCC 10+, Clang 12+, or MSVC 2019 16.10+ (Visual Studio)
- Optional: [Ninja](https://ninja-build.org/). The scripts use it when it is installed and fall back to CMake's default generator otherwise.

Tested on Windows with MinGW-w64 GCC 16.2 and Ninja, through both PowerShell and Git Bash. Other platforms and compilers have not been tested yet.

### Commands

Run these from the repository root. Each `run` or `all` builds automatically if needed.

| Task | Linux / macOS / Git Bash | Windows PowerShell |
|---|---|---|
| Build | `./qmm.sh build` | `.\qmm.ps1 build` |
| Validate one ticker | `./qmm.sh run AAPL` | `.\qmm.ps1 run AAPL` |
| Validate all tickers | `./qmm.sh all` | `.\qmm.ps1 all` |
| List tickers | `./qmm.sh list` | `.\qmm.ps1 list` |
| Delete build output | `./qmm.sh clean` | `.\qmm.ps1 clean` |

- If `./qmm.sh` says permission denied, run `chmod +x qmm.sh` once, or call it as `bash qmm.sh all`.
- If PowerShell blocks the script, run `powershell -ExecutionPolicy Bypass -File .\qmm.ps1 all`.

Output is colourised in an interactive terminal and plain when redirected to a file. To force colour off, set the `NO_COLOR` environment variable or pass `--no-color` to the `qmm_replay` executable directly.

`run` and `all` exit with code 0 when every ticker matches the LOBSTER ground truth and 1 otherwise. They also write per-5-minute agreement metrics (best bid/ask agreement, depth error, Genesis share at the touch) to `metrics/engineering/<TICKER>_agreement.csv`; the column layout is described in [`metrics/engineering/README.md`](metrics/engineering/README.md).

### Without The Scripts

```
cmake -S . -B build
cmake --build build --config Release
./build/bin/qmm_replay <message.csv> <orderbook.csv> <levels>
```

### Data

Tickers are discovered from `datasets/LOBSTER_SampleFile_<TICKER>_<DATE>_<LEVELS>/`, each folder holding a LOBSTER `*_message_<LEVELS>.csv` and `*_orderbook_<LEVELS>.csv`. Sample files are available from [LOBSTER](https://php.lobsterdata.com/info/DataSamples.php).

Note: not provided as files are too large. Create a `/datasets` folder at root of project, then include your downloaded LOBSTER samples into that directory (simply drag the folder right into it).
