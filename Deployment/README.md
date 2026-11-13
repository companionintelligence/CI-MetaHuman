# Containerized Pixel Streaming deployment

Runs MetaHumanLocalLLM as a browser-accessible, GPU-rendered agent: the packaged Unreal app
streams video/audio over WebRTC (Pixel Streaming), **CI-portal OIDC** gates access, and the
agent's brain is an **OpenAI-compatible chat endpoint** (or a native Ollama, via an override
file) selected through environment variables. Built on Epic's official Pixel Streaming container
pattern (`Engine/Extras/Containers/Examples/PixelStreaming`), plus an `oauth2-proxy` login gate.

```
browser ──▶ Tailscale HTTPS :10000 ──▶ oidc-proxy :8095 ──▶ signalling :8090 (loopback)
   (CI-portal OIDC)                                               ▲
                                                                 │ :8888 (loopback)
   packaged UE app (GPU) ────────────────────────────────────────┘
             └── WebRTC media ⇄ coturn :3478
        └── chat ──▶ OpenAI-compatible server at CI_HUB_URL
                     (default http://127.0.0.1:11434/v1 — a local Ollama)
```
(OIDC proxy on :8095 — :8080 is taken on core-2 by the CI stack.)

**The Hub is not in that chat path.** CI-Hub is an app and inference *control plane*; it does not
proxy inference — its own inference controller says so, and `Docs/COMPAT-AND-ECOSYSTEM-REVIEW.md`
§ 3 reached the same conclusion by enumerating every path in the Hub's `swagger.json` and finding
no `/v1`, no `*chat*`, no `*completions*`. What the Hub distributes to registered apps, over
`GET /api/inference/apps/<slug>/credentials`, is the *backend's* OpenAI base URL. So `CI_HUB_URL`
is a historical name for the address of the inference server itself, not of the Hub — point it at
the Ollama the Hub manages (the default above) or at any other OpenAI-compatible server. The long
comment above `CI_HUB_URL` in `docker-compose.yml` carries the details, including why the
`/api/inference/v1` base that appears in CI-Server's `hub-inference-integration.md` is not one to
configure against.

## Components (`docker-compose.yml`)

| Service      | Image                                                       | Role |
|--------------|-------------------------------------------------------------|------|
| `project`    | built from `Deployment/Dockerfile`                          | The packaged UE app; renders offscreen, streams, and talks to the chat endpoint |
| `signalling` | `ci/pixelstreaming-signalling:ue5.8` (built locally) | UE 5.8 WebRTC signalling + patched player page |
| `oidc-proxy` | `quay.io/oauth2-proxy/oauth2-proxy`                          | CI-portal OIDC login in front of the stream; the only reader of `.env.edge` |
| `turnserver` | `coturn/coturn`                                             | WebRTC relay for NAT/firewall traversal |

The Unreal app and coturn use host networking. The signalling server runs in bridge mode with
ports 8090, 8888, and 8889 published only on `127.0.0.1`; oauth2-proxy is the only browser route.

## Prerequisites

1. **Epic image access** — link your GitHub account to Epic Games and accept the UE EULA,
   then `docker login ghcr.io` for the Unreal Engine image. The signalling server is built from
   Epic's public `PixelStreamingInfrastructure` source pinned in `.env.example`.
   The NVIDIA runtime includes EGL/GLVND libraries for headless Vulkan. If you need to reuse an
   older packaged image without recooking, add them with `Deployment/Dockerfile.vulkan-runtime`.
2. **GPU host** — a Linux host that can render (Vulkan) and encode video. Two GPU paths:
   - **NVIDIA** — GPU with Vulkan and NVENC. Use the default `Dockerfile` +
     `docker-compose.nvidia.yml` and NVIDIA Container Toolkit CDI. The default stream codec is VP8:
     this was verified on the RTX 4080 preview because Unreal's H.264/NVENC encoder failed to
     initialize in the runtime container even though NVENC worked on the host. For rootless Docker,
     generate the user-scoped spec with `nvidia-ctk cdi generate --output="$HOME/.config/cdi/nvidia.yaml"`;
     this does not require changing or restarting the Docker daemon. Set
     `PIXEL_STREAMING_ENCODER_CODEC=H264` only after validating NVENC in the app container.
   - **AMD** (e.g. core-2, a Strix Halo APU) — GPU render via `/dev/dri`; encode is **VP8 in
     software**, since PixelStreaming supports NVENC and AMF and neither works on RADV/Linux. Use
     `Dockerfile.amd` + `docker-compose.amd.yml`. The current UE 5.8 MetaHuman build cooks but
     crashes in RADV during rendering on Strix Halo; the UE 5.5 renderer works on that GPU, but
     cannot load the current UE 5.8 MetaHuman assets. See [`core-2-runbook.md`](core-2-runbook.md).
   - No GPU / fallback — the base file's software VP8 encoder (CPU) runs anywhere but is slower.
3. **Docker + Compose v2.**
4. **CI-portal OIDC client** — register this deployment as an OIDC client in ci-portal and note
   the issuer/discovery URL, client id, and client secret. The first two and the redirect URL go in
   `.env`; the client secret goes in `.env.edge` (see below).
5. **An inference server** — anything speaking the OpenAI `/chat/completions` shape, addressed by
   its base URL up to and including the version segment (`…/v1`); `UOpenAIChatClient` appends the
   path itself. The default is a local Ollama on `127.0.0.1:11434/v1`. Ollama's `/v1` validates no
   key, so `CI_HUB_TOKEN` against it can be any non-empty literal — a real key matters only if you
   repoint this at a gateway that checks one.

## Configure

**There are two files to copy, not one:**

```bash
cd Deployment
cp .env.example      .env        # app settings + everything compose interpolates
cp .env.edge.example .env.edge   # the two oauth2-proxy secrets; the login proxy alone reads them
openssl rand -base64 32          # value for OAUTH2_PROXY_COOKIE_SECRET in .env.edge
```

The Pixel Streaming render defaults to 1280x720 (`STREAM_RES_X`/`STREAM_RES_Y`). Override both values
in `.env` when a host can sustain a larger frame; 1920x1080 saturated the preview RTX 4080 and
produced a low, unstable frame rate during a long browser session.

[`.env.example`](.env.example) and [`.env.edge.example`](.env.edge.example) **are the reference for
every key** — each carries its own comment, its default, and (where it matters) why it ships blank.
There is deliberately no second copy of that list here. The values with no usable default are
`CI_PORTAL_ISSUER_URL` / `CI_PORTAL_CLIENT_ID` / `CI_PORTAL_REDIRECT_URL` and
`TURN_USER` / `TURN_PASSWORD` in `.env`, plus `OAUTH2_PROXY_CLIENT_SECRET` and
`OAUTH2_PROXY_COOKIE_SECRET` in `.env.edge`.

Three things about that copy step that the commands themselves do not show:

- **Why the split.** `.env` is handed to the app container whole (next section), so a secret the
  app has no use for should not be in it. The two OIDC secrets moved to `.env.edge`, which only
  `oidc-proxy` reads. They are spelled with oauth2-proxy's own `OAUTH2_PROXY_*` names because
  `env_file` injects a file's keys under the names in the file and cannot rename on the way.
- **`MHLLM_BACKEND` selects the chat protocol, and the file you just copied sets it.**
  `.env.example` ships `MHLLM_BACKEND=openai`; `docker-compose.yml` independently defaults the same
  key to `openai`, so a missing or blank `.env` lands in the same place. For the native Ollama
  protocol, add `-f docker-compose.ollama.yml` — it pins the backend itself rather than relying on
  that line.
- **Neither file is required to RENDER the stack.** Both `env_file` entries are declared
  `required: false`, so `docker compose config` succeeds on a clean checkout that has neither,
  instead of failing on a missing file. That is about rendering, not running: with neither file the
  OIDC client id and cookie secret come out empty and coturn's `-u` renders as a bare `:`, so the
  login gate cannot actually work. Every documented procedure copies `.env` first —
  [`core-2-runbook.md`](core-2-runbook.md)'s deploy steps begin with `cp .env.example .env`. `run.sh` is stricter than
  compose: it exits with an error if `.env` is missing.

> **Check `git status` before committing.** `.gitignore` covers `Deployment/.env` but, as of this
> writing, has no line for `Deployment/.env.edge` — confirmed with `git check-ignore`. A filled-in
> `.env.edge` is a live client secret sitting in an untracked, un-ignored file.

## Build & run

The `project` image cooks the **authored** set, which no Dockerfile can produce on its own: author
it first and put the authored `Content/`, with the `AUTHOR_RESULT.txt` the authoring run writes
beside the map, in the build context. Otherwise the build stops at its `CHECK_MAP` step, which also
refuses a map authored before the current resident and prop rules, and Nia's or Seo's content
unless asked for (`--build-arg RESIDENT=nia` or `metahuman`). See
[`Docs/BUILD-AND-DEPLOY.md`](../Docs/BUILD-AND-DEPLOY.md) §2.2.

```bash
# NVIDIA host:
docker compose -f docker-compose.yml -f docker-compose.nvidia.yml up -d

# AMD host (core-2): use the AMD Dockerfile + VAAPI override
PROJECT_DOCKERFILE=Deployment/Dockerfile.amd \
  docker compose -f docker-compose.yml -f docker-compose.amd.yml up -d --build
```

`./run.sh [--rebuild]` is the convenience path, but read what it does before reaching for it on a
GPU box: it sources `.env`, fills in `PUBLIC_IP` if blank, and then runs
`docker compose up --force-recreate` with **no `-f` overrides at all** — so it renders the base file
alone and lands on the software VP8 encoder. Name the vendor override yourself for hardware encode.

The override files, each documented in its own header comment:

| File | What it adds |
|---|---|
| `docker-compose.nvidia.yml` | NVIDIA Vulkan render through `nvidia.com/gpu=all` CDI; VP8 is the default stream codec until NVENC initializes in the app container; starts the CPU-only Kokoro alignment sidecar beside the renderer |
| `docker-compose.amd.yml` | GPU render + `/dev/dri` device plumbing (`RENDER_GID`/`VIDEO_GID`, `LIBVA_DRIVER_NAME`). Encode stays **VP8 in software** — PixelStreaming's hardware paths are NVENC and AMF, neither of which exists on RADV/Linux, so the file selects VP8 explicitly rather than let a doomed probe run |
| `docker-compose.ollama.yml` | pins `MHLLM_BACKEND: ollama`; `OLLAMA_BASE_URL`/`OLLAMA_MODEL` defaults |
| `docker-compose.face.yml` | the companion/face stack: pins `ollama`, the beta-1 model and voice endpoints, persona, opening shot |
| `docker-compose.ue55.yml` | runs the 5.5-built image instead of the default 5.8 one |
| `docker-compose.mesagit.yml` | runs against a mesa-git RADV build layered into a `-mesagit` image |

First build also runs the initial Linux cook (long, multi-GB). For core-2 specifically,
follow [`core-2-runbook.md`](core-2-runbook.md) — it has the measured GPU/VAAPI facts, the
box prep, and a control-plane-only smoke test you can run before the cook finishes.

On core-2, Tailscale Serve publishes the loopback proxy over HTTPS at
`https://<core-2's MagicDNS name>:10000/`. The name ends in `.ts.net`; `tailscale status` on core-2
shows it, and it is not recorded in this public repository. The redirect URI registered with CI
Portal is that origin plus `/oauth2/callback`, and `CI_PORTAL_REDIRECT_URL` in `.env` must match it
exactly (`.env.example` carries the placeholder
`https://stream-host.example.invalid:10000/oauth2/callback`). The browser is sent to
`https://hub.ci.computer` to sign in; after login the Pixel Streaming player loads and connects
to the running agent. Port `:8095` itself is loopback-only, and the stream's signalling HTTP port
must not be opened around the proxy.

## How the wiring reaches the app

**The app is handed the whole `.env` file, minus a short deny-list.** `docker-compose.yml` gives
`project` an `env_file: .env`, so every key in that file arrives in the container — except
`CI_PORTAL_CLIENT_SECRET`, `OIDC_COOKIE_SECRET` and `TURN_PASSWORD`, which the service's
`environment:` block re-declares as empty strings so they cannot reach the app through a `.env`
written before the split. `TURN_PASSWORD` has to stay in `.env` even so, because compose
*interpolates* it into turnserver's `-u` argument and into the signalling server's
`peerConnectionOptions`; blanking it for the app is the only way to keep it out of the app. The
upside of `env_file` is that a key added to `.env.example` and copied into `.env` reaches the app
with no compose edit at all.

**`env_file` reads the FILE, not your shell — and this is the part that bites.**
`VAR=x docker compose up` sets `VAR` in the *compose process's* environment, and compose consults
that only when expanding a `${VAR}`. A key delivered solely by `env_file` is never expanded, so the
shell assignment is silently ignored: no error, no warning, and the container starts with the `.env`
value. The base file therefore keeps a short interpolated `environment:` block for the values a
one-off run actually flips — and an `environment:` entry outranks `env_file`, so for those the shell
does win. In the **base file** that set is exactly these ten:

```
MHLLM_BACKEND
CI_HUB_URL           CI_HUB_TOKEN       CI_HUB_MODEL
OPENAI_BASE_URL      OPENAI_API_KEY     OPENAI_MODEL
WHISPER_SERVER_URL   PIPER_SERVER_URL   MHLLM_SYSTEM_PROMPT
```

**Each override file adds its own**, so the set depends on which `-f` files you pass — this is the
part that is easy to get wrong. `docker-compose.face.yml` interpolates twenty more, including all
four `KOKORO_*`, both `OLLAMA_*`, `WHISPER_MODEL` and `MHLLM_THINKING`; `docker-compose.ollama.yml`
adds `OLLAMA_BASE_URL`, `OLLAMA_MODEL` and `PUBLIC_IP`. So `KOKORO_MODEL=x docker compose … up`
does nothing on the base stack and works on the face stack.

Anything interpolated by none of the files you passed — `MHLLM_MAX_TURNS`, `MHLLM_MEMORY`,
`MHLLM_CHAT_TIMEOUT`, … — **has to be edited in `.env`**. Take none of this on trust; the merged
result is printable, and these two lines are the whole distinction:

```bash
MHLLM_BACKEND=ollama docker compose config | grep MHLLM_BACKEND     # -> ollama   (interpolated: shell wins)
MHLLM_MAX_TURNS=99   docker compose config | grep MHLLM_MAX_TURNS   # -> "24"     (env_file only: shell ignored)
```

**The traffic is one-way: a service's `env_file` feeds that container's environment and never
compose's `${...}` interpolation.** Two consequences worth knowing before you move a key between
files. Nothing in `.env.edge` can be referenced as `${...}` in a compose file — which is why
`TURN_PASSWORD`, whose only consumers are interpolated command-line arguments, cannot move there.
And `.env` does double duty: it is both the app's `env_file` and compose's project env file, so a
value set there *also* wins over an override file's `${VAR:-default}`. That is why `.env.example`
ships `OLLAMA_BASE_URL`, `OLLAMA_MODEL`, `KOKORO_MODEL` and `MHLLM_SYSTEM_PROMPT` blank — its
comments explain each case.

**Which file to edit when a value changes:**

| Changing… | Edit |
|---|---|
| a per-deployment value (endpoints, model, persona, memory, turn cap, TURN credentials) | `Deployment/.env` |
| the OIDC client secret or the cookie secret | `Deployment/.env.edge` |
| the documented default and the comment an operator reads | `.env.example` / `.env.edge.example` |
| the fallback a *missing or blank* `.env` lands on | the `${VAR:-default}` in `docker-compose.yml` |
| a per-stack default (face/ollama endpoints, models, opening shot) | that override file |

### The three paths, concretely

- **Chat** — `MHLLM_BACKEND=openai` selects `UOpenAIChatClient`, which POSTs to
  `<OpenAIBaseUrl>/chat/completions` and sends `Authorization: Bearer <key>` when the key is
  non-empty. `FMetaHumanLLMEnvConfig::Apply` maps `CI_HUB_URL`/`CI_HUB_TOKEN`/`CI_HUB_MODEL` onto
  `OpenAIBaseUrl`/`OpenAIApiKey`/`OpenAIModel` and then applies `OPENAI_*` on top, so an explicit
  `OPENAI_*` wins where both are set — that trio is how you aim this build at LM Studio, vLLM,
  llama.cpp or Lemonade instead. An explicit `MHLLM_BACKEND` is authoritative and outranks the
  backend that a set `CI_HUB_URL` would otherwise imply; an unrecognised value is logged at Warning
  and ignored rather than falling through silently. For the native Ollama protocol add
  `-f docker-compose.ollama.yml`, which pins the literal `ollama` — an override file's value
  replaces the base file's, so nothing in `.env` can undo it. `MHLLM_THINKING` maps onto the
  `bEnableThinking` setting, which both chat clients act on; the comment block on that property in
  `MetaHumanLLMSettings.h` has the per-backend behaviour and its measured cost.
- **Voice** — TTS is selected at `BeginPlay`, in this order: `KOKORO_SERVER_URL` →
  `UKokoroTextToSpeech`; else `PIPER_SERVER_URL` → `UPiperTextToSpeech`; else the constructor's
  `UNullTextToSpeech`, which is silent. Silence is invisible from the client — the WebRTC audio
  track still negotiates and goes live — so a connected player is no evidence of speech; the app
  logs a Warning naming both variables when neither is set. `WHISPER_SERVER_URL` likewise swaps
  `UNullSpeechToText` for `UWhisperSpeechToText`. The base project now passes the optional
  `KOKORO_SERVER_URL`, `KOKORO_MODEL`, `KOKORO_VOICE` and `KOKORO_ALIGNER_URL` values from `.env`;
  `docker-compose.face.yml` supplies its face-stack defaults, including `WHISPER_MODEL`.
- **OIDC** — handled entirely by `oidc-proxy`; the UE app needs no auth code. The proxy takes its
  client id, issuer and redirect URL from `.env` by interpolation and its two secrets from
  `.env.edge` by `env_file`. Restrict who can connect via `email_domains` / group claims in
  `oauth2-proxy.cfg`.

## Production notes / caveats

- **TLS is mandatory in production.** Terminate HTTPS in front of `oidc-proxy` (or set it up
  directly) and set `cookie_secure = true` in `oauth2-proxy.cfg` — OIDC cookies over plain
  HTTP are unsafe. Set `CI_PORTAL_REDIRECT_URL` to the public `https://…/oauth2/callback`, and keep
  it in sync with the port in the proxy's `http_address`.
- **Packaging caveat.** The build cooks a MetaHuman project for Linux; the MetaHuman plugins
  are Linux-enabled in the `.uproject`, but a first Linux cook can surface asset/plugin issues
  worth validating on a dev host before relying on the image in CI.
- **Ports.** Publish signalling `:8090`, `:8888`, and `:8889` on loopback only. Expose the
  Tailscale Serve HTTPS port `:10000` to tailnet clients; it forwards to oauth2-proxy's loopback
  listener on `:8095`. TURN needs `3478` (TCP/UDP) and the UDP relay range `49152-65535` reachable
  within the tailnet.
- **The AI notice.** The resident introduces herself with a human name, so the stream says what she
  is (EU AI Act Art. 50(1)): a line in the frame from its first frame (`SCompanionAiNotice`), the
  same words on the player page with a Privacy link (`patch_pixelstreaming_player.py`, built into
  the signalling image), "an AI" in the chat box's idle status, and the face stack's opening line.
  The link defaults to the policy that covers the hosted preview; an operator whose deployment has
  its own policy builds the signalling image with `--build-arg PRIVACY_POLICY_URL=https://...`.
- **Pixel Streaming 2** is the plugin enabled in the `.uproject`
  (`-PixelStreamingUrl=ws://127.0.0.1:8888`). For the legacy Pixel Streaming plugin, substitute
  `-PixelStreamingIP`/`-PixelStreamingPort`.
- **Encoder and player codec must match.** The pinned signalling image build patches the upstream
  frontend to select VP8 unless the URL explicitly sets `PreferredCodec`; the NVIDIA and AMD previews use VP8 when H.264 hardware
  encoder initialization is unavailable. This avoids the misleading state where WebRTC connects
  and audio flows, but the browser waits forever for video.
- **Runtime validation.** On 2026-11-02 the Linux build and cook completed on core-2; the UE 5.8
  renderer then aborted in RADV, so core-2 is not a supported renderer for these assets. A stable
  preview is now running on the Razer RTX 4080 over Tailscale: Vulkan sees the GPU, the face driver
  resolves all 42 curves, Pixel Streaming joins, and the opening response is generated and spoken.
  The player initially negotiated WebRTC and audio but no video because the UE H.264/NVENC encoder
  could not initialize in the container; matching the tested VP8 codec in the app and player now
  streams the 1920x1080 scene in the Codex browser. On NVIDIA previews, the same compose override
  runs Kokoro alignment locally on port 13401; live speech returned measured phoneme timing aligned
  to the generated voice clip. The static conversation camera is 300cm back at 28-degree FOV, with
  the look point lowered and shifted slightly to give her more room beside the chat HUD. SSO
  redirects to CI Portal; passkey completion remains a browser-side step. See
  [`core-2-runbook.md`](core-2-runbook.md).
