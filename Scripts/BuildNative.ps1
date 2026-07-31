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
if ($isPre56) {
    $json = Get-Content $UProject -Raw | ConvertFrom-Json
    $json.Plugins = @($json.Plugins | Where-Object { $plugins58 -notcontains $_.Name })
    $BuildProject = Join-Path $env:TEMP 'MetaHumanLocalLLM.build.uproject'
    Copy-Item $UProject $BuildProject -Force
    $json | ConvertTo-Json -Depth 12 | Set-Content $BuildProject
    Write-Host "Stripped 5.8-only plugins for a $(Split-Path $Engine -Leaf) build." -ForegroundColor Cyan
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
& $RunUAT @uatArgs
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)" }

Write-Host "OK -> $Archive" -ForegroundColor Green
Write-Host 'Runtime config is environment variables, same as the container:' -ForegroundColor Cyan
Write-Host '  OLLAMA_BASE_URL, OLLAMA_MODEL, KOKORO_SERVER_URL, MHLLM_OPENING_LINE, MHLLM_HUD=0'
