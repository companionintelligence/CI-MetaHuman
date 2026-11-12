#!/bin/bash
# M2 diorama authoring in the content-only project: import 11 props + author the set.
# Copies the WHOLE Content/ tree out (Maps + imported Diorama assets).
#
# Author and cook with dev-5.8. This helper used to author with dev-5.5, and that split was safe:
# all 260 packages under /home/ci/nia-content are version 1009, below UE 5.5.4's 1013 ceiling
# (measured on core-2), and 5.8 reads the packages 5.5 writes. The engine images were later pruned
# from core-2, though, so the 5.8 cook has to pull its base regardless. Using 5.8 for both steps
# avoids fetching a second, roughly 85 GB engine base and removes a version split.
#
# WHICH RESIDENT: MHLLM_RESIDENT=metahuman (the default) or nia.
#   metahuman  the MetaHuman "Seo" FBX export mounted read-only at /mh (/home/ci/mh-seo). Nia's
#              content is neither mounted nor copied, so it cannot reach the cook tree or the image.
#   nia        additionally mounts /home/ci/nia-content and copies it into the project, which is
#              what every run did before this switch existed. Opt-in because her Fab listing is
#              tagged "Allows usage with AI: No" and this product is an AI companion -- see the
#              README's "Licensing" section.
# The value is passed through to author_diorama.py, which refuses anything else.
RESIDENT="$(printf '%s' "${MHLLM_RESIDENT:-metahuman}" | tr '[:upper:]' '[:lower:]')"
case "$RESIDENT" in
  metahuman|nia) ;;
  *) echo "MHLLM_RESIDENT must be 'metahuman' or 'nia' (got '$RESIDENT')"; exit 2 ;;
esac
NIA_MOUNT=()
if [ "$RESIDENT" = nia ]; then
  NIA_MOUNT=(-v /home/ci/nia-content:/nia:ro)
fi

echo "===== AUTHOR M2 $(date -u) resident=${RESIDENT} ====="
docker run --rm --user root -e MHLLM_RESIDENT="$RESIDENT" \
  -v /home/ci/authorproj:/proj -v /home/ci/stage-out:/out -v /home/ci/props-glb:/glb:ro -v /home/ci/mh-seo:/mh:ro "${NIA_MOUNT[@]}" -v /home/ci/cook-clean/Deployment/audio:/audio:ro -v /home/ci/cook-clean/Deployment/generated:/brand:ro \
  --entrypoint bash ghcr.io/epicgames/unreal-engine:dev-5.8 -c '
    rm -rf /out/Content /out/AUTHOR_RESULT.txt /out/author_m2.log
    chmod 777 /out
    rm -rf /proj/Content && mkdir -p /proj/Content
    # Only present when MHLLM_RESIDENT=nia mounted it.
    if [ -d /nia ]; then cp -r /nia/. /proj/Content/ 2>/dev/null; fi
    chown -R ue4:ue4 /proj && chmod 777 /out
    cp /out/author_diorama.py /proj/author_diorama.py && chown ue4:ue4 /proj/author_diorama.py
    MHLLM_BRAND_DIR=/brand su ue4 -c "MHLLM_RESIDENT=$MHLLM_RESIDENT MHLLM_BRAND_DIR=/brand /home/ue4/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd /proj/Author.uproject -run=pythonscript -script=/proj/author_diorama.py -unattended -nullrhi -stdout -utf8output -NoLogTimes" > /out/author_m2.log 2>&1
    echo "EDITOR_EXIT=$?" >> /out/author_m2.log
    echo "---CONTENT---" >> /out/author_m2.log
    find /proj/Content -name "*.uasset" -o -name "*.umap" | head -40 >> /out/author_m2.log 2>&1
    cp -r /proj/Content /out/ 2>/dev/null && echo CONTENT_COPIED >> /out/author_m2.log || echo NO_CONTENT >> /out/author_m2.log
  '
echo "===== DONE M2 AUTHOR $(date -u) ====="
