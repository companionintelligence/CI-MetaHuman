# syntax=docker/dockerfile:1
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

ARG RELEASE=5.8
FROM ghcr.io/epicgames/unreal-engine:dev-${RELEASE} AS builder

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
FROM ghcr.io/epicgames/unreal-engine:runtime-pixel-streaming

COPY --from=builder --chown=ue4:ue4 /home/ue4/project-src/dist/Linux /home/ue4/project

# The packaged launch script. Runtime flags (Pixel Streaming URL, resolution, env-driven
# CI-hub config) are supplied by docker-compose.yml so one image serves every environment.
ENTRYPOINT ["/home/ue4/project/MetaHumanLocalLLM.sh"]
