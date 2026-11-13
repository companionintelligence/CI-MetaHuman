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
    Engine root. Defaults to UE_5.8, which is the only version that can cook this project.

    This default used to be UE_5.5, on the belief that 5.5 was the version the deployment cooks
    against. The C++ half of that belief is true, and is why it survived so long: the plugin
    genuinely compiles on 5.5, and RunCompatMatrix.ps1 re-proves it on every run. The CONTENT half
    is not. Every git-tracked package under Content/ is UE5 package version 1018, and UE 5.5.4
    reads at most 1013. Package serialisation is forward-only, so a 5.5 cook cannot open this
    project's assets at all -- it fails at LOAD, never at compile, which is precisely why a
    compile-only check kept reporting that 5.5 was fine.

    -Engine UE_5.5 is still accepted, and the 5.8-only plugin strip further down exists for it,
    but it will not produce a working cook while the content is at 1018.

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

.PARAMETER Resident
    Who stands in the diorama: Mannequin (default), MetaHuman or Nia.

    Mannequin places Epic's UE mannequin (SKM_Manny with its idle, from the engine's MoverTests
    plugin). It needs no input outside the engine, and it is the only choice whose licence clears
    this use: content installed with Unreal Engine is Licensed Technology under the Unreal Engine
    EULA, which carries no AI restriction. It is a placeholder with no facial rig, so there is no
    lip-sync or expression.

    MetaHuman places the MetaHuman "Seo" export (see -MetaHumanDir). Nia places the Reallusion
    Character Creator "Nia" content (see -NiaContent) exactly as every build did before this switch
    existed. Both are opt-in because of their licences: both are Fab Standard License content tagged
    "Allows usage with AI: No" ("Seo - Editable MetaHuman Character" by Groomchenko; "Character
    Creator: Nia" by Reallusion Inc), and this product is an AI companion. Epic's MetaHuman terms do
    not clear Seo, because the Unreal Engine EULA does not cover assets bought or downloaded on Fab.
    README.md "Licensing" carries the citations; do not distribute a build with either of them
    without a licence that permits this use.

.PARAMETER MetaHumanDir
    The MetaHuman "Seo" FBX export: a folder holding fbx\SKM_MHC_Seo_BodyMesh.fbx,
    fbx\SKM_MHC_Seo_FaceMesh.fbx and fbx\MHC_Seo_Outfits.fbx. Defaults to $env:MHLLM_MH_DIR, then
    to C:\devel\_mh-convert\out\seo, where Deployment/metahuman/03_export_fbx_from_GUI.py writes it.
    Only read with -Resident MetaHuman. (This used to be hard-wired to Deployment\metahuman, which
    holds the conversion SCRIPTS and no FBX, so a native build could never place Seo at all.)

.PARAMETER NiaContent
    The Reallusion Character Creator "Nia" content root (the folder named CC_ControlRig_Nia).
    Defaults to $env:MHLLM_NIA_CONTENT, then to a CharacterCreatorNia project sitting beside
    this one. Only read with -Resident Nia.

.EXAMPLE
    ./Scripts/BuildNative.ps1 -Platform Win64 -Engine "C:\Program Files\Epic Games\UE_5.8"

.EXAMPLE
    ./Scripts/BuildNative.ps1 -Resident MetaHuman -MetaHumanDir "D:\mh-export\seo"

.EXAMPLE
    ./Scripts/BuildNative.ps1 -Resident Nia -NiaContent "D:\CharacterCreatorNia\Content\CC_ControlRig_Nia"
#>
param(
    [ValidateSet('Win64', 'Linux', 'Mac')]
    [string]$Platform = 'Win64',
    [string]$Engine = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Config = 'Development',
    [string]$Archive = '',
    [switch]$SkipAuthoring,
    [switch]$KeepAuthoredMap,
    [string]$GlbDir = '',
    [ValidateSet('Mannequin', 'MetaHuman', 'Nia')]
    [string]$Resident = 'Mannequin',
    [string]$MetaHumanDir = '',
    [string]$NiaContent = ''
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'MetaHumanLocalLLM.uproject'
if (-not (Test-Path $UProject)) { throw "Project not found: $UProject" }
if ($Archive -eq '') { $Archive = Join-Path $ProjectRoot "dist/$Platform" }
$StageMap = Join-Path $ProjectRoot 'Content\Maps\CompanionStage.umap'
$script:RefusedPropsNote = $null   # set by Invoke-StageAuthoring when NoAI prop slots were left empty

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
# and the resident herself) has only ever existed as the OUTPUT of Deployment/author_diorama.py, which
# ran exclusively on core-2 inside a Linux container. Nothing on this path ever invoked it, so a
# native build cooked the stub, and the shipped game logged "No resident found; face driver
# inactive" / "Tape reels spinning: 0" and opened onto an empty stage.
#
# WHY AUTHOR RATHER THAN COMMIT THE MAP. The set is DEFINED by ~960 lines of layout, lighting and
# material logic. Committing the .umap would freeze a build artifact as source and guarantee it
# drifts from the script the moment anyone edits the set — the exact drift trap HANDOFF.md
# Lesson 2 describes. It is also the resident's content behind it (~900 MB when that is Nia), and a .umap is
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
    $mh  = if ($MetaHumanDir) { $MetaHumanDir } elseif ($env:MHLLM_MH_DIR) { $env:MHLLM_MH_DIR }
           else { 'C:\devel\_mh-convert\out\seo' }

    if (-not (Test-Path $glb)) {
        throw "Prop GLBs not found: $glb. Pass -GlbDir <path> or set MHLLM_GLB_DIR."
    }

    # A checkout that once built with -Resident Nia keeps her staged under Content\ (the folder is
    # gitignored, not deleted). It is not referenced by a Mannequin or MetaHuman map and not cooked --
    # DefaultGame.ini cooks only /Game/Maps/CompanionStage and what it references -- but say so, so
    # nobody mistakes its presence for her being in the build.
    if ($Resident -ne 'Nia' -and (Test-Path (Join-Path $ProjectRoot 'Content\CC_ControlRig_Nia'))) {
        Write-Host ('Note: Content\CC_ControlRig_Nia is still staged from an earlier -Resident Nia build. ' +
                    "The $Resident map does not reference it, so it is not cooked. Delete it to be sure.") -ForegroundColor Yellow
    }

    if ($Resident -eq 'Mannequin') {
        # Engine content, so the only thing to check is that this engine still ships it. The authoring
        # gate below would catch a miss too, but only after several minutes in the editor.
        $manny = Join-Path $Engine 'Engine\Plugins\Experimental\MoverTests\Content\Characters\Mannequins\Meshes\SKM_Manny.uasset'
        if (-not (Test-Path $manny)) {
            throw ("Default resident not found: $manny. The Mannequin resident is SKM_Manny from the " +
                   "engine's MoverTests plugin; pass -Engine <root> for an engine that has it.")
        }
        Write-Host ('Resident: Mannequin (default). A placeholder with no facial rig: no lip-sync or ' +
                    'expression until a resident licensed for AI use replaces it (README.md "Licensing").') -ForegroundColor Cyan
    }
    elseif ($Resident -eq 'MetaHuman') {
        # All three parts, checked up front. author_diorama.py would otherwise skip a missing FBX,
        # place what it found, and the gate below would stop the build several minutes later with a
        # less specific message. The outfit is not optional either: MetaHuman removes the body
        # geometry under clothing, so Body+Face alone render a figure with no torso.
        $missing = @('SKM_MHC_Seo_BodyMesh.fbx', 'SKM_MHC_Seo_FaceMesh.fbx', 'MHC_Seo_Outfits.fbx') |
            Where-Object { -not (Test-Path (Join-Path $mh "fbx\$_")) }
        if ($missing) {
            throw ("MetaHuman resident export not found in $mh (missing fbx\$($missing -join ', fbx\')). " +
                   'Pass -MetaHumanDir <path> or set MHLLM_MH_DIR. core-2 authors from /home/ci/mh-seo; ' +
                   'copy that folder if it is intact. To produce it instead: import ' +
                   'mhc_seo.mhpkg in the 5.8 editor (GUI), assemble MHC_Seo, then run ' +
                   'Deployment/metahuman/03_export_fbx_from_GUI.py from Tools > Execute Python ' +
                   '(headless export deadlocks; see Docs/HANDOFF-METAHUMAN-SKIN.md). ' +
                   'Or build the default resident by leaving out -Resident.')
        }
        Write-Host ('Resident: MetaHuman "Seo" (opt-in). Her Fab listing ("Seo - Editable MetaHuman Character") ' +
                    'is tagged "Allows usage with AI: No"; see README.md "Licensing" before distributing this build.') -ForegroundColor Yellow
    }
    else {
        $nia = if ($NiaContent) { $NiaContent } elseif ($env:MHLLM_NIA_CONTENT) { $env:MHLLM_NIA_CONTENT }
               else { Join-Path (Split-Path -Parent $ProjectRoot) 'CharacterCreatorNia\Content\CC_ControlRig_Nia' }
        if (-not (Test-Path (Join-Path $nia 'RLContent\Nia\SK_Nia.uasset'))) {
            throw ("Nia content not found: $nia (expected RLContent\Nia\SK_Nia.uasset). " +
                   'Pass -NiaContent <path> or set MHLLM_NIA_CONTENT.')
        }
        Write-Host ('Resident: Nia (opt-in). Her Fab listing is tagged "Allows usage with AI: No"; ' +
                    'see README.md "Licensing" before distributing this build.') -ForegroundColor Yellow

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
    $env:MHLLM_MH_DIR = $mh
    # Lower-case is the spelling author_diorama.py documents; it lower-cases anyway.
    $env:MHLLM_RESIDENT = $Resident.ToLowerInvariant()
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
    $lines | Where-Object { $_ -match 'resident|Nia:|729|placed|REFUSED|NoAI|SUCCESS|FAIL' } | ForEach-Object { "  $_" }

    # FAIL FAST, loudly, on every condition that has previously shipped an empty stage.
    if (-not ($lines -match '^SUCCESS')) { throw "Authoring did not report SUCCESS. See $log and $result" }
    # Gate on the resident that was ASKED for. "resident: NIA placed" is the same gate Nia builds
    # always had; the MetaHuman line is printed only when all three parts imported, the face is
    # tagged and the body and outfit are attached to it (author_diorama.py prints INCOMPLETE
    # otherwise), so a partial figure cannot pass for a resident. The mannequin line is printed only
    # by the default path, never by the last-resort fall-back after a failed Seo import.
    if ($Resident -eq 'Nia') {
        if (-not ($lines -match 'resident: NIA placed')) { throw "Authoring placed no resident (Nia requested). See $result" }
    }
    elseif ($Resident -eq 'MetaHuman') {
        if (-not ($lines -match 'resident: METAHUMAN placed')) { throw "Authoring placed no complete MetaHuman resident. See $result" }
    }
    elseif (-not ($lines -match 'resident: MANNEQUIN placed')) {
        throw "Authoring placed no mannequin resident (the default). See $result"
    }
    # Not a failure: a slot whose GLB is still the NoAI-tagged Sketchfab download is left EMPTY rather
    # than shipped (author_diorama.py, CI_AUTHORED_ONLY). Said here and again after the cook, where
    # it is seen.
    $refused = @($lines | Where-Object { $_ -match '^prop REFUSED' })
    if ($refused.Count -gt 0) {
        $script:RefusedPropsNote = ("$($refused.Count) prop slot(s) left empty: their GLBs in $glb are the NoAI-tagged " +
            'Sketchfab models, not the CI-authored replacements. Build those with ' +
            "'blender --background --python Deployment/blender/make_period_props.py -- --out $glb'.")
        Write-Host $script:RefusedPropsNote -ForegroundColor Yellow
    }
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
    # Exposure is PINNED in the PostProcessVolume, and that is the difference between a lit room
    # and the white haze this set shipped for weeks (mean luminance 221/255, 90% of pixels over
    # 200). It failed silently: AutoExposureMinBrightness/MaxBrightness are read as EV100 in this
    # project, so the old 0.03/8.0 looked like generous bounds and were in fact a hard 3.5-stop
    # overexposure. Nothing in the log said so. This line makes the map assert it.
    if (-not ($lines -match 'exposure PINNED at EV100')) {
        throw "Authoring did not pin the exposure (the room will render blown out). See $result"
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
    $uatStarted = Get-Date
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

# The licence and attributions travel with the package. MetaHumanLocalLLM.Build.cs stages LICENSE,
# NOTICE.md and CREDITS.md as loose files beside the paks, and the cooked set includes eight CC BY 4.0
# models whose attribution has to go wherever the build goes (NOTICE.md). Checked against the NonUFS
# manifest THIS run wrote, so a copy left in the archive by an earlier build cannot pass for one this
# build staged. UAT archives into $Archive itself when the path names the platform (dist/Win64 does)
# and into $Archive\<platform> otherwise, hence the one level of recursion.
$legal = @('LICENSE', 'NOTICE.md', 'CREDITS.md')
$manifest = Get-ChildItem -Path $Archive -Filter 'Manifest_NonUFSFiles_*.txt' -File -Recurse -Depth 1 -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $uatStarted } | Select-Object -First 1
$missingLegal = if (-not $manifest) { $legal } else {
    $staged = @(Get-Content $manifest.FullName | ForEach-Object { ($_ -split "`t")[0] })
    @($legal | Where-Object { ($staged -notcontains "MetaHumanLocalLLM/$_") -or
                              -not (Test-Path (Join-Path $manifest.DirectoryName "MetaHumanLocalLLM\$_")) })
}
if ($missingLegal) {
    throw ("The package in $Archive came out without MetaHumanLocalLLM\$($missingLegal -join ', MetaHumanLocalLLM\'), " +
           'so it would ship without its licence and attributions. Source/MetaHumanLocalLLM/MetaHumanLocalLLM.Build.cs stages them.')
}
Write-Host "Licence and attributions staged: $(Join-Path $manifest.DirectoryName 'MetaHumanLocalLLM') ($($legal -join ', '))" -ForegroundColor Cyan

Write-Host "OK -> $Archive (resident: $Resident)" -ForegroundColor Green
if ($script:RefusedPropsNote) { Write-Host $script:RefusedPropsNote -ForegroundColor Yellow }
Write-Host 'Runtime config is environment variables, same as the container:' -ForegroundColor Cyan
Write-Host '  OLLAMA_BASE_URL, OLLAMA_MODEL, KOKORO_SERVER_URL, MHLLM_OPENING_LINE, MHLLM_HUD=0'
