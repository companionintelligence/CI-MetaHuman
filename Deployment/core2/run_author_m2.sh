#!/bin/bash
# M2 diorama authoring in the content-only project: import 12 props + author the set.
# Copies the WHOLE Content/ tree out (Maps + imported Diorama assets).
echo "===== AUTHOR M2 $(date -u) ====="
rm -f /home/ci/stage-out/AUTHOR_RESULT.txt /home/ci/stage-out/author_m2.log
rm -rf /home/ci/stage-out/Content
docker run --rm --user root \
  -v /home/ci/authorproj:/proj -v /home/ci/stage-out:/out -v /home/ci/props-glb:/glb:ro -v /home/ci/mh-seo:/mh:ro -v /home/ci/nia-content:/nia:ro \
  --entrypoint bash ghcr.io/epicgames/unreal-engine:dev-5.5 -c '
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
