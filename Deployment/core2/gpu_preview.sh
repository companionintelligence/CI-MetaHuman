#!/bin/bash
# FAST material-preview: GPU-render the UNCOOKED authored CompanionStage from stage-out content,
# via ci/ue55-editor-preview:mesa. Writes /home/ci/stage-out/stage_render.png. No cook.
# - setpriv (NOT su) keeps render/video groups 992/44 -> RADV can open /dev/dri (else SM5 fails).
# - Do NOT touch the packaged radeon_icd.json; loader auto-discovers RADV (no VK_ICD_FILENAMES).
# - HighResShot in -game does NOT self-quit, so poll for the PNG then kill the editor.
# - AutoExposure 0 so the frame isn't blown out (offscreen -game lacks the stream's exposure).
rm -f /home/ci/stage-out/stage_render.png /home/ci/stage-out/gpu_preview.log
timeout 900 docker run --rm --user root \
  --device /dev/dri:/dev/dri --group-add 992 --group-add 44 \
  -e XDG_RUNTIME_DIR=/tmp \
  -v /home/ci/authorproj:/proj -v /home/ci/stage-out:/out \
  --entrypoint bash ci/ue55-editor-preview:mesa -c '
    rm -rf /proj/Content && cp -r /out/Content /proj/Content
    chown -R 1000:1000 /proj && chmod 777 /out /home/ue4
    rm -rf /proj/Saved/Screenshots
    setpriv --reuid 1000 --regid 1000 --groups 992,44 bash -c "HOME=/home/ue4 /home/ue4/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd /proj/Author.uproject /Game/Maps/CompanionStage -game -RenderOffscreen -Vulkan -ResX=1280 -ResY=720 -unattended -nosplash -nosound -stdout -NoLogTimes -ExecCmds=\"r.DefaultFeature.AutoExposure 0, r.EyeAdaptation.ExposureOverride 14, r.RayTracing 0, r.Lumen.HardwareRayTracing 0, r.DynamicGlobalIlluminationMethod 0, r.ReflectionMethod 0, r.SkyLight.RealTimeReflectionCapture 0, r.SkyAtmosphere 0, r.Nanite 0, r.Vulkan.WaitForIdleOnSubmit 2, r.GenerateMeshDistanceFields 0, HighResShot 1280x720\"" > /out/gpu_preview.log 2>&1 &
    UEPID=$!
    SHOT=""
    for i in $(seq 1 85); do
      SHOT=$(find /proj/Saved/Screenshots -name "*.png" 2>/dev/null | head -1)
      [ -n "$SHOT" ] && { sleep 2; break; }   # let the write finish
      kill -0 $UEPID 2>/dev/null || break       # editor died
      sleep 10
    done
    [ -n "$SHOT" ] && cp "$SHOT" /out/stage_render.png && chmod 666 /out/stage_render.png
    kill $UEPID 2>/dev/null; setpriv --reuid 1000 --regid 1000 pkill -9 -f UnrealEditor 2>/dev/null; true
  '
if [ -f /home/ci/stage-out/stage_render.png ]; then ls -la /home/ci/stage-out/stage_render.png; echo RENDER_OK; else echo NO_RENDER; grep -iE "error|fatal|criteria|Vulkan Driver|missing usage|HighResShot|Screenshot" /home/ci/stage-out/gpu_preview.log | tail -12; fi
