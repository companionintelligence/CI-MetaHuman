<#
.SYNOPSIS
    Plays the authored CompanionStage map in the game and lets capture_resident.py take frames from
    inside it: real game frames through the player's view, no editor viewport and no SceneCapture.

.DESCRIPTION
    Author the map first (Scripts/BuildNative.ps1 -KeepAuthoredMap, or the authoring step alone), then:

        pwsh Docs/verification-2026-11-13-eliza/capture_resident.ps1 -OutDir <folder>
        pwsh Docs/verification-2026-11-13-eliza/capture_resident.ps1 -OutDir <folder> -Fps 120 -Mode blink

    Fixed game time (-UseFixedTimeStep), synchronous shader compilation and no texture streaming, so
    no frame is taken on a fallback material or a low mip. The stage director's static call framing
    (MHLLM_OPENING_SHOT=0) and no services: the lip-sync strip comes from the runtime's own text
    estimate (SpeakLiteral with no KOKORO_SERVER_URL), and the opening line is held off so the
    script speaks the line itself. -RenderOffScreen needs a GPU RHI; it opens no window.

.PARAMETER ExtraArgs
    Extra command-line arguments for UnrealEditor-Cmd, e.g. a local derived-data cache.
#>
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Worktree = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$Engine = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$Fps = 30,
    [ValidateSet('full', 'stills', 'blink')][string]$Mode = 'full',
    [int]$TimeoutMin = 40,
    [string]$ExtraArgs = ''
)
$ErrorActionPreference = 'Stop'
$uproject = Join-Path (Resolve-Path $Worktree).Path 'MetaHumanLocalLLM.uproject'
$exe = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = (Join-Path $PSScriptRoot 'capture_resident.py').Replace('\', '/')
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$log = Join-Path $OutDir 'game.log'
$done = Join-Path $OutDir 'capture_done.txt'
Remove-Item $done -ErrorAction SilentlyContinue
if ($py -match '\s' -or $uproject -match '\s' -or $log -match '\s') {
    throw 'The project, the script and -OutDir must have no spaces in their paths (they are passed unquoted).'
}
if ((Get-Item (Join-Path (Split-Path $uproject) 'Content\Maps\CompanionStage.umap')).Length -lt 200KB) {
    throw 'Content\Maps\CompanionStage.umap is the committed stub: author the set first.'
}

foreach ($v in @('KOKORO_SERVER_URL', 'KOKORO_ALIGNER_URL', 'PIPER_SERVER_URL', 'OLLAMA_BASE_URL', 'OPENAI_BASE_URL',
                 'WHISPER_SERVER_URL', 'MHLLM_FACE_SELFTEST', 'MHLLM_FACE_PROFILE', 'MHLLM_FACE_GAIN', 'MHLLM_FACE_POSE_CAP',
                 'MHLLM_EMOTION', 'MHLLM_GAZE', 'MHLLM_FREE_LOOK')) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
$env:MHLLM_OPENING_SHOT = '0'
$env:MHLLM_OPENING_DELAY = '100000'
$env:CAPTURE_DIR = $OutDir
$env:CAPTURE_MODE = $Mode

# One string, so -ExecCmds keeps its quotes (an argument array would split "py <file>" in two).
$ueArgs = (@(
    $uproject, '/Game/Maps/CompanionStage', '-game', '-RenderOffScreen', '-ResX=1920', '-ResY=1080', '-ForceRes', '-windowed',
    '-UseFixedTimeStep', "-FPS=$Fps", '-FixedSeed', '-NoTextureStreaming', '-unattended', '-nosplash', '-nosound', '-noP4',
    '-ini:Engine:[DevOptions.Shaders]:bAllowAsynchronousShaderCompiling=False',
    # The same Model Context Protocol override BuildNative.ps1 passes: no server on the fixed port 8000.
    '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
    "-abslog=$log", $ExtraArgs
) -join ' ') + " -ExecCmds=`"py $py`""
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exe -ArgumentList $ueArgs -PassThru -WindowStyle Hidden
while (-not $p.HasExited) {
    if (Test-Path $done) {
        # The script asked the game to quit; allow it a minute, then make sure.
        if (-not $p.WaitForExit(60000)) { Stop-Process -Id $p.Id -Force }
        break
    }
    if ($sw.Elapsed.TotalMinutes -gt $TimeoutMin) { Stop-Process -Id $p.Id -Force; Write-Host "TIMEOUT after $TimeoutMin min"; break }
    Start-Sleep -Seconds 5
}
$p.WaitForExit()
$verdict = if (Test-Path $done) { (Get-Content $done -Raw).Trim() } else { 'missing' }
Write-Host ("game exited {0} after {1:N1} min; capture {2}; manifest {3}" -f $p.ExitCode, $sw.Elapsed.TotalMinutes, $verdict,
    (Join-Path $OutDir 'capture_manifest.json'))
if ($verdict -ne 'ok') { exit 1 }
