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
