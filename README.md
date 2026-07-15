# CI MetaHermit

A locally-embodied AI companion: an Unreal Engine 5.8 **MetaHuman** avatar driven by a
local LLM, delivered to the browser via **Pixel Streaming (WebRTC)**, gated by **CI-portal
OIDC**, and talking to **CI-hub Ollama**. The Unreal-engine sibling of the org's other 3D
agents.

## What it does

- **MetaHuman avatar** (`DefaultCompanion`, fully rigged) speaks LLM responses aloud with
  amplitude-driven lip-sync.
- **Pluggable chat backend** (`IChatBackend`): Ollama native `/api/chat` or any
  OpenAI-compatible `/v1/chat/completions` (SSE) — so it runs against CI-hub, OpenAI, or a
  local Ollama by config alone.
- **Local voice**: Whisper STT client + Piper TTS client (+ WAV decode + viseme envelope).
- **Emotion mapping** drives facial expression from reply text.
- **Container-native config**: every endpoint/credential comes from environment variables
  (`CI_HUB_URL`, `CI_HUB_TOKEN`, `MHLLM_BACKEND`, `WHISPER_SERVER_URL`, …) applied at
  module startup — one image, every environment.

## Layout

| Path | What |
|---|---|
| `Source/MetaHumanLocalLLM/` | C++ module (LLM backends, voice, avatar orchestrator, Slate HUD, settings, env-config) |
| `Source/.../Private/Tests/` | 62 Automation Spec tests (57 backend + 5 frontend) |
| `Content/MetaHumans/DefaultCompanion` | The rigged MetaHuman avatar |
| `Deployment/` | Dockerfile, docker-compose, oauth2-proxy (OIDC), coturn, run.sh |
| `Docs/FOLLOW-UP.md` | Status, remaining work, and hard-won gotchas |
| `Scripts/` | Headless test runners |

## Build & test

Editor **must be closed** (Live Coding locks the DLL); adding the first C++ module needs one
Editor restart.

```sh
# Compile
"<UE>/Engine/Build/BatchFiles/Build.bat" MetaHumanLocalLLMEditor Win64 Development -project=MetaHumanLocalLLM.uproject
# Test (headless)
pwsh Scripts/RunBackendTests.ps1
pwsh Scripts/RunFrontendTests.ps1
```

## Deploy (Pixel Streaming)

See [`Deployment/README.md`](Deployment/README.md). Requires an **NVIDIA GPU host** (Pixel
Streaming uses NVENC hardware encoding). `cp Deployment/.env.example Deployment/.env`, fill
in CI-portal + CI-hub, then `Deployment/run.sh --rebuild`.

## Licensing

Uses MetaHuman assets — may be rendered/animated freely but **not** used to train or enhance
AI models. This project only renders/animates in response to LLM output. Flag to counsel
before any commercial ship.
