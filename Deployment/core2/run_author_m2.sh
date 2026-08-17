#!/bin/bash
# M2 diorama authoring in the content-only project: import 12 props + author the set.
# Copies the WHOLE Content/ tree out (Maps + imported Diorama assets).
#
# STILL dev-5.5 while m2_full.sh / m2_clean.sh now cook on 5.8, and that split is deliberate
# rather than an oversight. The engine ceiling that forces the COOK to 5.8 does not apply here:
# the cook has to open the repo's git-tracked Content, which is package version 1018 against UE
# 5.5.4's ceiling of 1013 (Scripts/BuildNative.ps1's header has the full account). This script
# opens a different tree -- the content-only Author.uproject seeded from /home/ci/nia-content --
# and every one of the 260 packages there is version 1009, comfortably under that ceiling.
# Measured on core-2 by reading the package headers, not assumed.
#
# 5.5 also WRITES at its own version, and the 5.8 cook reads older packages happily, so the
# handoff is in the safe direction. Moving this to dev-5.8 for consistency would cost pulling
# another ~85 GB engine base (core2/README.md) onto a box that serves live work, for no behaviour
# it does not already have. If nia-content is ever re-exported from a newer editor, re-check the
# package version before assuming this still holds.
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
