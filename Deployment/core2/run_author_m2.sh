#!/bin/bash
# M2 diorama authoring in the content-only project: import 11 props + author the set.
# Copies the WHOLE Content/ tree out (Maps + imported Diorama assets).
#
# Author and cook with dev-5.8. This helper used to author with dev-5.5, and that split was safe:
# all 260 packages under /home/ci/nia-content are version 1009, below UE 5.5.4's 1013 ceiling
# (measured on core-2), and 5.8 reads the packages 5.5 writes. The engine images were later pruned
# from core-2, though, so the 5.8 cook has to pull its base regardless. Using 5.8 for both steps
# avoids fetching a second, roughly 85 GB engine base and removes a version split.
echo "===== AUTHOR M2 $(date -u) ====="
docker run --rm --user root \
  -v /home/ci/authorproj:/proj -v /home/ci/stage-out:/out -v /home/ci/props-glb:/glb:ro -v /home/ci/mh-seo:/mh:ro -v /home/ci/nia-content:/nia:ro -v /home/ci/cook-clean/Deployment/audio:/audio:ro \
  --entrypoint bash ghcr.io/epicgames/unreal-engine:dev-5.8 -c '
    rm -rf /out/Content /out/AUTHOR_RESULT.txt /out/author_m2.log
    chmod 777 /out
    rm -rf /proj/Content && mkdir -p /proj/Content
    cp -r /nia/. /proj/Content/ 2>/dev/null
    chown -R ue4:ue4 /proj && chmod 777 /out
    cp /out/author_diorama.py /proj/author_diorama.py && chown ue4:ue4 /proj/author_diorama.py
    su ue4 -c "/home/ue4/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd /proj/Author.uproject -run=pythonscript -script=/proj/author_diorama.py -unattended -nullrhi -stdout -utf8output -NoLogTimes" > /out/author_m2.log 2>&1
    echo "EDITOR_EXIT=$?" >> /out/author_m2.log
    echo "---CONTENT---" >> /out/author_m2.log
    find /proj/Content -name "*.uasset" -o -name "*.umap" | head -40 >> /out/author_m2.log 2>&1
    cp -r /proj/Content /out/ 2>/dev/null && echo CONTENT_COPIED >> /out/author_m2.log || echo NO_CONTENT >> /out/author_m2.log
  '
echo "===== DONE M2 AUTHOR $(date -u) ====="
