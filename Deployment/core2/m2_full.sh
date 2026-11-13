#!/bin/bash
# Full M2 v3: author (with UE-side scale normalization) -> cook -> deploy -> capture.
#
# COOKS ON 5.8. This script drove a RELEASE=5.5 cook. Every git-tracked package under Content/ is
# UE5 package version 1018 and UE 5.5.4 reads at most 1013, and package serialisation is
# forward-only, so a 5.5 cook cannot open this project's content: it fails at LOAD, never at
# compile, which is why a compile-only check kept reporting that 5.5 was fine. The full reasoning
# lives in Scripts/BuildNative.ps1's -Engine notes; read that rather than re-deriving it here.
#
# 5.5 stays buildable on purpose: Dockerfile.amd still takes --build-arg RELEASE, and
# Deployment/docker-compose.ue55.yml still runs a 5.5 image. It is just no longer the default, so
# this script no longer layers that override in.
#
# UE_RELEASE is what docker-compose.yml interpolates into its `ci/metahuman-local-llm:` image name,
# so exporting it here is what makes compose start the image this script just built. Before, the
# build produced :5.5-m2 while the ue55 override started :5.5 -- two different tags.
RELEASE=5.8
export UE_RELEASE="${RELEASE}-m2"
# mannequin (default), metahuman or nia -- read by run_author_m2.sh, which explains the choice.
MHLLM_RESIDENT="$(printf '%s' "${MHLLM_RESIDENT:-mannequin}" | tr '[:upper:]' '[:lower:]')"
export MHLLM_RESIDENT

echo "===== M2v3 START $(date -u) ====="
# Non-zero only when it refuses to start; the result file would then be the previous run's.
bash /home/ci/run_author_m2.sh || { echo "AUTHOR_NOT_STARTED"; exit 1; }
echo "--- normalization report ---"
grep -E "^place |placed " /home/ci/stage-out/AUTHOR_RESULT.txt
if ! grep -q SUCCESS /home/ci/stage-out/AUTHOR_RESULT.txt; then echo "AUTHOR_FAIL"; exit 1; fi

# refresh cook tree content with the newly authored assets
rm -rf /home/ci/cook-m2/Content/Diorama /home/ci/cook-m2/Content/Maps /home/ci/cook-m2/Content/Seo /home/ci/cook-m2/Content/CC_ControlRig_Nia /home/ci/cook-m2/Content/Eliza
cp -r /home/ci/stage-out/Content/. /home/ci/cook-m2/Content/
grep -E "^GameDefaultMap" /home/ci/cook-m2/Config/DefaultEngine.ini

cd /home/ci/cook-m2
# RESIDENT: the resident Dockerfile.amd's CHECK_MAP requires the authored map's AUTHOR_RESULT.txt
# (copied in above with Content/) to have placed, and the only one whose content it lets in.
DOCKER_BUILDKIT=1 docker build -f Deployment/Dockerfile.amd --build-arg RELEASE="${RELEASE}" \
  --build-arg RESIDENT="${MHLLM_RESIDENT}" --build-arg ELIZA_VARIANT="${MHLLM_ELIZA_VARIANT:-}" \
  -t "ci/metahuman-local-llm:${UE_RELEASE}" . && echo "BUILD_OK" || { echo "BUILD_FAIL"; exit 1; }
cd /home/ci/ci-metahermit/Deployment
docker compose -f docker-compose.yml -f docker-compose.amd.yml -f docker-compose.ollama.yml up -d project
sleep 25
docker logs --tail 30 deployment-project-1 2>&1 | grep -iE "LoadMap|joined" | tail -3
curl -s -o /dev/null -w "http :8090 = %{http_code}\n" http://localhost:8090/
docker run --rm --network host -v /home/ci/shots:/shots -w /shots \
  mcr.microsoft.com/playwright:v1.49.0-noble bash -lc "npm i -s playwright@1.49.0 >/dev/null 2>&1 && node cap2.mjs" 2>&1 | grep STATS
echo "===== M2v3 DONE $(date -u) ====="
