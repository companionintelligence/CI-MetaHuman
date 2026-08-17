# Shared helper for RunBackendTests.ps1 / RunFrontendTests.ps1. Runs one Automation
# Spec suite headlessly via UnrealEditor-Cmd and reports pass/fail by scanning the log
# for failure markers (UnrealEditor-Cmd's process exit code does not reliably reflect
# automation test pass/fail across engine versions).
function Invoke-MetaHumanLLMTests {
    param(
        [Parameter(Mandatory = $true)][string]$TestFilter,
        [Parameter(Mandatory = $true)][string]$LogName,
        [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8",
        # Escape hatch for running the same binary twice (e.g. backend then frontend) without
        # paying for a second no-op build. Never skip it on the FIRST run of a session.
        [switch]$SkipBuild
    )

    $ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
    $ProjectPath = Join-Path $ProjectRoot "MetaHumanLocalLLM.uproject"
    $EditorCmd = Join-Path $EngineDir "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    $BuildBat = Join-Path $EngineDir "Engine\Build\BatchFiles\Build.bat"
    $LogDir = Join-Path $ProjectRoot "Saved\TestLogs"
    $LogPath = Join-Path $LogDir "$LogName.log"

    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

    if (-not (Test-Path $EditorCmd)) {
        Write-Error "UnrealEditor-Cmd.exe not found at $EditorCmd. Pass -EngineDir if UE 5.8 is installed elsewhere."
        return 1
    }

    # COMPILE FIRST. This function used to launch the editor directly, which loads whatever DLL was
    # last built and happily reports a full green suite for source that is not on disk. That is the
    # most dangerous shape of false pass this repo has: every other guard below exists to stop a
    # crashed or truncated run being read as a pass, and none of them can tell you that the code
    # under test was never compiled. RunCompatMatrix.ps1 already builds; this did not.
    if (-not $SkipBuild) {
        if (-not (Test-Path $BuildBat)) {
            Write-Error "Build.bat not found at $BuildBat"
            return 1
        }
        Write-Host "Building MetaHumanLocalLLMEditor before testing..." -ForegroundColor Cyan
        # -NoHotReloadFromIDE for the same reason BuildNative.ps1 passes it. Live Coding's guard
        # mutex is named after the EXECUTABLE, and every project's editor target produces the same
        # one: <engine>\Engine\Binaries\Win64\UnrealEditor.exe. So an editor open on a COMPLETELY
        # DIFFERENT project is enough to fail this build with exit 6 and "Unable to build while Live
        # Coding is active" -- a message that reads as if THIS project were open, which it is not.
        # Without the flag the suite cannot be run at all on a box where any 5.8 editor is up, which
        # is the normal state of a developer machine.
        & $BuildBat MetaHumanLocalLLMEditor Win64 Development "-project=$ProjectPath" -waitmutex -NoHotReloadFromIDE
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Build FAILED (exit $LASTEXITCODE) - not running tests against a stale binary."
            return 1
        }
    }

    Write-Host "Running '$TestFilter' tests..." -ForegroundColor Cyan

    # -abslog (not -log, which takes a bare filename under Saved/Logs, not a full path)
    # The MCP ini override stops the Model Context Protocol plugin auto-starting its HTTP server on
    # the fixed port 8000, which collides with the developer's own editor if one is open and adds a
    # bind error to every log. BuildNative.ps1 already passes the same flag.
    $McpOff = '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False'
    & $EditorCmd $ProjectPath -ExecCmds="Automation RunTests $TestFilter;Quit" -unattended -nopause -nullrhi $McpOff "-abslog=$LogPath" | Out-Null

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
