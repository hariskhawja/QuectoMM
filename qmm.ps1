<#
.SYNOPSIS
    QuectoMM helper for Windows (PowerShell 5.1+ or PowerShell 7).

.EXAMPLE
    .\qmm.ps1 build          # configure (first time) + compile
    .\qmm.ps1 run AAPL       # validate one ticker
    .\qmm.ps1 all            # validate every ticker in datasets/
    .\qmm.ps1 clean          # delete the build directory
    .\qmm.ps1 list           # show available tickers

    If scripts are blocked, run:
    powershell -ExecutionPolicy Bypass -File .\qmm.ps1 all
#>
param(
    [Parameter(Position = 0)] [string] $Command = "help",
    [Parameter(Position = 1)] [string] $Ticker
)

$ErrorActionPreference = "Stop"
$Root     = $PSScriptRoot
$BuildDir = Join-Path $Root "build"
$Datasets = Join-Path $Root "datasets"

# The engine disables colour when its stdout is not a TTY, and the engine's own
# stdout looks non-interactive once it is launched from inside this script. So
# when this script is itself attached to an interactive console, tell the engine
# to keep colour on. Honour NO_COLOR, and skip when our own output is redirected
# (e.g. `.\qmm.ps1 all > log.txt`) so colour codes don't land in the file.
$qmmInteractive = $false
try { $qmmInteractive = -not [System.Console]::IsOutputRedirected -and $Host.Name -ne $null } catch {}
if (-not $env:NO_COLOR -and $qmmInteractive) {
    $env:QMM_FORCE_COLOR = "1"
} else {
    Remove-Item Env:\QMM_FORCE_COLOR -ErrorAction SilentlyContinue
}

function Invoke-Configure {
    if (Test-Path (Join-Path $BuildDir "CMakeCache.txt")) { return }
    $cmakeArgs = @("-S", $Root, "-B", $BuildDir)
    # Ninja is optional; use it when installed, otherwise CMake picks a default.
    if (Get-Command ninja -ErrorAction SilentlyContinue) { $cmakeArgs += @("-G", "Ninja") }
    Write-Host "Configuring: cmake $($cmakeArgs -join ' ')"
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
}

function Invoke-Build {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake not found on PATH. Install CMake 3.20+."
    }
    Invoke-Configure
    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
}

function Get-Exe {
    foreach ($name in "qmm_replay.exe", "qmm_replay") {
        $p = Join-Path (Join-Path $BuildDir "bin") $name
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Get-ExeOrBuild {
    $exe = Get-Exe
    if (-not $exe) { Invoke-Build; $exe = Get-Exe }
    if (-not $exe) { throw "qmm_replay executable not found after build." }
    return $exe
}

# Folders are named LOBSTER_SampleFile_<TICKER>_<DATE>_<LEVELS>.
function Get-TickerDirs {
    if (-not (Test-Path $Datasets)) { return @() }
    return @(Get-ChildItem $Datasets -Directory -Filter "LOBSTER_SampleFile_*" | Sort-Object Name)
}

function Get-TickerName($dir) { return ($dir.Name -split "_")[2] }

function Invoke-Ticker($dir, $exe) {
    $msg = Get-ChildItem $dir.FullName -Filter "*_message_*.csv"   | Select-Object -First 1
    $ob  = Get-ChildItem $dir.FullName -Filter "*_orderbook_*.csv" | Select-Object -First 1
    if (-not $msg -or -not $ob) { throw "Missing message/orderbook CSV in $($dir.FullName)" }
    # Depth comes from the file name suffix, e.g. ..._message_10.csv -> 10.
    $levels = [System.IO.Path]::GetFileNameWithoutExtension($msg.Name).Split("_")[-1]

    # Per-5-minute agreement metrics (see metrics/engineering/README.md).
    $csv = Join-Path $Root "metrics/engineering/$(Get-TickerName $dir)_agreement.csv"

    # Out-Host is required: without it the exe's output becomes this function's
    # return value, so it is swallowed by the caller AND the returned array is
    # always truthy, which would report FAIL tickers as PASS.
    # Colour still works: QMM_FORCE_COLOR (set at the top) makes the exe emit
    # ANSI codes even though its stdout is a pipe, and Out-Host writes them as-is.
    & $exe $msg.FullName $ob.FullName $levels $csv | Out-Host
    return ($LASTEXITCODE -eq 0)
}

function Find-TickerDir($t) {
    $want = $t.ToUpper()
    foreach ($d in Get-TickerDirs) { if ((Get-TickerName $d) -eq $want) { return $d } }
    return $null
}

function Show-Help {
    Write-Host @"
QuectoMM helper

Usage: .\qmm.ps1 <command> [ticker]

Commands:
  build          Configure (first time) and compile
  run <TICKER>   Validate one ticker, e.g. .\qmm.ps1 run AAPL
  all            Validate every ticker found in datasets/
  list           List available tickers
  clean          Delete the build directory
  help           Show this message
"@
}

switch ($Command.ToLower()) {
    "build" { Invoke-Build }
    "run" {
        if (-not $Ticker) { throw "Usage: .\qmm.ps1 run <TICKER>   (see .\qmm.ps1 list)" }
        $dir = Find-TickerDir $Ticker
        if (-not $dir) { throw "No dataset for '$Ticker'. Available: $((Get-TickerDirs | ForEach-Object { Get-TickerName $_ }) -join ', ')" }
        $exe = Get-ExeOrBuild
        if (-not (Invoke-Ticker $dir $exe)) { exit 1 }
    }
    "all" {
        $dirs = Get-TickerDirs
        if ($dirs.Count -eq 0) { throw "No datasets found in $Datasets" }
        $exe = Get-ExeOrBuild
        $results = @()
        foreach ($d in $dirs) {
            $ok = Invoke-Ticker $d $exe
            $results += [pscustomobject]@{ Ticker = (Get-TickerName $d); Result = $(if ($ok) { "PASS" } else { "FAIL" }) }
        }
        Write-Host ""
        Write-Host "===== Summary =====" -ForegroundColor Cyan
        Write-Host ("{0,-8}{1}" -f "Ticker", "Result")
        Write-Host ("{0,-8}{1}" -f "------", "------")
        foreach ($r in $results) {
            Write-Host ("{0,-8}" -f $r.Ticker) -NoNewline
            if ($env:NO_COLOR) {
                Write-Host $r.Result
            } else {
                Write-Host $r.Result -ForegroundColor $(if ($r.Result -eq "PASS") { "Green" } else { "Red" })
            }
        }
        Write-Host ""
        if ($results.Result -contains "FAIL") { exit 1 }
    }
    "list"  { Get-TickerDirs | ForEach-Object { Get-TickerName $_ } }
    "clean" {
        if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir; Write-Host "Removed $BuildDir" }
        else { Write-Host "Nothing to clean." }
    }
    default { Show-Help }
}
