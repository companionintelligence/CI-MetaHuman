#!/bin/bash
# M2 diorama authoring in the content-only project: author the set from stage_layout.json.
# Copies the WHOLE Content/ tree out (Maps + imported Diorama assets).
#
# Author and cook with dev-5.8. This helper used to author with dev-5.5, and that split was safe:
# all 260 packages under /home/ci/nia-content are version 1009, below UE 5.5.4's 1013 ceiling
# (measured on core-2), and 5.8 reads the packages 5.5 writes. The engine images were later pruned
# from core-2, though, so the 5.8 cook has to pull its base regardless. Using 5.8 for both steps
# avoids fetching a second, roughly 85 GB engine base and removes a version split.
#
# WHICH RESIDENT: MHLLM_RESIDENT=mannequin (the default), eliza, metahuman or nia.
#   mannequin  Epic's UE mannequin from the engine's MoverTests plugin. Nothing is mounted for it:
#              neither the Seo export nor Nia's content reaches the container, the cook tree or the
#              image. The only resident whose licence clears an AI companion; a placeholder with no
#              facial rig.
#   eliza      the staged Eliza content (/home/ci/eliza-content/Eliza, holding eliza_manifest.json)
#              mounted read-only at /eliza and copied into Content/Eliza before authoring. Private,
#              never committed; author_diorama.py reads the variant (mpfb or metahuman) from the
#              manifest and MHLLM_ELIZA_VARIANT, when set, must match it.
#   metahuman  the MetaHuman "Seo" FBX export mounted read-only at /mh (/home/ci/mh-seo).
#   nia        additionally mounts /home/ci/nia-content and copies it into the project, which is
#              what every run did before this switch existed (/mh too, for her fall-back to Seo).
#   Seo and Nia are opt-in because both Fab listings are tagged "Allows usage with AI: No" and this
#   product is an AI companion -- see the README's "Licensing" section.
# The value is passed through to author_diorama.py, which refuses anything else.
RESIDENT="$(printf '%s' "${MHLLM_RESIDENT:-mannequin}" | tr '[:upper:]' '[:lower:]')"
case "$RESIDENT" in
  mannequin|eliza|metahuman|nia) ;;
  *) echo "MHLLM_RESIDENT must be 'mannequin', 'eliza', 'metahuman' or 'nia' (got '$RESIDENT')"; exit 2 ;;
esac
RESIDENT_MOUNTS=()
if [ "$RESIDENT" = metahuman ] || [ "$RESIDENT" = nia ]; then
  RESIDENT_MOUNTS+=(-v /home/ci/mh-seo:/mh:ro)
fi
if [ "$RESIDENT" = eliza ]; then
  test -f /home/ci/eliza-content/Eliza/eliza_manifest.json \
    || { echo "MHLLM_RESIDENT=eliza, but /home/ci/eliza-content/Eliza/eliza_manifest.json is missing"; exit 2; }
  RESIDENT_MOUNTS+=(-v /home/ci/eliza-content:/eliza:ro)
fi
if [ "$RESIDENT" = nia ]; then
  RESIDENT_MOUNTS+=(-v /home/ci/nia-content:/nia:ro)
fi

echo "===== AUTHOR M2 $(date -u) resident=${RESIDENT} ====="
docker run --rm --user root -e MHLLM_RESIDENT="$RESIDENT" -e MHLLM_ELIZA_VARIANT="${MHLLM_ELIZA_VARIANT:-}" \
  -v /home/ci/authorproj:/proj -v /home/ci/stage-out:/out -v /home/ci/props-glb:/glb:ro "${RESIDENT_MOUNTS[@]}" -v /home/ci/cook-clean/Deployment/audio:/audio:ro -v /home/ci/cook-clean/Deployment/generated:/brand:ro \
  --entrypoint bash ghcr.io/epicgames/unreal-engine:dev-5.8 -c '
    rm -rf /out/Content /out/AUTHOR_RESULT.txt /out/author_m2.log
    chmod 777 /out
    rm -rf /proj/Content && mkdir -p /proj/Content
    # Only present when MHLLM_RESIDENT=nia mounted it.
    if [ -d /nia ]; then cp -r /nia/. /proj/Content/ 2>/dev/null; fi
    # Only present when MHLLM_RESIDENT=eliza mounted it.
    if [ -d /eliza/Eliza ]; then cp -r /eliza/Eliza /proj/Content/Eliza; fi
    chown -R ue4:ue4 /proj && chmod 777 /out
    cp /out/author_diorama.py /proj/author_diorama.py && chown ue4:ue4 /proj/author_diorama.py
    # The layout the script authors from; it is read from beside the script.
    cp /out/stage_layout.json /proj/stage_layout.json && chown ue4:ue4 /proj/stage_layout.json
    if [ -f /out/stage_layout.py ]; then cp /out/stage_layout.py /proj/stage_layout.py && chown ue4:ue4 /proj/stage_layout.py; fi
    MHLLM_BRAND_DIR=/brand su ue4 -c "MHLLM_RESIDENT=$MHLLM_RESIDENT MHLLM_ELIZA_VARIANT=$MHLLM_ELIZA_VARIANT MHLLM_BRAND_DIR=/brand /home/ue4/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd /proj/Author.uproject -run=pythonscript -script=/proj/author_diorama.py -unattended -nullrhi -stdout -utf8output -NoLogTimes" > /out/author_m2.log 2>&1
    echo "EDITOR_EXIT=$?" >> /out/author_m2.log
    echo "---CONTENT---" >> /out/author_m2.log
    find /proj/Content -name "*.uasset" -o -name "*.umap" | head -40 >> /out/author_m2.log 2>&1
    cp -r /proj/Content /out/ 2>/dev/null && echo CONTENT_COPIED >> /out/author_m2.log || echo NO_CONTENT >> /out/author_m2.log
  '
echo "===== DONE M2 AUTHOR $(date -u) ====="
