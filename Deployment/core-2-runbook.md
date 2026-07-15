# Hosting on core-2 (AMD Strix Halo) — runbook

Concrete deployment path for `core-2`, measured 2026-07-14 over Tailscale SSH (`root@core-2`).

## What core-2 actually is

| | |
|---|---|
| APU | AMD Ryzen AI Max+ 395 w/ Radeon 8060S (RADV `STRIX_HALO`) |
| OS / kernel | Ubuntu 26.04 LTS, kernel 7.0 |
| CPU / RAM | 32 threads, 125 GB unified |
| Disk | 767 GB free on `/` |
| Render | **Vulkan works** (`AMD Radeon 8060S (RADV STRIX_HALO)`) |
| Encode | **VAAPI on `/dev/dri/renderD128`**: H.264 Baseline/Main/High, HEVC Main/Main10, AV1 (all `VAEntrypointEncSlice`) |
| Docker | present; `/dev/dri` passthrough into a container confirmed working |
| Not present | NVIDIA GPU/NVENC, NVIDIA Container Toolkit, any Unreal install |

Implication: use `Dockerfile.amd` + `docker-compose.amd.yml` (VAAPI H.264), **not** the
NVENC default. WebRTC negotiates H.264, which VAAPI provides here.

## Known issues on the box (flagged, not changed — it's a production host)

- `/root/.docker/config.json` is a **directory**, not a file. This breaks `docker login` and
  emits config-parse warnings. Fix before pulling private images:
  `rmdir /root/.docker/config.json` (it's empty) — Docker recreates it as a file on next login.
- core-2 runs the live CI marketplace stack (`ci-hermes`, `ci-memory` + DB, `ci-openclaw`, …).
  Keep the Pixel Streaming ports off anything those use; everything here is host-networked.

## The one prerequisite still needed: a Linux build

The image's builder stage cooks the project for Linux on `ghcr.io/epicgames/unreal-engine:dev-5.8`.
That requires **Epic GHCR access** (link GitHub↔Epic, accept the UE EULA, `docker login ghcr.io`).
Neither this repo's authoring machine (no `LINUX_MULTIARCH_ROOT` cross-toolchain) nor core-2
(no Unreal installed) has produced a Linux package yet — so the first `docker build` is also
the first Linux cook (long, multi-GB). Alternatives if Epic GHCR access is unavailable:
install UE 5.8 for Linux on core-2 and cook natively, or install the cross-compile toolchain
on the Windows box and `BuildCookRun -platform=Linux` there, then `COPY` the `dist/Linux`
staging into a slim runtime image.

## Deploy steps (once Epic GHCR access is in place)

```bash
# 0. one-time box prep
rmdir /root/.docker/config.json 2>/dev/null || true
docker login ghcr.io                     # Epic-linked GitHub PAT

# 1. get the repo onto core-2
git clone https://github.com/companionintelligence/ci-metahermit.git
cd ci-metahermit/Deployment
cp .env.example .env                     # fill CI_PORTAL_* (OIDC), CI_HUB_* (Ollama), TURN_*
openssl rand -base64 32                  # -> OIDC_COOKIE_SECRET

# 2. build the AMD image (first run also cooks Linux — long)
PROJECT_DOCKERFILE=Deployment/Dockerfile.amd \
  docker compose -f docker-compose.yml -f docker-compose.amd.yml build

# 3. bring the stack up (VAAPI H.264 encode via /dev/dri)
docker compose -f docker-compose.yml -f docker-compose.amd.yml up -d

# 4. verify
docker compose logs -f project           # look for "Streamer connected" / encoder init
vainfo --display drm --device /dev/dri/renderD128 | grep H264   # host-side encode sanity
```

## Access over Tailscale

core-2 is `100.101.156.33` on the tailnet. With host networking, the OIDC proxy is on
`:8080`. From a tailnet client: browse `http://core-2:8080/` → redirected to CI-portal login →
Pixel Streaming player loads. For anything beyond a tailnet test, terminate TLS in front of
`oidc-proxy` and set `cookie_secure = true` (see README).

## Quick smoke test without a full cook

To validate the **control plane** (signalling + TURN + OIDC) before the Linux build exists,
bring up everything except `project`:

```bash
docker compose -f docker-compose.yml -f docker-compose.amd.yml up -d turnserver signalling oidc-proxy
# browse http://core-2:8080/  → CI-portal login should gate the (streamer-less) player page
```
