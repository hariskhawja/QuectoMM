# QuectoMM
High-Frequency Market-Making Research Engine

## Requirements

- CMake 3.20 or newer
- C++20 compiler: GCC 10+, Clang 12+, or MSVC 2019 16.10+ (Visual Studio)
- Optional: [Ninja](https://ninja-build.org/). The scripts use it when it is installed and fall back to CMake's default generator otherwise.

Tested on Windows with MinGW-w64 GCC 16.2 and Ninja, through both PowerShell and Git Bash. Other platforms and compilers have not been tested yet.

## Build and Run

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

### Without the scripts

```
cmake -S . -B build
cmake --build build --config Release
./build/bin/qmm_replay <message.csv> <orderbook.csv> <levels>
```

## Data

Tickers are discovered from `datasets/LOBSTER_SampleFile_<TICKER>_<DATE>_<LEVELS>/`, each folder holding a LOBSTER `*_message_<LEVELS>.csv` and `*_orderbook_<LEVELS>.csv`. Sample files are available from [LOBSTER](https://php.lobsterdata.com/info/DataSamples.php).

Note: not provided as files are too large.
