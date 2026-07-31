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

.PARAMETER SkipAuthoring
    Cook whatever Content/Maps/CompanionStage.umap happens to be on disk instead of authoring a
    fresh one. Fast, and almost always wrong: the committed .umap is a 23 KB STUB (sky + ground,
    no resident, no set), so a build made this way opens onto an empty stage and logs
    "No resident found; face driver inactive".

.PARAMETER KeepAuthoredMap
    Leave the freshly authored .umap in Content/Maps instead of restoring the committed stub
    after the cook. Use it to open the real set in the editor; do NOT commit the result (see
    Author-Stage below for why an authored map is engine-version-specific).

.PARAMETER GlbDir
    Directory holding the prop GLBs. Defaults to $env:MHLLM_GLB_DIR, then to
    C:\devel\llm-metahuman\scene\glb.

.PARAMETER NiaContent
    The Reallusion Character Creator "Nia" content root (the folder named CC_ControlRig_Nia).
    Defaults to $env:MHLLM_NIA_CONTENT, then to a CharacterCreatorNia project sitting beside
    this one.

.EXAMPLE
    ./Scripts/BuildNative.ps1 -Platform Win64 -Engine "C:\Program Files\Epic Games\UE_5.8"
#>
param(
    [ValidateSet('Win64', 'Linux', 'Mac')]
    [string]$Platform = 'Win64',
    [string]$Engine = 'C:\Program Files\Epic Games\UE_5.5',
    [string]$Config = 'Development',
    [string]$Archive = '',
    [switch]$SkipAuthoring,
    [switch]$KeepAuthoredMap,
    [string]$GlbDir = '',
    [string]$NiaContent = ''
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'MetaHumanLocalLLM.uproject'
if (-not (Test-Path $UProject)) { throw "Project not found: $UProject" }
if ($Archive -eq '') { $Archive = Join-Path $ProjectRoot "dist/$Platform" }
$StageMap = Join-Path $ProjectRoot 'Content\Maps\CompanionStage.umap'

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

# ---------------------------------------------------------------------------------------------
# Author-Stage: build the diorama with the SAME script the server cook uses.
#
# ROOT CAUSE THIS EXISTS TO FIX. Content/Maps/CompanionStage.umap in the repo is a 23 KB stub —
# sky and ground, nothing else. The real set (the 1970s machine hall, the props, the lighting,
# and Nia herself) has only ever existed as the OUTPUT of Deployment/author_diorama.py, which
# ran exclusively on core-2 inside a Linux container. Nothing on this path ever invoked it, so a
# native build cooked the stub, and the shipped game logged "No resident found; face driver
# inactive" / "Tape reels spinning: 0" and opened onto an empty stage.
#
# WHY AUTHOR RATHER THAN COMMIT THE MAP. The set is DEFINED by ~960 lines of layout, lighting and
# material logic. Committing the .umap would freeze a build artifact as source and guarantee it
# drifts from the script the moment anyone edits the set — the exact drift trap HANDOFF.md
# Lesson 2 describes. It is also ~900 MB of Nia content behind it, and a .umap is
# ENGINE-VERSION-SPECIFIC: one authored by 5.8 here cannot be loaded by the 5.5 cook on core-2.
# So the map stays generated, and this function makes the native build generate it the same way
# the container does — one source of truth, two consumers.
function Invoke-StageAuthoring {
    $editorCmd = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    if (-not (Test-Path $editorCmd)) { throw "UnrealEditor-Cmd not found: $editorCmd" }

    $script = Join-Path $ProjectRoot 'Deployment\author_diorama.py'
    if (-not (Test-Path $script)) { throw "Authoring script missing: $script" }

    # Inputs. author_diorama.py reads these through MHLLM_* env vars; its defaults are the
    # container mount points (/glb, /mh, /audio), so the server pipeline is untouched.
    $glb = if ($GlbDir) { $GlbDir } elseif ($env:MHLLM_GLB_DIR) { $env:MHLLM_GLB_DIR }
           else { 'C:\devel\llm-metahuman\scene\glb' }
    $nia = if ($NiaContent) { $NiaContent } elseif ($env:MHLLM_NIA_CONTENT) { $env:MHLLM_NIA_CONTENT }
           else { Join-Path (Split-Path -Parent $ProjectRoot) 'CharacterCreatorNia\Content\CC_ControlRig_Nia' }

    if (-not (Test-Path $glb)) {
        throw "Prop GLBs not found: $glb. Pass -GlbDir <path> or set MHLLM_GLB_DIR."
    }
    if (-not (Test-Path (Join-Path $nia 'RLContent\Nia\SK_Nia.uasset'))) {
        throw ("Nia content not found: $nia (expected RLContent\Nia\SK_Nia.uasset). " +
               'Pass -NiaContent <path> or set MHLLM_NIA_CONTENT.')
    }

    # Nia has to live under THIS project's Content/ because UE assets reference each other by
    # package path and the script loads her from /Game/CC_ControlRig_Nia/RLContent/Nia/SK_Nia.
    # Copied, not junctioned: the authoring run re-saves what it loads, and a junction would
    # quietly rewrite the CharacterCreatorNia project's own assets into this engine's version.
    # robocopy is incremental, so only the first build pays the ~900 MB.
    $niaDest = Join-Path $ProjectRoot 'Content\CC_ControlRig_Nia'
    Write-Host "Staging Nia content -> Content\CC_ControlRig_Nia (incremental)" -ForegroundColor Cyan
    foreach ($sub in @('RLContent', 'CC_Shaders', 'CC_Rigs')) {
        $from = Join-Path $nia $sub
        if (-not (Test-Path $from)) { continue }
        robocopy $from (Join-Path $niaDest $sub) /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
        # robocopy's exit code is a BIT FIELD, not a status: 0-7 are success (1 = files copied,
        # 2 = extras, 4 = mismatches). Only >= 8 is a real failure. Testing it like a normal
        # exit code fails every build that actually copies something.
        if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE) staging $from" }
    }

    # Purge what the last authoring run produced. The container gets this for free by wiping
    # Content/ before it starts; here, leftovers make the re-import collide with itself and UE
    # silently renames the new assets (T_ibm_3178 -> T_ibm_31781), so the map ends up pointing at
    # one generation of textures and the tree accumulates another.
    foreach ($gen in @('Content\Diorama', 'Content\Seo', 'Content\Audio')) {
        $p = Join-Path $ProjectRoot $gen
        if (Test-Path $p) { Remove-Item $p -Recurse -Force }
    }
    if (Test-Path $StageMap) { Remove-Item $StageMap -Force }

    $result = Join-Path $ProjectRoot 'Saved\AUTHOR_RESULT.txt'
    New-Item -ItemType Directory -Force (Split-Path -Parent $result) | Out-Null
    if (Test-Path $result) { Remove-Item $result -Force }

    $env:MHLLM_GLB_DIR = $glb
    $env:MHLLM_MH_DIR = Join-Path $ProjectRoot 'Deployment\metahuman'
    $env:MHLLM_AUDIO_DIR = Join-Path $ProjectRoot 'Deployment\audio'
    # Synthesized set dressing (CRT screen texture + room tone), committed under Deployment/.
    # author_diorama.py defaults MHLLM_GEN_DIR to the GLB directory because on core-2 that is
    # where these two files live; pointing it into the repo is what stops the native build
    # depending on a machine it is not running on.
    $gen = Join-Path $ProjectRoot 'Deployment\generated'
    foreach ($f in @('T_CRT_Vector.png', 'S_RoomTone.wav')) {
        if (-not (Test-Path (Join-Path $gen $f))) {
            throw ("Generated set dressing missing: $gen\$f. Rebuild it with " +
                   'Scripts/MakeCRTTexture.py and Scripts/MakeAmbience.py --mode roomtone.')
        }
    }
    $env:MHLLM_GEN_DIR = $gen
    $env:MHLLM_AUTHOR_RESULT = $result

    Write-Host "Authoring the diorama with $script (headless editor, this takes a few minutes)" -ForegroundColor Cyan
    $log = Join-Path $ProjectRoot 'Saved\author_diorama.log'
    & $editorCmd $UProject -run=pythonscript "-script=$script" `
        -unattended -nullrhi -nosplash -noP4 -stdout -utf8output -NoLogTimes `
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False' `
        > $log 2>&1
    # The commandlet exits non-zero on its ERROR COUNT alone, and this run logs benign ones (an
    # OpenXR loader complaint among them). AUTHOR_RESULT.txt is the real verdict, so gate on that
    # rather than on $LASTEXITCODE.

    if (-not (Test-Path $result)) { throw "Authoring produced no result file. See $log" }
    $lines = Get-Content $result
    $lines | Where-Object { $_ -match 'resident:|Nia:|729|placed|SUCCESS|FAIL' } | ForEach-Object { "  $_" }

    # FAIL FAST, loudly, on every condition that has previously shipped an empty stage.
    if (-not ($lines -match '^SUCCESS')) { throw "Authoring did not report SUCCESS. See $log and $result" }
    if (-not ($lines -match 'resident: NIA placed')) { throw "Authoring placed no resident. See $result" }
    # Both of these ship in the repo now, so a miss is a regression rather than a missing optional
    # input. They used to fail SILENTLY -- try/except around the import, no line in the result
    # either way -- and the build went out with dead screens and a silent hall for weeks.
    # Match the COUNT, not just the words: the authoring line reports "N of 3" precisely so this
    # gate can tell "placed three screens" from "placed nothing and said so cheerfully".
    if (-not ($lines -match 'CRT vector displays placed: [1-9]')) {
        throw "Authoring placed no CRT displays (screens will read as painted plastic). See $result"
    }
    if (-not ($lines -match 'room tone placed')) {
        throw "Authoring placed no room tone (the hall will be silent). See $result"
    }
    if (-not (Test-Path $StageMap)) { throw "Authoring reported SUCCESS but wrote no $StageMap" }
    $size = (Get-Item $StageMap).Length
    if ($size -lt 200KB) { throw "Authored map is only $size bytes — that is the empty stub, not the set." }
    Write-Host "Authored CompanionStage.umap ($([math]::Round($size/1MB,2)) MB)" -ForegroundColor Green
}

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
    '-utf8output', '-nodebuginfo', '-noP4',
    # Live Coding's guard mutex is named after the EXECUTABLE, and every project's editor target
    # produces the same one: <engine>\Engine\Binaries\Win64\UnrealEditor.exe. So an editor open on
    # a COMPLETELY DIFFERENT project (CharacterCreatorNia here) holds
    # "Global\LiveCoding_C+\Program Files\...\UnrealEditor.exe" and UBT refuses this project's
    # build with "Unable to build while Live Coding is active. Exit the editor and game" --
    # a message that reads as if THIS project were open, which it is not.
    # (UnrealBuildTool/System/HotReload.cs:273 CheckForLiveCodingSessionActive, mutex built at :293.)
    # Bypassing is safe for packaging: an installed engine's own binaries are never relinked, and
    # the only DLLs this build writes -- UnrealEditor-MetaHumanLocalLLM.dll and
    # UnrealEditor-CICompanion.dll -- live under THIS project and are not loaded by that editor.
    '-ubtargs=-NoHotReloadFromIDE',
    # Config/DefaultEditorPerProjectUserSettings.ini turns the Model Context Protocol server on
    # (bAutoStartServer=True) at the FIXED port 8000, so it starts in every process that loads
    # this project -- including the headless UnrealEditor-Cmd the cook runs in. If any other UE
    # 5.8 editor is open (a different project is enough; the port is per-machine, not
    # per-project) the bind fails, logs "LogHttpListener: Error: HttpListener unable to bind to
    # 127.0.0.1:8000", and the cook commandlet exits 1 on the error count alone -- surfacing as
    # Error_UnknownCookFailure, which points nowhere near a port conflict.
    #
    # Overridden on the COOK COMMAND LINE rather than in the .ini, so the developer's editor keeps
    # its MCP server exactly as configured. A packaged game has no use for one.
    '-additionalcookeroptions=-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False'
)
# The committed stub is put back after the cook (see the finally block). An authored .umap is a
# build artifact of ONE engine version, and this tree is cooked by two: leaving a 5.8-authored map
# in Content/Maps would make `git status` offer it for commit, and Dockerfile.amd's
# "Using the SUPPLIED CompanionStage.umap" branch would then hand a 5.8 asset to the 5.5 cook.
$StageMapBackup = $null
if (-not $SkipAuthoring -and (Test-Path $StageMap)) {
    $StageMapBackup = "$StageMap.orig"
    Copy-Item $StageMap $StageMapBackup -Force
}
try {
    if ($SkipAuthoring) {
        Write-Host 'Skipping stage authoring: cooking whatever .umap is on disk.' -ForegroundColor Yellow
        Write-Host 'If that is the committed stub, the build will open onto an EMPTY STAGE.' -ForegroundColor Yellow
    }
    else {
        Invoke-StageAuthoring
    }
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
    if ($StageMapBackup -and (Test-Path $StageMapBackup)) {
        if ($KeepAuthoredMap) {
            Remove-Item $StageMapBackup -Force
            Write-Host 'Kept the authored CompanionStage.umap (-KeepAuthoredMap). Do not commit it.' -ForegroundColor Yellow
        }
        else {
            Move-Item $StageMapBackup $StageMap -Force
            Write-Host 'Restored the committed CompanionStage.umap stub (the cook already has the real one).' -ForegroundColor Cyan
        }
    }
}
if ($uatExit -ne 0) { throw "BuildCookRun failed ($uatExit)" }

Write-Host "OK -> $Archive" -ForegroundColor Green
Write-Host 'Runtime config is environment variables, same as the container:' -ForegroundColor Cyan
Write-Host '  OLLAMA_BASE_URL, OLLAMA_MODEL, KOKORO_SERVER_URL, MHLLM_OPENING_LINE, MHLLM_HUD=0'
