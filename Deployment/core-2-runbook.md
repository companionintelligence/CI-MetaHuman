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
`:8095` (:8080 is already taken on core-2). From a tailnet client: browse `http://core-2:8095/` → redirected to CI-portal login →
Pixel Streaming player loads. For anything beyond a tailnet test, terminate TLS in front of
`oidc-proxy` and set `cookie_secure = true` (see README).

## Verifying the beta-1 forwards

The model services do not run on core-2. They run on beta-1 and reach this box through one
`--user` systemd SSH unit per port in [`core2/`](core2/). Replies and speech both cross those
forwards, and a break anywhere along the chain reaches you only as an HTTP status in the log —
spelled differently by each client, so there is no one string to grep for:
`Kokoro: synthesis failed (HTTP %d)` (`KokoroTextToSpeech.cpp:117`), `Ollama returned HTTP %d`
(`LLM/OllamaClient.cpp:172`), `OpenAI-compatible server returned HTTP %d`
(`LLM/OpenAIChatClient.cpp:164`).

The recorded instance is worth reading before assuming the forward is at fault. `HTTP 0` on the
TTS path ([`../Docs/LEARNINGS.md`](../Docs/LEARNINGS.md), commit `4480553` *"supervise the voice
tunnels, and stop trusting a listener"*) turned out **not** to be a dropped forward: that commit's
own message records *"The tunnel was a symptom, not the cause… both voice containers were
stopped."* The listener was healthy and the far end was gone — which is exactly why the check
below is by status code and not by socket.

The units carry `Restart=always` and SSH keepalives for that class of failure. The restart backoff
is newer and is stated as intent rather than as a fix for a logged incident; the unit comments say
so themselves.

**Check a forward by status code, never by socket.** A forward whose far end is gone still shows a
`LISTEN` socket owned by a live `ssh` pid, so `ss` cannot tell a working tunnel from a dead one —
only a real status code can. And a `200` proves that *something* answered, not that the service
behind the port is the one that answered.

Two files already carry the exact probe commands; a third divergent copy is how a port and a
path drift apart, so this section points at them instead of repeating them:

- [`core2/README.md`](core2/README.md) § *Service tunnels* — one `%{http_code}` curl per port,
  plus the failure that motivated the units. Treat this as the copy to edit if a port or a path
  ever changes.
- [`../Docs/TESTING.md`](../Docs/TESTING.md) § 5.4 — the same probes, filed under § 5 *The
  verification discipline* alongside the pixel and audio-energy versions of the same idea.
- [`../Docs/ARCHITECTURE.md`](../Docs/ARCHITECTURE.md) § 2.3 — the rule plus the two design
  constraints it produced: one port per unit, and a listener is not a tunnel.

## Quick smoke test without a full cook

To validate the **control plane** (signalling + TURN + OIDC) before the Linux build exists,
bring up everything except `project`:

```bash
docker compose -f docker-compose.yml -f docker-compose.amd.yml up -d turnserver signalling oidc-proxy
# browse http://core-2:8095/  -> CI-portal login should gate the (streamer-less) player page
```

Image auth note: `coturn` and `oauth2-proxy` images are public, but the **signalling-server
image is Epic-gated** (`ghcr.io/epicgames/pixel-streaming-signalling-server` needs
`docker login ghcr.io`). Without Epic GHCR access, build the signalling server from its
open-source repo (`github.com/EpicGamesExt/PixelStreamingInfrastructure`, plain Node.js, no
Epic auth). Ports verified free on core-2: 8090, 8888, 3478 (8080 is taken -> we use 8095).
