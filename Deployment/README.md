# Containerized Pixel Streaming deployment

Runs MetaHumanLocalLLM as a browser-accessible, GPU-rendered agent: the packaged Unreal app
streams video/audio over WebRTC (Pixel Streaming), **CI-portal OIDC** gates access, and the
agent's brain is an **OpenAI-compatible chat endpoint** (or a native Ollama, via an override
file) selected through environment variables. Built on Epic's official Pixel Streaming container
pattern (`Engine/Extras/Containers/Examples/PixelStreaming`), plus an `oauth2-proxy` login gate.

```
browser ──▶ oidc-proxy :8095 ──▶ signalling :8090 (player) / :8888 (streamer)
   (CI-portal OIDC)                     ▲
                                        │ WebRTC media ⇄ coturn :3478
   packaged UE app (GPU) ──────────────┘
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
| `signalling` | `ghcr.io/epicgames/pixel-streaming-signalling-server`       | WebRTC signalling + player page |
| `oidc-proxy` | `quay.io/oauth2-proxy/oauth2-proxy`                          | CI-portal OIDC login in front of the stream; the only reader of `.env.edge` |
| `turnserver` | `coturn/coturn`                                             | WebRTC relay for NAT/firewall traversal |

All four run with `network_mode: host`.

## Prerequisites

1. **Epic image access** — link your GitHub account to Epic Games and accept the UE EULA,
   then `docker login ghcr.io`. Grants access to `ghcr.io/epicgames/unreal-engine:*` and the
   signalling-server image.
2. **GPU host** — a Linux host that can render (Vulkan) and hardware-encode video. Two paths:
   - **NVIDIA** — GPU + [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/),
     NVENC. Use the default `Dockerfile` + `docker-compose.nvidia.yml`.
   - **AMD** (e.g. core-2, a Strix Halo APU) — GPU render via `/dev/dri`; encode is **VP8 in
     software**, since PixelStreaming supports NVENC and AMF and neither works on RADV/Linux. Use
     `Dockerfile.amd` + `docker-compose.amd.yml`. See [`core-2-runbook.md`](core-2-runbook.md).
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
| `docker-compose.nvidia.yml` | NVENC H.264 encode + an `nvidia` GPU device reservation |
| `docker-compose.amd.yml` | GPU render + `/dev/dri` device plumbing (`RENDER_GID`/`VIDEO_GID`, `LIBVA_DRIVER_NAME`). Encode stays **VP8 in software** — PixelStreaming's hardware paths are NVENC and AMF, neither of which exists on RADV/Linux, so the file selects VP8 explicitly rather than let a doomed probe run |
| `docker-compose.ollama.yml` | pins `MHLLM_BACKEND: ollama`; `OLLAMA_BASE_URL`/`OLLAMA_MODEL` defaults; patched signalling start script |
| `docker-compose.face.yml` | the companion/face stack: pins `ollama`, the beta-1 model and voice endpoints, persona, opening shot |
| `docker-compose.ue55.yml` | runs the 5.5-built image instead of the default 5.8 one |
| `docker-compose.mesagit.yml` | runs against a mesa-git RADV build layered into a `-mesagit` image |

First build also runs the initial Linux cook (long, multi-GB). For core-2 specifically,
follow [`core-2-runbook.md`](core-2-runbook.md) — it has the measured GPU/VAAPI facts, the
box prep, and a control-plane-only smoke test you can run before the cook finishes.

Then browse to the proxy (e.g. `https://agent.example.com/` → `http://<host>:8095/` in dev).
You are redirected to ci-portal to sign in; after login the Pixel Streaming player loads and
connects to the running agent.

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
  `UNullSpeechToText` for `UWhisperSpeechToText`. Note that `KOKORO_SERVER_URL` is **not** in the
  interpolated ten above: set it in `.env`, or bring the stack up with `docker-compose.face.yml`,
  which supplies it along with `KOKORO_VOICE`, the aligner URL and `WHISPER_MODEL`.
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
- **Ports.** Expose only `:8095` (the OIDC proxy — see `http_address` in `oauth2-proxy.cfg`)
  publicly. Keep signalling `:8090` internal. TURN needs `3478` (TCP/UDP) and the UDP relay range
  `49152-65535`.
- **Pixel Streaming 2** is the plugin enabled in the `.uproject`
  (`-PixelStreamingUrl=ws://127.0.0.1:8888`). For the legacy Pixel Streaming plugin, substitute
  `-PixelStreamingIP`/`-PixelStreamingPort`.
- **Not yet built or run end to end.** [`core-2-runbook.md`](core-2-runbook.md) records that
  neither this repo's authoring machine nor core-2 has produced a Linux package, so the first
  `docker build` is also the first Linux cook. The manifests follow Epic's official example and
  oauth2-proxy's documented OIDC config; validate on a GPU host before shipping.
