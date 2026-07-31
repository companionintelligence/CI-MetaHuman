<#
.SYNOPSIS
    Packages MetaHumanLocalLLM as a standalone native executable.

.DESCRIPTION
    The project has only ever shipped as a headless Linux container for Pixel Streaming. This
    produces the same game as a normal application you can double-click, which is a different
    build target, not a different program: the companion, face driver, stage director and chat
    box are all plain C++ subsystems that self-attach at BeginPlay, so they come along with no
    per-platform wiring.

    PLATFORM REALITY, stated plainly rather than implied by a -Platform switch that silently
    fails:
      Win64  - builds here. Requires the Windows toolchain (present).
      Linux  - CANNOT build from Windows without the cross-compile toolchain
               (UnrealToolchains, dev-<version>-clang). Not installed. The Linux binary is
               already produced by Deployment/Dockerfile.amd on core-2, which is the supported
               path and needs no toolchain here.
      Mac    - CANNOT be produced on Windows AT ALL. Apple's toolchain does not cross-compile;
               a Mac build requires macOS with Xcode. The target is defined and will build
               there unchanged, but nobody should expect a .app to appear from this machine.

.PARAMETER Platform
    Win64 (default), Linux, or Mac.

.PARAMETER Engine
    Engine root. Defaults to UE_5.5 because that is the version the deployment cooks against;
    5.8 also works since Target.cs uses BuildSettingsVersion.Latest.

.EXAMPLE
    ./Scripts/BuildNative.ps1 -Platform Win64
#>
param(
    [ValidateSet('Win64', 'Linux', 'Mac')]
    [string]$Platform = 'Win64',
    [string]$Engine = 'C:\Program Files\Epic Games\UE_5.5',
    [string]$Config = 'Development',
    [string]$Archive = ''
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'MetaHumanLocalLLM.uproject'
if (-not (Test-Path $UProject)) { throw "Project not found: $UProject" }
if ($Archive -eq '') { $Archive = Join-Path $ProjectRoot "dist/$Platform" }

if ($Platform -eq 'Mac') {
    Write-Host 'Mac cannot be built from Windows. Apple provides no cross-compiler.' -ForegroundColor Yellow
    Write-Host 'On a Mac with Xcode, run:' -ForegroundColor Yellow
    Write-Host '  RunUAT.sh BuildCookRun -project=<path>/MetaHumanLocalLLM.uproject -platform=Mac \'
    Write-Host '    -clientconfig=Development -cook -build -stage -pak -archive -archivedirectory=dist/Mac'
    exit 2
}
if ($Platform -eq 'Linux' -and -not (Test-Path 'C:\UnrealToolchains')) {
    Write-Host 'Linux cross-compile toolchain not installed (C:\UnrealToolchains missing).' -ForegroundColor Yellow
    Write-Host 'The Linux build is produced on core-2 instead:' -ForegroundColor Yellow
    Write-Host '  docker build -f Deployment/Dockerfile.amd --build-arg RELEASE=5.5 .'
    exit 2
}

$RunUAT = Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path $RunUAT)) { throw "RunUAT not found: $RunUAT. Pass -Engine <root>." }

# The 5.8 editor ADDS plugins to the .uproject on its own and marks them REQUIRED, which hard
# fails a 5.5 build with "Unable to find plugin" (this is how AIAssistant appeared). Strip the
# 5.8-only set into a TEMP copy so the developer's own .uproject is never rewritten by a build.
$plugins58 = @('AIAssistant','MCPClientToolset','ModelContextProtocol','AllToolsets','MetaHuman',
               'MetaHumanCharacter','MetaHumanCalibrationProcessing','MetaHumanCoreTech',
               'MetaHumanLiveLink','MetaHumanGenerator','MetaHumanCharacterUAF',
               'MetaHumanCalibrationDiagnostics')
$isPre56 = (Split-Path $Engine -Leaf) -match 'UE_5\.[0-5]'
$BuildProject = $UProject
$UProjectBackup = $null
if ($isPre56) {
    # The project must keep its EXACT name. Cooking a renamed copy
    # (MetaHumanLocalLLM.build.uproject) writes cooked output to Saved/Cooked/Windows/
    # MetaHumanLocalLLM.build/ while the staging step still looks for .../MetaHumanLocalLLM/,
    # and fails with "A package store manifest or project store is required when staging to
    # IoStore" -- a message that says nothing about the rename that caused it.
    #
    # So the real .uproject is edited in place and restored in the finally block below. Uglier
    # than a copy, but it is the only form that keeps cook and stage agreeing on the name.
    $UProjectBackup = "$UProject.orig"
    Copy-Item $UProject $UProjectBackup -Force
    $json = Get-Content $UProject -Raw | ConvertFrom-Json
    $json.Plugins = @($json.Plugins | Where-Object { $plugins58 -notcontains $_.Name })
    $json | ConvertTo-Json -Depth 12 | Set-Content $UProject
    Write-Host "Stripped 5.8-only plugins for a $(Split-Path $Engine -Leaf) build (restored afterwards)." -ForegroundColor Cyan
}

# Intermediate/ShaderAutogen is written by whichever ENGINE last touched this project. This
# project is opened in the 5.8 editor but cooks against 5.5, so a 5.8-generated
# AutogenShaderHeaders.ush gets reused by the 5.5 shader compiler and fails with errors that
# name engine internals rather than anything in this project -- "no member named
# 'NormalDistribution' in 'FGBufferData'", "undeclared identifier 'SceneTexturesStruct'".
# Nothing in the message suggests a stale-cache problem, so clear it rather than debug it.
$autogen = Join-Path $ProjectRoot 'Intermediate\ShaderAutogen'
if (Test-Path $autogen) {
    Remove-Item $autogen -Recurse -Force
    Write-Host 'Cleared Intermediate/ShaderAutogen (written by a different engine version).' -ForegroundColor Cyan
}

Write-Host "Packaging $Platform ($Config) -> $Archive" -ForegroundColor Cyan
# Every switch is an explicitly-quoted string. A bare -platform=$Platform token is NOT
# interpolated by PowerShell in argument mode -- it reaches UAT as the literal text
# "$Platform", which fails with "The platform name $Platform is not a valid platform name".
$uatArgs = @(
    'BuildCookRun',
    "-project=$BuildProject",
    "-platform=$Platform",
    "-clientconfig=$Config",
    '-cook', '-build', '-stage', '-pak', '-archive',
    "-archivedirectory=$Archive",
    '-utf8output', '-nodebuginfo', '-noP4'
)
try {
    & $RunUAT @uatArgs
    $uatExit = $LASTEXITCODE
}
finally {
    # ALWAYS restore, including on Ctrl-C: leaving the developer's .uproject stripped of its
    # editor plugins would silently break the next editor session.
    if ($UProjectBackup -and (Test-Path $UProjectBackup)) {
        Move-Item $UProjectBackup $UProject -Force
        Write-Host 'Restored the original .uproject.' -ForegroundColor Cyan
    }
}
if ($uatExit -ne 0) { throw "BuildCookRun failed ($uatExit)" }

Write-Host "OK -> $Archive" -ForegroundColor Green
Write-Host 'Runtime config is environment variables, same as the container:' -ForegroundColor Cyan
Write-Host '  OLLAMA_BASE_URL, OLLAMA_MODEL, KOKORO_SERVER_URL, MHLLM_OPENING_LINE, MHLLM_HUD=0'
