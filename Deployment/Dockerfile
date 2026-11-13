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
#   - THE AUTHORED SET in Content/, with the AUTHOR_RESULT.txt its authoring run wrote beside the
#     map. This file does not author the diorama, and the map committed to git is a 23 KB stub;
#     the build refuses to cook it, or an authored map without its result (CHECK_MAP below).
#     Author first: Docs/BUILD-AND-DEPLOY.md section 2.2.
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

# ---- Refuse to cook the stub map, or a map nothing vouches for -----------------------------
# Nothing in this file authors the set. It cooks whatever Content/ the build context carries, and
# the CompanionStage.umap committed to git is a 23 KB STUB: sky and ground, no machine hall, no
# props, no resident. So a clean checkout built an image that streamed an empty stage, and nothing
# anywhere failed. The authored Content/ (the map and the imported Diorama assets, plus the
# resident's content when an opt-in resident was authored; the default mannequin is engine
# content) has to be in the context first: Docs/BUILD-AND-DEPLOY.md section 2.2.
#
# The opposite failure is as quiet: an AUTHORED map that is not what this release describes. One
# authored before the resident switch places Reallusion's Nia, one authored before the NoAI prop
# gate carries the three Sketchfab props CREDITS.md says were replaced, and both pass the size and
# stub checks. So an authored map is cooked only beside the Content/AUTHOR_RESULT.txt its
# authoring run wrote (author_diorama.py), and only if that result reports SUCCESS, placed the
# resident this build asks for, ran the NoAI prop gate, and records this map's sha256. Nia's and
# Seo's content (Fab, tagged NoAI) is refused unless asked for by name, as m2_clean.sh does on
# core-2: --build-arg RESIDENT=nia or RESIDENT=metahuman (NOTICE.md says why neither ships).
#
# The script is byte-for-byte the same in Dockerfile.amd, and CI extracts it and runs it against
# fixtures (.github/workflows/checks.yml), so change both copies together and keep it POSIX sh.
# The 200 KB floor is the one Scripts/BuildNative.ps1 applies to a freshly authored map, and the
# hash is the stub's Git LFS oid (CI fails if the committed stub changes without it).
# --build-arg ALLOW_STUB_MAP=1 cooks the stub anyway: for a toolchain smoke build, never a release.
# It never lets through an authored map that nothing vouches for.
ARG ALLOW_STUB_MAP=0
ARG RESIDENT=mannequin
WORKDIR /home/ue4/project-src
RUN <<'CHECK_MAP'
set -eu
map=Content/Maps/CompanionStage.umap
result=Content/AUTHOR_RESULT.txt
stub_sha256=688fb6b8f0f6528404e9088ba389fd876c344044c947a28395ab0aac64b6fc10
min_bytes=204800
resident="$(printf '%s' "${RESIDENT:-mannequin}" | tr '[:upper:]' '[:lower:]')"
refuse() {
    echo "CHECK_MAP: REFUSING TO COOK: $1" >&2
    shift
    for line in "$@"; do echo "CHECK_MAP: $line" >&2; done
    exit 1
}
lines() { tr -d '\r' < "$result"; }
case "$resident" in
    mannequin) placed="resident: MANNEQUIN placed" ;;
    metahuman) placed="resident: METAHUMAN placed" ;;
    nia)       placed="resident: NIA placed" ;;
    *) refuse "RESIDENT=$resident is not one of mannequin (the default), metahuman or nia." ;;
esac
# Seo and Nia are Fab content tagged NoAI (NOTICE.md): cooked only when asked for by name, the same
# rule m2_clean.sh applies to its cook tree. The mannequin is engine content, so nothing under
# Content/ belongs to it; either folder in a mannequin build's context is a stale tree.
if [ -d Content/CC_ControlRig_Nia ] && [ "$resident" != nia ]; then
    refuse "Content/CC_ControlRig_Nia is in the build context of a $resident build." \
        "It is Reallusion's \"Nia\", Fab content tagged NoAI (NOTICE.md), and a stale tree is how she would" \
        "ride into an image nobody asked her into. Remove it, or ask for her: --build-arg RESIDENT=nia."
fi
if [ -d Content/Seo ] && [ "$resident" = mannequin ]; then
    refuse "Content/Seo is in the build context of a mannequin build." \
        "It is the MetaHuman \"Seo\", Fab content tagged NoAI (NOTICE.md), and a mannequin build authors" \
        "nothing under /Game/Seo. Remove it, or ask for her: --build-arg RESIDENT=metahuman."
fi
if [ "$resident" = nia ] && [ ! -d Content/CC_ControlRig_Nia ]; then
    refuse "RESIDENT=nia, but the build context has no Content/CC_ControlRig_Nia."
fi
if [ "$resident" = metahuman ] && [ ! -d Content/Seo ]; then
    refuse "RESIDENT=metahuman, but the build context has no Content/Seo."
fi
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
    # An authored map, so it is cooked only on the word of the run that authored it. Its bytes cannot
    # say who stands in it or which props it placed: a map authored before the resident switch (Nia)
    # or before the NoAI prop gate (three Sketchfab models) passes every check above.
    if [ ! -f "$result" ]; then
        refuse "$map has no $result beside it, so nothing vouches for what it contains." \
            "author_diorama.py writes that file into Content/ with the map; a map authored before it did," \
            "or copied without it, may hold Nia or the NoAI-tagged props. Copy the authored Content/ whole," \
            "or re-author: Docs/BUILD-AND-DEPLOY.md section 2.2."
    fi
    lines | grep -qx 'SUCCESS' || refuse "$result does not report SUCCESS: that authoring run failed."
    lines | grep -q "^$placed" || refuse "$result has no \"$placed\" line (RESIDENT=$resident)." \
        "The map holds another resident, or none: $(lines | grep '^resident: ' | tail -n 1)"
    lines | grep -q '^NoAI prop slots: ' || refuse "$result has no \"NoAI prop slots\" line." \
        "It predates author_diorama.py refusing NoAI-tagged props, so ibm-1401-cpu, ibm-3178-terminal and" \
        "ibm-3350-disk may be the Sketchfab models CREDITS.md says were replaced. Re-author."
    recorded="$(lines | sed -n 's/^map sha256: \([0-9a-f]\{64\}\)$/\1/p' | tail -n 1)"
    actual="$(sha256sum "$map" | cut -d ' ' -f 1)"
    if [ "$recorded" != "$actual" ]; then
        refuse "$result belongs to another map: it records map sha256 ${recorded:-(none)}, and $map is $actual." \
            "Copy the authored Content/ whole, map and result together, or re-author."
    fi
    echo "CHECK_MAP: OK, $map is $(wc -c < "$map") bytes, and $result vouches for it:"
    echo "CHECK_MAP: $placed; $(lines | grep '^NoAI prop slots: ' | tail -n 1)"
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
# in-Dockerfile plugin patching is needed.
#
# SHIPPING, because this image is what viewers reach. A Development build carries the console,
# cheat and stat commands and honours every command-line override; Shipping compiles them out.
# What that changes for operators: -ExecCmds, -ini: and -dpcvars are ignored (so runtime cvars
# have to live in the image's config, as Dockerfile.amd does for its RADV workarounds), and
# UE_LOG output is compiled out, so `docker logs` no longer shows the engine log. An installed
# engine cannot build Shipping with logging (bUseLoggingInShipping needs a unique build
# environment). For a debuggable image, change -clientconfig to Development in a local copy and
# never publish the result.
RUN /home/ue4/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh BuildCookRun \
    -project=/home/ue4/project-src/MetaHumanLocalLLM.uproject \
    -clientconfig=Shipping -utf8output -nodebuginfo -allmaps -noP4 \
    -cook -build -stage -prereqs -pak -archive \
    -archivedirectory=/home/ue4/project-src/dist \
    -platform=Linux

# The package carries its licence and attributions: MetaHumanLocalLLM.Build.cs stages LICENSE,
# NOTICE.md and CREDITS.md beside the paks, and the cooked set includes eight CC BY 4.0 models whose
# attribution has to travel with it (NOTICE.md). They end up in /home/ue4/project/MetaHumanLocalLLM/.
# Refuse an image without them; the same check is in Dockerfile.amd and Scripts/BuildNative.ps1.
RUN for f in LICENSE NOTICE.md CREDITS.md; do \
        test -s "dist/Linux/MetaHumanLocalLLM/$f" || { \
            echo "The package has no MetaHumanLocalLLM/$f, so it would ship without its licence and attributions." >&2; \
            exit 1; }; \
    done

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
