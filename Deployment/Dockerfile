# syntax=docker/dockerfile:1.27.1@sha256:4edf897a3ffa55b89f906fc8cc78afdb3f1834cc9c7083565e611a8a7d5fe99e
#
# Multi-stage build for the containerized MetaHumanLocalLLM Pixel Streaming agent.
# Stage 1 packages the project for Linux on Epic's development image; stage 2 copies the
# packaged build into Epic's Pixel Streaming runtime image (ue4-runtime + NVENC).
#
# Prerequisites (see Deployment/README.md):
#   - Access to ghcr.io/epicgames/unreal-engine images (link your GitHub account to Epic and
#     accept the UE EULA, then `docker login ghcr.io`).
#   - The NVIDIA Container Toolkit on the deploy host (GPU is required at runtime).
#   - THE AUTHORED SET in Content/. This file does not author the diorama, and the map committed
#     to git is a 23 KB stub; the build refuses to cook it (CHECK_MAP below). Author first:
#     Docs/BUILD-AND-DEPLOY.md section 2.2.
#
# Build context is the PROJECT ROOT (see docker-compose.yml `context: ..`), so the whole
# project (.uproject, Source/, Content/, Config/) is available to the packager.
#
# BASE IMAGES ARE PINNED BY DIGEST, so rebuilding the same commit starts from the same bytes.
# The tag stays beside each digest for the reader; Docker pulls the digest and ignores the tag.
# To move a pin, look the new digest up (`docker buildx imagetools inspect <image>:<tag>` or the
# registry API) and replace it everywhere the image appears: the engine digests are shared with
# Dockerfile.amd, Dockerfile.editorpreview and core2/pin-engine-image.sh, and CI fails if they
# disagree. Docs/BUILD-AND-DEPLOY.md section 7 lists every pin.

ARG RELEASE=5.8

# ---- Engine images: one digest-pinned stage per supported RELEASE ------------------------
# RELEASE is still the build-arg compose passes (UE_RELEASE), but it now selects a pinned stage
# instead of the floating dev-<release> tag. BuildKit (the default builder since Docker 23) only
# pulls the stage `engine-${RELEASE}` names, so the unused engine is never downloaded. A RELEASE
# with no stage here fails to resolve rather than building on whatever the tag points at today.
FROM ghcr.io/epicgames/unreal-engine:dev-5.8.4@sha256:675755b3b06fc3bf450a1eafe52a75f0d043e8b060932a3e1e703f6cf91b7810 AS engine-5.8
FROM ghcr.io/epicgames/unreal-engine:dev-5.5.4@sha256:3f7b292cda7f6066aeaea46fa95a520a0d26811810e0d082cfbf5dc85018bd82 AS engine-5.5

FROM engine-${RELEASE} AS builder

# Bring in the project source (a repo-root .dockerignore keeps Saved/Intermediate/DDC out).
COPY --chown=ue4:ue4 . /home/ue4/project-src

# ---- Refuse to cook the stub map -----------------------------------------------------------
# Nothing in this file authors the set. It cooks whatever Content/ the build context carries, and
# the CompanionStage.umap committed to git is a 23 KB STUB: sky and ground, no machine hall, no
# props, no resident. So a clean checkout built an image that streamed an empty stage, and nothing
# anywhere failed. The authored Content/ (the map and the imported Diorama assets, plus the
# resident's content when an opt-in resident was authored; the default mannequin is engine
# content) has to be in the context first: Docs/BUILD-AND-DEPLOY.md section 2.2.
#
# The script is byte-for-byte the same in Dockerfile.amd, and CI extracts it and runs it against
# fixtures (.github/workflows/checks.yml), so change both copies together and keep it POSIX sh.
# The 200 KB floor is the one Scripts/BuildNative.ps1 applies to a freshly authored map, and the
# hash is the stub's Git LFS oid (CI fails if the committed stub changes without it).
# --build-arg ALLOW_STUB_MAP=1 cooks the stub anyway: for a toolchain smoke build, never a release.
ARG ALLOW_STUB_MAP=0
WORKDIR /home/ue4/project-src
RUN <<'CHECK_MAP'
set -eu
map=Content/Maps/CompanionStage.umap
stub_sha256=688fb6b8f0f6528404e9088ba389fd876c344044c947a28395ab0aac64b6fc10
min_bytes=204800
if [ -f /tmp/CompanionStage.synthesized ]; then
    echo "CHECK_MAP: WARNING: $map is the minimal fallback stage this build synthesized for a"
    echo "CHECK_MAP: pre-5.6 engine (no set, no resident). Cooking it because the context supplied no map."
    exit 0
fi
problem=""
if [ ! -f "$map" ]; then
    problem="is missing"
elif head -c 100 "$map" | grep -q '^version https://git-lfs'; then
    problem="is a Git LFS pointer, not a map (the checkout was made without git-lfs)"
elif [ "$(sha256sum "$map" | cut -d ' ' -f 1)" = "$stub_sha256" ]; then
    problem="is the 23 KB stub committed to git (sky and ground: no set, no resident)"
elif [ "$(wc -c < "$map")" -lt "$min_bytes" ]; then
    problem="is only $(wc -c < "$map") bytes, which is a stub rather than the authored set"
fi
if [ -z "$problem" ]; then
    echo "CHECK_MAP: OK, $map is $(wc -c < "$map") bytes"
    exit 0
fi
if [ "${ALLOW_STUB_MAP:-0}" = "1" ]; then
    echo "CHECK_MAP: WARNING: $map $problem."
    echo "CHECK_MAP: ALLOW_STUB_MAP=1, so cooking it anyway. This image opens onto an EMPTY STAGE; never ship it."
    exit 0
fi
echo "CHECK_MAP: REFUSING TO COOK: $map $problem." >&2
echo "CHECK_MAP: This Dockerfile does not author the set. Author it, put the authored Content/ in the" >&2
echo "CHECK_MAP: build context, and rebuild: Docs/BUILD-AND-DEPLOY.md section 2.2. (Dockerfile.amd with" >&2
echo "CHECK_MAP: RELEASE < 5.6 instead synthesizes a minimal fallback stage when the context has NO map.)" >&2
echo "CHECK_MAP: --build-arg ALLOW_STUB_MAP=1 builds anyway, for a toolchain smoke test only." >&2
exit 1
CHECK_MAP

# Package for Linux. Pixel Streaming is already enabled in MetaHumanLocalLLM.uproject, so no
# in-Dockerfile plugin patching is needed. Development config keeps iteration fast; switch to
# -clientconfig=Shipping for a production image.
RUN /home/ue4/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh BuildCookRun \
    -project=/home/ue4/project-src/MetaHumanLocalLLM.uproject \
    -clientconfig=Development -utf8output -nodebuginfo -allmaps -noP4 \
    -cook -build -stage -prereqs -pak -archive \
    -archivedirectory=/home/ue4/project-src/dist \
    -platform=Linux

# ---- Runtime image ---------------------------------------------------------------------
# Epic publishes this only as the floating `runtime-pixel-streaming` tag (no versioned
# variant), which is exactly why the digest matters here.
FROM ghcr.io/epicgames/unreal-engine:runtime-pixel-streaming@sha256:dd6927514c845ca6431003dfdee081c1c33e7e3cbfbffaaab79a176fa9934c79

# Headless Vulkan through NVIDIA's container driver needs the EGL/GLVND loader and X11
# compatibility libraries even when no display server is used. Without these, nvidia-smi sees
# the GPU but the Vulkan loader rejects libGLX_nvidia.so.0 with VK_ERROR_INCOMPATIBLE_DRIVER.
USER root
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libegl1 \
        libgl1 \
        libx11-6 \
        libx11-xcb1 \
        libxext6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder --chown=ue4:ue4 /home/ue4/project-src/dist/Linux /home/ue4/project

USER ue4

# The packaged launch script. Runtime flags (Pixel Streaming URL, resolution, env-driven
# CI-hub config) are supplied by docker-compose.yml so one image serves every environment.
ENTRYPOINT ["/home/ue4/project/MetaHumanLocalLLM.sh"]
