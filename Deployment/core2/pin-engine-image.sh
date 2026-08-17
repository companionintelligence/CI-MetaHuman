#!/bin/bash
# Protect the 85 GB Unreal engine image from `docker system prune --all`.
#
# THE PROBLEM THIS SOLVES, which has now cost this project at least six rebuilds.
# `docker system prune --volumes --all --force` appears five times in core-2's shell history and
# runs as part of unrelated CI housekeeping on that box. Each occurrence destroys
# ghcr.io/epicgames/unreal-engine:dev-5.5 (85 GB), the cooked application image, and the Epic
# signalling image. Re-pulling the engine alone is ~40 minutes on a good link, and nothing can
# author, cook or deploy until it lands. REPO-STATE.md has logged this as a standing risk since
# 2026-07-31; the previously documented mitigation was a ~17 GB tarball archive, which was itself
# deleted at some point before 2026-08-06 because nothing kept it alive either.
#
# WHY A CONTAINER RATHER THAN AN ARCHIVE.
# `docker system prune --all` removes every image "not used by at least one container". It does NOT
# remove RUNNING containers, and it will not remove an image a running container is built from. So
# one idle container is a hard interlock: while it is up, the engine image cannot be pruned by any
# invocation of prune, no matter who runs it or with which flags short of an explicit `docker rmi -f`.
#
# An archive only helps if someone remembers to restore it, and costs a 40 GB write plus a restore
# step. This costs a few MB of RAM for a sleeping process and no operator memory at all.
#
# --restart=always is what makes it survive a reboot, which is how the last set of services on
# beta-1 came to be down for ten hours without anyone noticing.
set -euo pipefail

IMAGE="${1:-ghcr.io/epicgames/unreal-engine:dev-5.5}"
NAME="ue-image-pin-$(echo "$IMAGE" | tr '/:.' '---')"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "Image not present locally: $IMAGE"
  echo "Pull it first:  docker pull $IMAGE"
  exit 1
fi

if docker ps --format '{{.Names}}' | grep -qx "$NAME"; then
  echo "Already pinned: $NAME"
  exit 0
fi

docker rm -f "$NAME" >/dev/null 2>&1 || true

# `sleep infinity` with no entrypoint override would run the engine image's own entrypoint, which
# expects a project. --entrypoint is what keeps this a genuinely idle process.
docker run -d --name "$NAME" --restart=always --entrypoint sleep "$IMAGE" infinity >/dev/null

echo "Pinned $IMAGE via container $NAME"
docker ps --filter "name=$NAME" --format '  {{.Names}}  {{.Status}}  {{.Image}}'
echo
echo "Verify the interlock holds:"
echo "  docker system prune --all --force && docker images | grep unreal-engine"
