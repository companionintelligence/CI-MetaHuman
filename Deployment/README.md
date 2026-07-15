# Containerized Pixel Streaming deployment

Runs MetaHumanLocalLLM as a browser-accessible, GPU-rendered agent: the packaged Unreal app
streams video/audio over WebRTC (Pixel Streaming), **CI-portal OIDC** gates access, and the
agent's brain is **CI-hub's Ollama** (or any OpenAI-compatible endpoint) selected purely
through environment variables. Built on Epic's official Pixel Streaming container pattern
(`Engine/Extras/Containers/Examples/PixelStreaming`), plus an `oauth2-proxy` login gate.

```
browser ──▶ oidc-proxy :8080 ──▶ signalling :8090 (player) / :8888 (streamer)
   (CI-portal OIDC)                     ▲
                                        │ WebRTC media ⇄ coturn :3478
   packaged UE app (GPU) ──────────────┘
        └── chat ──▶ CI-hub Ollama (OpenAI-compatible, via CI_HUB_URL)
```

## Components (`docker-compose.yml`)

| Service      | Image                                                       | Role |
|--------------|-------------------------------------------------------------|------|
| `project`    | built from `Deployment/Dockerfile`                          | The packaged UE app; renders offscreen, streams, talks to CI-hub |
| `signalling` | `ghcr.io/epicgames/pixel-streaming-signalling-server`       | WebRTC signalling + player page |
| `oidc-proxy` | `quay.io/oauth2-proxy/oauth2-proxy`                          | CI-portal OIDC login in front of the stream |
| `turnserver` | `coturn/coturn`                                             | WebRTC relay for NAT/firewall traversal |

## Prerequisites

1. **Epic image access** — link your GitHub account to Epic Games and accept the UE EULA,
   then `docker login ghcr.io`. Grants access to `ghcr.io/epicgames/unreal-engine:*` and the
   signalling-server image.
2. **GPU host** — a Linux host with an NVIDIA GPU and the
   [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/).
   Pixel Streaming needs hardware NVENC.
3. **Docker + Compose v2.**
4. **CI-portal OIDC client** — register this deployment as an OIDC client in ci-portal and
   note the issuer/discovery URL, client id, and client secret.
5. **CI-hub** — an OpenAI-compatible base URL (`.../v1`) and a token.

## Configure

```bash
cd Deployment
cp .env.example .env
# edit .env: CI_PORTAL_* (OIDC), CI_HUB_* (Ollama), TURN_*, OIDC_COOKIE_SECRET
openssl rand -base64 32   # value for OIDC_COOKIE_SECRET
```

## Build & run

```bash
./run.sh --rebuild     # first time: packages the UE app into an image (long — full cook)
./run.sh               # subsequent starts
```

Then browse to the proxy (e.g. `https://agent.example.com/` → `http://<host>:8080/` in dev).
You are redirected to ci-portal to sign in; after login the Pixel Streaming player loads and
connects to the running agent.

## How the wiring maps to the app

- **CI-hub Ollama** — `project`'s environment sets `MHLLM_BACKEND=openai`, `CI_HUB_URL`,
  `CI_HUB_TOKEN`, `CI_HUB_MODEL`. On startup `FMetaHumanLLMEnvConfig` applies these to the
  settings, so `UOpenAIChatClient` posts to `<CI_HUB_URL>/chat/completions` with the bearer
  token. Swapping to a sidecar Ollama is just `OLLAMA_BASE_URL` + `MHLLM_BACKEND=ollama`.
- **Voice** — set `WHISPER_SERVER_URL` / `PIPER_SERVER_URL` to co-deployed STT/TTS services
  to activate `UWhisperSpeechToText` / `UPiperTextToSpeech`; leave blank to keep the stubs.
- **OIDC** — handled entirely by `oidc-proxy`; the UE app needs no auth code. Restrict who
  can connect via `email_domains` / group claims in `oauth2-proxy.cfg`.

## Production notes / caveats

- **TLS is mandatory in production.** Terminate HTTPS in front of `oidc-proxy` (or set it up
  directly) and set `cookie_secure = true` in `oauth2-proxy.cfg` — OIDC cookies over plain
  HTTP are unsafe. Set `CI_PORTAL_REDIRECT_URL` to the public `https://…/oauth2/callback`.
- **Packaging caveat.** The build cooks a MetaHuman project for Linux; the MetaHuman plugins
  are Linux-enabled in the `.uproject`, but a first Linux cook can surface asset/plugin issues
  worth validating on a dev host before relying on the image in CI.
- **Ports.** Expose only `:8080` (the OIDC proxy) publicly. Keep signalling `:8090` internal.
  TURN needs `3478` (TCP/UDP) and the UDP relay range `49152-65535`.
- **Pixel Streaming 2** is the plugin used (`-PixelStreamingUrl=ws://127.0.0.1:8888`). For the
  legacy Pixel Streaming plugin, substitute `-PixelStreamingIP`/`-PixelStreamingPort`.
- This deployment could not be built/run in the authoring environment (no GPU host / no Epic
  GHCR access there); the manifests follow Epic's official example and oauth2-proxy's
  documented OIDC config. Validate on a GPU host before shipping.
