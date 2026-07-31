# Builds and tests the CICompanion plugin against BOTH installed engines.
#
# WHY THIS EXISTS
# RunBackendTests.ps1 / RunFrontendTests.ps1 default to UE 5.8, so the existing suite -- however
# green -- proves nothing about UE 5.5. That is a problem specifically for the compatibility layer,
# whose whole substance is #if branches: a pass on one engine only ever exercises one side of them.
#
# WHY A HARNESS PROJECT INSTEAD OF THE REAL ONE
# MetaHumanLocalLLM cannot be opened by the 5.5 editor. Not because of the plugin -- .uasset
# serialization is forward-only, and Content/ holds MetaHuman and Character Creator assets saved by
# 5.8. Pointing the 5.5 editor at the real project fails on content load before a single test runs.
#
# So this generates a THROWAWAY project that contains only a stub module and a junction to the real
# CICompanion plugin: no Content, no MetaHuman assets, nothing version-locked. That is exactly the
# scope the compatibility tests need, and it is why they were written to be hermetic (no editor
# world, no GPU, no rigged mesh, no live Ollama).
#
# ASCII ONLY. Windows PowerShell 5.1 reads these scripts as ANSI, and a non-ASCII character such as
# an em-dash breaks quote parsing. RunTests.Common.ps1 carries the same warning; it was hit twice.
#
# Usage:
#   pwsh Scripts/RunCompatMatrix.ps1
#   pwsh Scripts/RunCompatMatrix.ps1 -Engines "5.8"
#   pwsh Scripts/RunCompatMatrix.ps1 -TestFilter "MetaHumanLocalLLM.Backend"

param(
    [string[]]$Engines = @("5.5", "5.8"),
    [string]$TestFilter = "MetaHumanLocalLLM.Backend.Compat",
    [string]$EngineRoot = "C:\Program Files\Epic Games",
    [switch]$KeepHarness
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$PluginSource = Join-Path $ProjectRoot "Plugins\CICompanion"
$HarnessRoot = Join-Path $env:TEMP "CICompanionCompat"
$HarnessName = "CICompanionCompat"

if (-not (Test-Path $PluginSource)) {
    Write-Error "CICompanion plugin not found at $PluginSource"
    exit 1
}

function New-Harness {
    param([string]$Dir, [string]$EngineVersion)

    if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $Dir "Source\$HarnessName") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $Dir "Plugins") | Out-Null

    # CICompanion is junctioned rather than copied so the tests run against the working tree, not a
    # snapshot -- editing a spec and re-running must not silently test the old code.
    $LinkPath = Join-Path $Dir "Plugins\CICompanion"
    cmd /c mklink /J "$LinkPath" "$PluginSource" | Out-Null
    if (-not (Test-Path $LinkPath)) {
        Write-Error "Failed to junction the plugin into the harness at $LinkPath"
        return $false
    }

    # Only CICompanion is enabled. None of the 5.8-only plugins are referenced at all, so this
    # project loads on either engine without needing the "Optional" mechanism.
    $Uproject = @{
        FileVersion        = 3
        EngineAssociation  = $EngineVersion
        Modules            = @(@{ Name = $HarnessName; Type = "Runtime"; LoadingPhase = "Default" })
        Plugins            = @(@{ Name = "CICompanion"; Enabled = $true })
    } | ConvertTo-Json -Depth 10
    Set-Content -Path (Join-Path $Dir "$HarnessName.uproject") -Value $Uproject -Encoding ASCII

    # BuildSettingsVersion.Latest matches what MetaHumanLocalLLM.Target.cs uses, so the harness
    # compiles the plugin under the same envelope the real project does. That matters: Latest is V5
    # on 5.5.4 and V7 on 5.8.1, and V6+V7 add five warnings-as-errors. Pinning something else here
    # would let a warning-as-error slip through the matrix and fail the real build instead.
    $Target = @"
using UnrealBuildTool;
using System.Collections.Generic;

public class ${HarnessName}Target : TargetRules
{
	public ${HarnessName}Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "$HarnessName" });
	}
}
"@
    Set-Content -Path (Join-Path $Dir "Source\$HarnessName.Target.cs") -Value $Target -Encoding ASCII

    $EditorTarget = $Target -replace "${HarnessName}Target", "${HarnessName}EditorTarget" -replace "TargetType.Game", "TargetType.Editor"
    Set-Content -Path (Join-Path $Dir "Source\${HarnessName}Editor.Target.cs") -Value $EditorTarget -Encoding ASCII

    $ModuleBuild = @"
using UnrealBuildTool;

public class $HarnessName : ModuleRules
{
	public $HarnessName(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
"@
    Set-Content -Path (Join-Path $Dir "Source\$HarnessName\$HarnessName.Build.cs") -Value $ModuleBuild -Encoding ASCII

    $ModuleCpp = @"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, $HarnessName, "$HarnessName");
"@
    Set-Content -Path (Join-Path $Dir "Source\$HarnessName\$HarnessName.cpp") -Value $ModuleCpp -Encoding ASCII

    return $true
}

$Results = @()

foreach ($Version in $Engines) {
    $EngineDir = Join-Path $EngineRoot "UE_$Version"
    $BuildBat = Join-Path $EngineDir "Engine\Build\BatchFiles\Build.bat"
    $EditorCmd = Join-Path $EngineDir "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

    Write-Host ""
    Write-Host "=== UE $Version ===" -ForegroundColor Cyan

    if (-not (Test-Path $BuildBat)) {
        Write-Host "  SKIP: no engine at $EngineDir" -ForegroundColor Yellow
        $Results += [pscustomobject]@{ Engine = $Version; Build = "skipped"; Tests = "skipped" }
        continue
    }

    $Dir = "$HarnessRoot-$Version"
    if (-not (New-Harness -Dir $Dir -EngineVersion $Version)) {
        $Results += [pscustomobject]@{ Engine = $Version; Build = "harness-failed"; Tests = "skipped" }
        continue
    }

    $UprojectPath = Join-Path $Dir "$HarnessName.uproject"
    $LogDir = Join-Path $ProjectRoot "Saved\TestLogs"
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    $BuildLog = Join-Path $LogDir "CompatBuild-$Version.log"
    $TestLog = Join-Path $LogDir "CompatTests-$Version.log"

    Write-Host "  Building ${HarnessName}Editor ..." -ForegroundColor Gray
    & $BuildBat "${HarnessName}Editor" Win64 Development -project="$UprojectPath" -waitmutex 2>&1 |
        Tee-Object -FilePath $BuildLog | Out-Null
    $BuildOk = ($LASTEXITCODE -eq 0)

    if (-not $BuildOk) {
        Write-Host "  BUILD FAILED. Log: $BuildLog" -ForegroundColor Red
        Get-Content $BuildLog -Tail 25 | ForEach-Object { Write-Host "    $_" }
        $Results += [pscustomobject]@{ Engine = $Version; Build = "FAIL"; Tests = "skipped" }
        continue
    }
    Write-Host "  Build OK" -ForegroundColor Green

    Write-Host "  Running '$TestFilter' ..." -ForegroundColor Gray
    & $EditorCmd "$UprojectPath" -ExecCmds="Automation RunTests $TestFilter;Quit" `
        -unattended -nopause -nullrhi "-abslog=$TestLog" | Out-Null

    # Same pass/fail discipline as RunTests.Common.ps1: the process exit code does not reliably
    # reflect automation results, so the log is the source of truth and every truncation mode counts
    # as a failure rather than a silent pass.
    $TestStatus = "NO LOG"
    if (Test-Path $TestLog) {
        $LogText = Get-Content -Path $TestLog -Raw -ErrorAction SilentlyContinue
        $Crash = [regex]::Match($LogText, '(?m)^.*(appError called|LowLevelFatalError|Fatal error:|Assertion failed).*$')
        $Started = [regex]::Matches($LogText, 'Test Started\.').Count
        $Completed = [regex]::Matches($LogText, 'Test Completed\. Result=\{(\w+)\}')
        $Failed = @($Completed | Where-Object { $_.Groups[1].Value -ne 'Success' })

        if ($Crash.Success) { $TestStatus = "CRASH: $($Crash.Value.Trim())" }
        elseif ($Completed.Count -eq 0) { $TestStatus = "NO RESULTS (crash on boot, or filter matched nothing)" }
        elseif ($Started -gt $Completed.Count) { $TestStatus = "TRUNCATED ($Started started, $($Completed.Count) completed)" }
        elseif ($Failed.Count -gt 0) { $TestStatus = "FAIL ($($Failed.Count)/$($Completed.Count))" }
        else { $TestStatus = "PASS ($($Completed.Count))" }
    }

    if ($TestStatus -like "PASS*") { Write-Host "  Tests $TestStatus" -ForegroundColor Green }
    else { Write-Host "  Tests $TestStatus  Log: $TestLog" -ForegroundColor Red }

    $Results += [pscustomobject]@{ Engine = $Version; Build = "OK"; Tests = $TestStatus }

    if (-not $KeepHarness) { Remove-Item -Recurse -Force $Dir -ErrorAction SilentlyContinue }
}

Write-Host ""
Write-Host "=== Compatibility matrix ===" -ForegroundColor Cyan
$Results | Format-Table -AutoSize

$Passed = @($Results | Where-Object { $_.Tests -like "PASS*" })
$Bad = @($Results | Where-Object { $_.Build -eq "FAIL" -or $_.Build -eq "harness-failed" -or ($_.Tests -notlike "PASS*" -and $_.Tests -ne "skipped") })

# A matrix where nothing actually ran is a FAILURE, not a pass. Without this, a typo in -Engines
# (or both engines missing) skips every row and exits 0, reporting success for a matrix that
# verified nothing. Docs/FOLLOW-UP.md records two separate false-pass bugs of exactly this shape in
# RunTests.Common.ps1; this guard is here so the matrix runner does not become the third.
if ($Passed.Count -eq 0) {
    Write-Host "NOTHING RAN: no engine produced a passing suite. Check -Engines and -EngineRoot." -ForegroundColor Red
    exit 1
}
if ($Bad.Count -gt 0) { exit 1 }

# Passing on only one engine cannot prove a compatibility layer -- that is the entire reason this
# script exists -- so say so rather than printing a clean green result.
if ($Passed.Count -lt 2) {
    Write-Host "WARNING: only $($Passed.Count) engine verified. Both branches of the #if layer are NOT covered." -ForegroundColor Yellow
}
exit 0
