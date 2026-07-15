# Shared helper for RunBackendTests.ps1 / RunFrontendTests.ps1. Runs one Automation
# Spec suite headlessly via UnrealEditor-Cmd and reports pass/fail by scanning the log
# for failure markers (UnrealEditor-Cmd's process exit code does not reliably reflect
# automation test pass/fail across engine versions).
function Invoke-MetaHumanLLMTests {
    param(
        [Parameter(Mandatory = $true)][string]$TestFilter,
        [Parameter(Mandatory = $true)][string]$LogName,
        [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8"
    )

    $ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
    $ProjectPath = Join-Path $ProjectRoot "MetaHumanLocalLLM.uproject"
    $EditorCmd = Join-Path $EngineDir "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    $LogDir = Join-Path $ProjectRoot "Saved\TestLogs"
    $LogPath = Join-Path $LogDir "$LogName.log"

    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

    if (-not (Test-Path $EditorCmd)) {
        Write-Error "UnrealEditor-Cmd.exe not found at $EditorCmd. Pass -EngineDir if UE 5.8 is installed elsewhere."
        return 1
    }

    Write-Host "Running '$TestFilter' tests..." -ForegroundColor Cyan

    # -abslog (not -log, which takes a bare filename under Saved/Logs, not a full path)
    & $EditorCmd $ProjectPath -ExecCmds="Automation RunTests $TestFilter;Quit" -unattended -nopause -nullrhi "-abslog=$LogPath" | Out-Null

    if (-not (Test-Path $LogPath)) {
        Write-Error "No log output captured at $LogPath"
        return 1
    }

    # Read the whole log into memory (avoids Select-String file-handle/encoding races that
    # previously let a Result={Fail} slip past and get reported as a pass).
    $LogText = Get-Content -Path $LogPath -Raw -ErrorAction SilentlyContinue
    if ([string]::IsNullOrEmpty($LogText)) {
        Write-Error "Log at $LogPath was empty or unreadable"
        return 1
    }

    # A crash mid-suite truncates the run: some tests report Success, then the editor dies.
    # Fail loudly on any fatal/crash marker so a partial run can never look like a pass.
    $CrashMarker = [regex]::Match($LogText, '(?m)^.*(appError called|LowLevelFatalError|Fatal error:|Assertion failed).*$')
    if ($CrashMarker.Success) {
        Write-Error "Editor crashed during '$TestFilter' - run is incomplete: $($CrashMarker.Value.Trim())"
        Write-Host "Full log: $LogPath"
        return 1
    }

    # Started-but-never-completed also signals truncation (crash between start and result).
    $StartedCount = [regex]::Matches($LogText, 'Test Started\.').Count
    $CompletedLines = [regex]::Matches($LogText, 'Test Completed\. Result=\{(\w+)\}')
    $FailCount = 0
    $SuccessCount = 0
    $FailureDetail = @()
    foreach ($m in $CompletedLines) {
        if ($m.Groups[1].Value -eq 'Success') { $SuccessCount++ }
        else { $FailCount++; $FailureDetail += $m.Value }
    }

    # Guard against the editor crashing or the filter matching nothing: if no test ever
    # reported a result, that is a failure, not a silent pass.
    if ($CompletedLines.Count -eq 0) {
        Write-Error "No test results found in $LogPath (editor crashed, or filter '$TestFilter' matched no tests)"
        return 1
    }

    if ($StartedCount -gt $CompletedLines.Count) {
        Write-Error "Run truncated: $StartedCount test(s) started but only $($CompletedLines.Count) completed (likely a crash mid-suite). Check $LogPath"
        return 1
    }

    if ($FailCount -gt 0) {
        Write-Host "FAILED - $FailCount of $($CompletedLines.Count) test(s) failed:" -ForegroundColor Red
        $FailureDetail | ForEach-Object { Write-Host "  $_" }
        Write-Host "Full log: $LogPath"
        return 1
    }

    Write-Host "All $SuccessCount '$TestFilter' test(s) passed. Log: $LogPath" -ForegroundColor Green
    return 0
}
