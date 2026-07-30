#!/bin/bash
# Stage repo sources to core-2 for a cook, and PROVE they landed.
#
# Exists because the same failure has now happened three times: a fix is written, staged to a path
# nothing reads, and a 25-minute cook silently builds the previous revision. The tarball has ONE
# canonical name -- the one m2_clean.sh extracts -- and this script verifies named symbols are
# present in the cook tree before any cook is allowed to start.
set -euo pipefail
REPO="${1:?usage: stage.sh <repo-root> [host]}"
HOST="${2:-ci@core-2}"

cd "$REPO"
tar czf /tmp/proj-src.tgz \
  --exclude='*/Binaries/*' --exclude='*/Intermediate/*' \
  --exclude='*/Saved/*' --exclude='*/DerivedDataCache/*' \
  Source Plugins Config Scripts Deployment MetaHumanLocalLLM.uproject

# The canonical name. Do NOT invent a second one: m2_clean.sh reads exactly this.
scp -q /tmp/proj-src.tgz "$HOST:/home/ci/proj-src.tgz"
scp -q Deployment/author_diorama.py "$HOST:/home/ci/stage-out/author_diorama.py"
scp -q Deployment/docker-compose.face.yml "$HOST:/home/ci/docker-compose.face.yml"
scp -q Deployment/core2/cap_burst.mjs "$HOST:/home/ci/shots/cap_burst.mjs" 2>/dev/null || true

# Expand into the cook tree now, so verification inspects what the cook will actually compile
# rather than what the tarball merely contains.
ssh "$HOST" "docker run --rm --user root -v /home/ci:/host --entrypoint sh alpine:latest -c '
  rm -rf /host/cook-clean/Source /host/cook-clean/Plugins /host/cook-clean/Config \
         /host/cook-clean/Scripts /host/cook-clean/Deployment /host/cook-clean/MetaHumanLocalLLM.uproject
  mkdir -p /host/cook-clean && tar xzf /host/proj-src.tgz -C /host/cook-clean'"

echo '--- verifying staged sources ---'
FAIL=0
check_file() {  # path-in-cook-tree, human name
  if ssh "$HOST" "test -f /home/ci/cook-clean/$1"; then echo "  OK   $2"; else echo "  MISS $2"; FAIL=1; fi
}
check_grep() {  # file, pattern, human name
  if ssh "$HOST" "grep -q '$2' /home/ci/$1"; then echo "  OK   $3"; else echo "  MISS $3"; FAIL=1; fi
}
check_file "Plugins/CICompanion/Source/CICompanion/Private/Avatar/CompanionStageDirector.cpp" "stage director"
check_file "Plugins/CICompanion/Source/CICompanion/Private/Avatar/CompanionRuntimeSubsystem.cpp" "companion runtime"
check_file "Plugins/CICompanion/Source/CICompanion/Private/Voice/KokoroTextToSpeech.cpp" "kokoro tts"
check_grep "docker-compose.face.yml" "MHLLM_OPENING_LINE" "compose: opening line"
check_grep "stage-out/author_diorama.py" "CAM_DIST = 160" "author: solved framing"
[ "$FAIL" -eq 0 ] || { echo "STAGE_FAILED — refusing to cook a stale tree"; exit 1; }
echo "STAGE_OK"
