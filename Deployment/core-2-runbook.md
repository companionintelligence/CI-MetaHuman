# Hosting on core-2 (AMD Strix Halo) — runbook

Concrete deployment path for `core-2`, measured 2026-07-14 over Tailscale SSH (`root@core-2`).

## What core-2 actually is

| | |
|---|---|
| APU | AMD Ryzen AI Max+ 395 w/ Radeon 8060S (RADV `STRIX_HALO`) |
| OS / kernel | Ubuntu 26.04 LTS, kernel 7.0 |
| CPU / RAM | 32 threads, 125 GB unified |
| Disk | 767 GB free on `/` |
| Render | Vulkan enumerates on Radeon 8060S; current UE 5.8 process aborts in RADV (`store_deref`) |
| Encode | **VAAPI on `/dev/dri/renderD128`**: H.264 Baseline/Main/High, HEVC Main/Main10, AV1 (all `VAEntrypointEncSlice`) |
| Docker | present; `/dev/dri` passthrough into a container confirmed working |
| Not present | NVIDIA GPU/NVENC, NVIDIA Container Toolkit, any Unreal install |

Use `Dockerfile.amd` + `docker-compose.amd.yml` for this AMD host. The current UE 5.8 MetaHuman
assets cook, but the renderer crashes on this Strix Halo GPU; the historical UE 5.5 workaround
renders here but cannot load the current UE 5.8 MetaHuman assets. Treat this host as unsupported
for the current MetaHuman runtime until that compatibility gap is resolved.

## Known issues on the box (flagged, not changed — it's a production host)

- `/root/.docker/config.json` is a **directory**, not a file. This breaks `docker login` and
  emits config-parse warnings. Fix before pulling private images:
  `rmdir /root/.docker/config.json` (it's empty) — Docker recreates it as a file on next login.
- core-2 runs the live CI marketplace stack (`ci-hermes`, `ci-memory` + DB, `ci-openclaw`, …).
  Signalling runs in bridge mode with 8090/8888/8889 bound to loopback; the Unreal app and coturn
  use host networking. The OIDC proxy on 8095 is the browser upstream for Tailscale Serve.

## Current build and runtime status (2026-11-02)

The Linux build and cook completed on core-2 using Epic's 5.8 builder image. The packaged app
started, bound all 42 face curves, and joined Pixel Streaming, then aborted during rendering in
RADV with `Unknown intrinsic: @store_deref`. `RADV_DEBUG=llvm` delayed the abort but did not fix
it. The repo's earlier 5.5 workaround is not compatible with the current 5.8 character assets.
The app is not stable enough to publish or expose as a working demo on this hardware yet.

## Stable preview on the RTX 4080 host (2026-11-02)

The live preview uses the Razer Linux host (`razer-nvidia-linux` on Tailscale), not core-2. Its
rootless Docker daemon runs the UE 5.8 package with NVIDIA CDI passthrough and host networking.
The runtime image needs `libegl1`, `libgl1`, `libx11-6`, `libx11-xcb1`, and `libxext6` in
addition to the Epic runtime base: without them, Vulkan reports
`VK_ERROR_INCOMPATIBLE_DRIVER` even though the container can see the GPU. With those libraries,
`vulkaninfo` reports the RTX 4080 and the app has remained up with Pixel Streaming joined, all 42
face curves bound, and its scripted opening generated and spoken by Qwen and Kokoro.

The browser URL remains `https://core-2.capybara-ulmer.ts.net:10000/`; core-2's Tailscale Serve
and CI Portal proxy serve the player, while the renderer on Razer reaches core-2's signalling,
model, and voice services through SSH tunnels. WebRTC media relays through coturn on core-2. The
opening and TTS were verified from the app log. A browser sign-in page is reachable, but passkey
completion still requires the viewer's authenticator. The beta-1 Kokoro aligner is currently
unavailable, so the app uses its audio-length-rescaled lip-sync estimate until that optional
service is restored.

## Deploy steps (once Epic GHCR access is in place)

```bash
# 0. one-time box prep
rmdir /root/.docker/config.json 2>/dev/null || true
docker login ghcr.io                     # Epic-linked GitHub PAT

# 1. get the repo onto core-2
git clone https://github.com/companionintelligence/ci-metahermit.git
cd ci-metahermit/Deployment
cp .env.example .env                     # fill CI_PORTAL_*, CI_HUB_*, and TURN_* values
cp .env.edge.example .env.edge           # set oauth2-proxy client/cookie secrets here

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

core-2 is `100.101.156.33` on the tailnet. Tailscale Serve already publishes the Hub on HTTPS
port 443, so MetaHuman uses HTTPS port 10000 and forwards to the loopback listener at
`127.0.0.1:8095`. From a tailnet client, browse to
`https://core-2.capybara-ulmer.ts.net:10000/`. CI Portal returns to
`https://core-2.capybara-ulmer.ts.net:10000/oauth2/callback`. Signalling ports `:8090`, `:8888`,
and `:8889` bind to loopback only. The OIDC proxy uses CI Portal's explicit endpoints under
`/api/auth` because discovery is hosted at `/api/auth/.well-known/openid-configuration` while the
issuer remains the origin root.

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

To validate the **control plane** (signalling + TURN + OIDC) without starting the Unreal renderer,
bring up everything except `project`:

```bash
docker compose -f docker-compose.yml -f docker-compose.amd.yml up -d turnserver signalling oidc-proxy
# Start Tailscale Serve: tailscale serve --https=10000 --bg http://127.0.0.1:8095
# browse https://core-2.capybara-ulmer.ts.net:10000/ -> CI Portal login gates the player page
```

Image auth note: `coturn` and `oauth2-proxy` images are public, but the **signalling-server
image is Epic-gated** (`ghcr.io/epicgames/pixel-streaming-signalling-server` needs
`docker login ghcr.io`). Without Epic GHCR access, build the signalling server from its
open-source repo (`github.com/EpicGamesExt/PixelStreamingInfrastructure`, plain Node.js, no
Epic auth). Ports verified free on core-2: 8090, 8888, 3478 (8080 is taken -> we use 8095).
