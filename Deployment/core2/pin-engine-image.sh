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
#
# WHICH IMAGE. The default is the digest-pinned 5.8 engine that Deployment/Dockerfile and
# Dockerfile.amd build FROM (their `engine-5.8` stage), so the copy this protects is the one the
# cook actually uses rather than whatever the floating dev-5.x tag pointed at when it was pulled.
# Keep it identical to those Dockerfiles; CI fails if they disagree. Pass another reference as $1
# to protect a different image (e.g. the dev-5.5 stage for a deliberate 5.5 build).
#
# When the pin moves to a new digest, re-running this replaces the old interlock container, which
# leaves the previous engine image unprotected -- prune it deliberately once nothing needs it.
set -euo pipefail

DEFAULT_IMAGE="ghcr.io/epicgames/unreal-engine:dev-5.8.4@sha256:675755b3b06fc3bf450a1eafe52a75f0d043e8b060932a3e1e703f6cf91b7810"
IMAGE="${1:-$DEFAULT_IMAGE}"

# Container names allow only [a-zA-Z0-9_.-]; a digest reference also carries '@' and ':'. Name the
# interlock after repo:tag alone (the digest is checked below instead), with every other character
# mapped to '-' -- which gives the same name the floating-tag version of this script used.
NAME="ue-image-pin-$(printf '%s' "${IMAGE%%@*}" | tr -c 'a-zA-Z0-9_-' '-')"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "Image not present locally: $IMAGE"
  echo "Pull it first:  docker pull $IMAGE"
  exit 1
fi

# "Already pinned" means a RUNNING interlock on THIS image ID. A container of the right name from an
# older digest protects the wrong bytes, so it is replaced rather than reported as fine.
WANT_ID="$(docker image inspect --format '{{.Id}}' "$IMAGE")"
HAVE_ID="$(docker inspect --format '{{.Image}}' "$NAME" 2>/dev/null || true)"
RUNNING="$(docker inspect --format '{{.State.Running}}' "$NAME" 2>/dev/null || true)"
if [ "$HAVE_ID" = "$WANT_ID" ] && [ "$RUNNING" = "true" ]; then
  echo "Already pinned: $NAME ($IMAGE)"
  exit 0
fi
if [ -n "$HAVE_ID" ] && [ "$HAVE_ID" != "$WANT_ID" ]; then
  echo "Replacing $NAME: it was protecting $HAVE_ID, not $WANT_ID"
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
