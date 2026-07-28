#!/bin/bash
# Full M2 v3: author (with UE-side scale normalization) -> cook -> deploy -> capture.
echo "===== M2v3 START $(date -u) ====="
bash /home/ci/run_author_m2.sh
echo "--- normalization report ---"
grep -E "^place |placed " /home/ci/stage-out/AUTHOR_RESULT.txt
if ! grep -q SUCCESS /home/ci/stage-out/AUTHOR_RESULT.txt; then echo "AUTHOR_FAIL"; exit 1; fi

# refresh cook tree content with the newly authored assets
rm -rf /home/ci/cook-m2/Content/Diorama /home/ci/cook-m2/Content/Maps /home/ci/cook-m2/Content/Seo /home/ci/cook-m2/Content/CC_ControlRig_Nia
cp -r /home/ci/stage-out/Content/. /home/ci/cook-m2/Content/
grep -E "^GameDefaultMap" /home/ci/cook-m2/Config/DefaultEngine.ini

cd /home/ci/cook-m2
DOCKER_BUILDKIT=1 docker build -f Deployment/Dockerfile.amd --build-arg RELEASE=5.5 \
  -t ci/metahuman-local-llm:5.5-m2 . && echo "BUILD_OK" || { echo "BUILD_FAIL"; exit 1; }
cd /home/ci/ci-metahermit/Deployment
docker compose -f docker-compose.yml -f docker-compose.amd.yml -f docker-compose.ollama.yml -f docker-compose.ue55.yml up -d project
sleep 25
docker logs --tail 30 deployment-project-1 2>&1 | grep -iE "LoadMap|joined" | tail -3
curl -s -o /dev/null -w "http :8090 = %{http_code}\n" http://localhost:8090/
docker run --rm --network host -v /home/ci/shots:/shots -w /shots \
  mcr.microsoft.com/playwright:v1.49.0-noble bash -lc "npm i -s playwright@1.49.0 >/dev/null 2>&1 && node cap2.mjs" 2>&1 | grep STATS
echo "===== M2v3 DONE $(date -u) ====="
