#!/bin/bash
# Cook + deploy from the CLEAN tree (exact copy of committed source), NOT the drifted cook-m2.
#
# File ops run as ROOT INSIDE A CONTAINER: the authored content is written by the UE container as
# root, so a host-side `rm -rf`/`cp` as user ci silently half-fails on permissions — which is
# exactly how an earlier run ended up cooking with NO map, letting Dockerfile.amd regenerate a
# bare fallback stage and produce a build with no resident in it.
#
# COOKS ON 5.8. This script drove a RELEASE=5.5 cook. Every git-tracked package under Content/ is
# UE5 package version 1018 and UE 5.5.4 reads at most 1013, and package serialisation is
# forward-only, so a 5.5 cook cannot open this project's content: it fails at LOAD, never at
# compile, which is why a compile-only check kept reporting that 5.5 was fine. The full reasoning
# lives in Scripts/BuildNative.ps1's -Engine notes. 5.5 stays buildable on purpose -- Dockerfile.amd
# still takes --build-arg RELEASE and Deployment/docker-compose.ue55.yml still runs a 5.5 image --
# it is just not the default any more.
set -o pipefail

RELEASE=5.8
IMAGE="ci/metahuman-local-llm:${RELEASE}-face"
# Which resident to author: mannequin (default), metahuman or nia. Exported so run_author_m2.sh sees
# it; see that script for what each means and why Seo and Nia are opt-in. Lower-cased HERE as well
# as there, so the gates below compare the same spelling the authoring run was given.
MHLLM_RESIDENT="$(printf '%s' "${MHLLM_RESIDENT:-mannequin}" | tr '[:upper:]' '[:lower:]')"
export MHLLM_RESIDENT

echo "===== FACE COOK START $(date -u) ====="

# 1. Refresh the source tree from the committed tarball (Dockerfile fixes included).
docker run --rm --user root -v /home/ci:/host --entrypoint bash ubuntu:26.04 -c '
  rm -rf /host/cook-clean/Source /host/cook-clean/Plugins /host/cook-clean/Config \
         /host/cook-clean/Scripts /host/cook-clean/Deployment /host/cook-clean/MetaHumanLocalLLM.uproject
  mkdir -p /host/cook-clean && (tar xzf /tmp/../host/../tmp/proj-src.tgz -C /host/cook-clean 2>/dev/null || \
  tar xzf /host/proj-src.tgz -C /host/cook-clean) && \
  find /host/cook-clean -type f -name ._\* -delete
  find /host/cook-clean -type f -name .DS_Store -delete'
grep -q "SUPPLIED CompanionStage" /home/ci/cook-clean/Deployment/Dockerfile.amd \
  && echo "DOCKERFILE_FIX_PRESENT (will not regenerate a supplied map)" \
  || { echo "DOCKERFILE_STALE — aborting, it would delete the diorama"; exit 1; }

# 2. Author the map. run_author_m2.sh exits non-zero only when it refuses to start (an unknown
# MHLLM_RESIDENT), and then the AUTHOR_RESULT.txt below is the PREVIOUS run's -- stop here rather
# than gate on, and cook, a stale result.
bash /home/ci/run_author_m2.sh || { echo "AUTHOR_NOT_STARTED"; exit 1; }
grep -q SUCCESS /home/ci/stage-out/AUTHOR_RESULT.txt || { echo AUTHOR_FAIL; exit 1; }
grep -E "resident|Nia:|tag" /home/ci/stage-out/AUTHOR_RESULT.txt
# Gate on the resident that was ASKED for, the same lines Scripts/BuildNative.ps1 gates on. The
# MetaHuman line is printed only when all three Seo parts imported, the face is tagged and the body
# and outfit are attached to it; anything less prints "METAHUMAN INCOMPLETE" instead. The mannequin
# line is printed only by the default path, never by the fall-back after a failed Seo import.
case "$MHLLM_RESIDENT" in
  nia)       GATE="resident: NIA placed" ;;
  metahuman) GATE="resident: METAHUMAN placed" ;;
  *)         GATE="resident: MANNEQUIN placed" ;;
esac
grep -q "$GATE" /home/ci/stage-out/AUTHOR_RESULT.txt || { echo "NO_RESIDENT (${MHLLM_RESIDENT} requested)"; exit 1; }

# 3. Move authored content into the cook tree, as root (see header).
docker run --rm --user root -v /home/ci:/host --entrypoint bash ubuntu:26.04 -c '
  rm -rf /host/cook-clean/Content
  cp -r /host/stage-out/Content /host/cook-clean/Content
  chmod -R a+rX /host/cook-clean/Content'

# 4. FAIL FAST. A missing map here is what silently produced a residentless build last time.
test -f /home/ci/cook-clean/Content/Maps/CompanionStage.umap || { echo "NO_MAP — aborting before the cook"; exit 1; }
echo "MAP_PRESENT $(stat -c%s /home/ci/cook-clean/Content/Maps/CompanionStage.umap) bytes"
if [ "$MHLLM_RESIDENT" = nia ]; then
  test -d /home/ci/cook-clean/Content/CC_ControlRig_Nia || { echo "NO_NIA_CONTENT — aborting"; exit 1; }
  echo "NIA_CONTENT_PRESENT"
else
  if [ "$MHLLM_RESIDENT" = metahuman ]; then
    test -d /home/ci/cook-clean/Content/Seo || { echo "NO_METAHUMAN_CONTENT — aborting"; exit 1; }
    echo "METAHUMAN_CONTENT_PRESENT"
  else
    # The mannequin is engine plugin content, so there is nothing under Content/ to require -- only
    # things to refuse. A mannequin build authors nothing under /Game/Seo, so Seo here is stale too.
    if [ -d /home/ci/cook-clean/Content/Seo ]; then
      echo "UNEXPECTED_SEO_CONTENT in the cook tree for a mannequin build — aborting"; exit 1
    fi
    echo "MANNEQUIN_BUILD (no resident content under Content/)"
  fi
  # The opposite check matters as much: run_author_m2.sh no longer copies Nia in, so finding her here
  # means a stale tree, and a stale tree is how she would ride into an image nobody asked her into.
  if [ -d /home/ci/cook-clean/Content/CC_ControlRig_Nia ]; then
    echo "UNEXPECTED_NIA_CONTENT in the cook tree for a ${MHLLM_RESIDENT} build — aborting"; exit 1
  fi
fi

cd /home/ci/cook-clean
DOCKER_BUILDKIT=1 docker build -f Deployment/Dockerfile.amd --build-arg RELEASE="${RELEASE}" \
  -t "${IMAGE}" . && echo BUILD_OK || { echo BUILD_FAIL; exit 1; }
docker build --progress=plain -f Deployment/Dockerfile.amd --build-arg RELEASE="${RELEASE}" -t "${IMAGE}" . 2>&1 | grep -iE "SUPPLIED|synthesizing" | head -2

cp /home/ci/docker-compose.face.yml /home/ci/ci-metahermit/Deployment/docker-compose.face.yml
cd /home/ci/ci-metahermit/Deployment

# docker-compose.face.yml pins the app image by NAME and is not maintained by this script, so the
# tag built above has to be the tag it names. If it is not, compose starts whatever older tag it
# does name and the fresh cook is never deployed -- the same class of silent-wrong-artifact failure
# as the missing map above, so fail the same way: loudly, before deploying.
grep -q "^[[:space:]]*image:.*${IMAGE}" docker-compose.face.yml || {
  echo "COMPOSE_IMAGE_MISMATCH - docker-compose.face.yml does not name ${IMAGE}:"
  grep -n "image:" docker-compose.face.yml
  echo "point its project.image at ${IMAGE} (that file is maintained elsewhere), then re-run"
  exit 1; }

docker compose -f docker-compose.yml -f docker-compose.amd.yml -f docker-compose.ollama.yml \
               -f docker-compose.face.yml up -d project
sleep 40
echo "--- face driver ---"
docker logs deployment-project-1 2>&1 | grep -iE "LogCompanionFace|resident|profile curves|self-test" | tail -8
docker logs deployment-project-1 2>&1 | grep -iE "LoadMap complete|joined" | tail -2
curl -s -o /dev/null -w "http :8090 = %{http_code}\n" http://localhost:8090/
echo "===== FACE COOK DONE $(date -u) ====="
