# Follow-up / deferred scope

Captured so nothing from the original planning doc or these passes gets silently dropped.
Reference north star (planning doc "Existing Tech on RPM / Whisper / Piper"): a local clone
of Google Astra / Meta AI with a locally-embodied agent — pluggable AI backend over an
OpenAI-style API, Whisper STT, Piper TTS, viseme lip-sync, on Quest 3 / XREAL / mobile.

## SOLVED — UE 5.5 renders on AMD Strix Halo (2026-07-16)

**The blocker was UE 5.8, not the AMD hardware.** After exhausting every AMD driver/firmware/
flag avenue (see the "DEFINITIVE" section below), research pointed at UE 5.8's known-fragile
Linux-Vulkan path (it crashes on NVIDIA/Linux too). We rebuilt the project on **UE 5.5** and it
**renders on the Radeon 8060S (RADV STRIX_HALO) without crashing** — container `Up` and stable,
`Player (DefaultStreamer) joined`, player page serving HTTP 200 on :8090, VP8 encode, `gemma4:e2b`
backend live. The whole pipeline works end to end on AMD on 5.5.

To keep both engines easy, the project is now **version-agnostic (5.5-5.8)**:
- `Target.cs` uses `BuildSettingsVersion.Latest` (compiles on any of 5.5-5.8).
- The 5.8-only plugins (Unreal-MCP, MetaHuman*) are marked `"Optional"` in the `.uproject`, so the
  same project opens in the 5.8 dev editor (with MCP) yet cooks clean on 5.5.
- ONE parameterized `Deployment/Dockerfile.amd` builds either: `--build-arg RELEASE=5.5` or `=5.8`.
- All companion code lives in a reusable `Plugins/CICompanion` plugin (drop into a 2nd Unreal project).

**Remaining for a talking avatar on 5.5:** the `DefaultCompanion` MetaHuman is a UE 5.8
`MetaHumanCharacter` asset, excluded from the 5.5 cook (that asset type doesn't exist in 5.5). The
render/stream/LLM pipeline all work; the avatar itself needs a 5.5-compatible MetaHuman (import via
5.5's MetaHuman plugin / Quixel Bridge) placed in a map, wired to the CICompanion character's
OnSetVisemeWeights/OnSetEmotion hooks. Dev that part in the 5.8 editor with the MCP, keep the
assets 5.5-importable.

## AMD Strix Halo render wall — DEFINITIVE (2026-07-15)

The containerized Pixel Streaming stack works end to end on core-2 (AMD Strix Halo,
Radeon 8060S) EXCEPT the GPU render, which crashes in RADV. Exhaustively ruled out,
every combination SIGABRT/SIGSEGVs in `libvulkan_radeon.so` mid-render:

- Mesa userspace: Ubuntu stock **26.0.3** (SIGABRT @0x15a79f) AND **git main / 26.1-dev
  compiled from source** (SIGSEGV, different offset). Two independent builds, two crashes.
- RADV backends: ACO and LLVM; debug flags nodcc / zerovram / nongg (nongg only *delays*
  it to ~416 frames). Feature levels SM6 and SM5. llvmpipe: UE won't init its RHI.
- **GPU firmware**: updated to upstream linux-firmware git and rebooted (twice — first boot
  loaded stale `.bin.zst` over the new `.bin`; fixed by recompressing). `amdgpu_firmware_info`
  confirms the loaded gfx1151 microcode versions are **identical** (PFP 0x32, MEC 0x23,
  IMU 0x0b352300) — the firmware was **already at the latest available version**. No change.
- Kernel: amdgpu 6.19.4 / 7.0.0-27-generic (already very new). No GPU ring-timeouts in dmesg
  (so it's a driver-code/userspace abort, not a microcode hang).

**Conclusion:** brand-new gfx1151 silicon + UE 5.8's Vulkan render path hits a RADV bug that
is not fixed in any currently-available Mesa (stable or git) or firmware. Working paths:
(a) an **NVIDIA render node** (NVENC + mature driver; `docker-compose.nvidia.yml` is ready) —
reliable today; (b) a mainline-git **kernel** on core-2 — last AMD lever, but low-probability
and boot-risky on a production host; (c) wait for RADV gfx1151 maturity / file a Mesa bug with
the deterministic repro. Everything below the render is proven: image build, non-root run,
Vulkan init on the 8060S, VP8 encode, PixelStreaming2, signalling on :8090, streamer
registration, ollama gemma4:e2b backend.

## Status snapshot (2026-07-14)

**Working, compiled, and unit-tested (62 tests: 57 backend + 5 frontend, all green):**

- **Containerized Pixel Streaming deployment** (`Deployment/`) — browser-accessible,
  GPU-rendered agent built on Epic's official Pixel Streaming container pattern:
  - `PixelStreaming2` plugin enabled in the `.uproject` (Win64/Linux).
  - `Deployment/docker-compose.yml`: `project` (packaged UE app, GPU) + Epic `signalling`
    server + `coturn` TURN + `oidc-proxy` (oauth2-proxy), all host-networked.
  - **CI-portal OIDC** — `oauth2-proxy.cfg` gates the stream behind ci-portal login; the UE
    app needs no auth code (auth is a proxy concern).
  - **CI-hub Ollama** — `FMetaHumanLLMEnvConfig` reads `CI_HUB_URL`/`CI_HUB_TOKEN`/
    `CI_HUB_MODEL` (+ `MHLLM_BACKEND`, `OLLAMA_*`, `WHISPER_SERVER_URL`, `PIPER_SERVER_URL`,
    `MHLLM_SYSTEM_PROMPT`) at module startup and configures the backend, so one image serves
    every environment via env vars. Points `UOpenAIChatClient` at the hub's `/v1` endpoint.
  - `Dockerfile` (multi-stage: package on `dev` image → `runtime-pixel-streaming`),
    `.env.example`, `run.sh`, repo-root `.dockerignore`, and `Deployment/README.md`.
  - **Not built/run in the authoring environment** (no GPU host / no Epic GHCR access there):
    manifests follow Epic's example + oauth2-proxy's documented OIDC config; validate the
    image build and live stream on a GPU host before shipping. A first Linux cook of a
    MetaHuman project may surface asset/plugin issues worth checking on a dev host.

- **Pluggable chat backends** (`Settings > Backend > BackendType`):
  - `UOllamaClient` — Ollama native `/api/chat`, NDJSON streaming.
  - `UOpenAIChatClient` — any OpenAI-compatible `/v1/chat/completions` with SSE streaming
    (`FOpenAIStreamParser`: `data:` / `[DONE]` / `choices[].delta.content`) and Bearer auth.
    This is the planning doc's "compatible with our CI servers or OpenAI or Claude or
    anything else" requirement: point `OpenAIBaseUrl` at Ollama's own `/v1` shim, the CI
    server, OpenAI, or any gateway. Backend chosen per-project in Settings; the orchestrator
    (`AMetaHumanCompanionCharacter`) is backend-agnostic via `IChatBackend`.
- **Whisper STT client** — `UWhisperSpeechToText` POSTs multipart audio to a whisper.cpp
  server (`/inference`) and parses the transcription. Request assembly + response parsing
  are unit-tested; live mic capture is the one remaining runtime binding (see below).
- **Piper TTS + amplitude lip-sync** — `UPiperTextToSpeech` POSTs text to a Piper HTTP
  server, decodes the returned WAV (`FWavAudioReader`, RIFF/WAVE PCM8/16), plays it via a
  procedural sound wave, and publishes a jaw-open timeline (`FVisemeEnvelope`, RMS-window
  loudness → `jawOpen`, the SALSA-style approach the doc references) on
  `OnVisemeEnvelopeReady`. This is the concrete backend for the `OnSetVisemeWeights` hook.
- **MetaHuman asset** — `Content/MetaHumans/DefaultCompanion` is a **fully rigged**
  MetaHuman Character (Rig State: Joints and Blend Shapes), created via the MetaHuman MCP
  and auto-rigged through Epic's cloud service. Saved to disk. (Resolves the prior
  "Unrigged / Login Failed" blocker — the fix was a one-time device-auth sign-in inside the
  Editor plus installing the MetaHuman Creator Core Data engine feature via Fab.)

## Remaining work — runtime bindings (need hardware / editor, not unit-testable)

1. **Live mic capture → Whisper.** `UWhisperSpeechToText::TranscribeWav(bytes)` is the
   functional entry point; a capture source (push-to-talk buffer, `UAudioCapture` submix
   recording, or an imported clip) must hand it a finished WAV on `StopListening`. Left as
   a subclass/Blueprint seam so the class stays free of the AudioCapture module.
2. **Audio-synced viseme playback.** `OnVisemeEnvelopeReady` currently emits the whole
   jaw-open timeline at once. `BP_CompanionCharacter` should walk it against playback time
   and forward each `jawOpen` value into the MetaHuman Face AnimBP via `OnSetVisemeWeights`
   (feed ARKit-named curves — `jawOpen` etc. — into the Face mesh's AnimInstance, letting
   the AnimBP's built-in RigLogic node evaluate; see OpenRigLogic note below).
3. **Wire real voice components onto BP_CompanionCharacter.** The C++ actor still defaults
   to the Null STT/TTS subobjects (safe, keeps tests hermetic). Swap to
   `UWhisperSpeechToText` / `UPiperTextToSpeech` on the Blueprint subclass, or make the
   defaults settings-driven like the chat backend.

## Remaining work — larger scope from the planning doc (not started)

- **Gaze** — avatar looking at the user / at objects the user is looking at.
- **Gestures + room-walking controller** — body animation mixer, navigating the space.
- **XR/MR deployment** — Quest 3, XREAL, and mobile packaging. Everything so far targets
  desktop/in-editor.
- **Passthrough camera vision / multimodal** — Quest 3 passthrough camera feed into a
  vision-capable model (doc references the Quest Passthrough Camera API samples and
  Florence2-style detection). Ollama's `images[]` field / OpenAI's image content parts are
  the wire format; the `UOpenAIChatClient` request builder would extend to multi-part
  content messages.

## OpenRigLogic — scoped, not integrated (unchanged conclusion)

`github.com/EpicGames/OpenRigLogic` (MIT, source-only) exists so tools *outside* Unreal can
evaluate a MetaHuman `.dna` rig. Inside UE5 every MetaHuman Face AnimBP already has a
`RigLogic` AnimGraph node (the same LiveLink Face uses), so vendoring a second copy would be
redundant. Correct integration point for viseme driving: feed ARKit-named curve values into
the Face skeletal mesh component's AnimInstance from the viseme timeline, letting the
embedded RigLogic node do the evaluation. Exact C++ curve-push API needs hands-on Editor
verification; now unblocked since a rigged MetaHuman and a viseme source both exist.

## Notes carried forward

- **Run-script hardening (2026-07-14).** `Scripts/RunTests.Common.ps1` had TWO false-pass
  bugs, both fixed: (1) a `Select-String` failure scan that silently reported a pass while a
  `Result={Fail}` sat in the log; (2) after those counts were added, a crash mid-suite (the
  editor dying partway) still reported "All N passed" because N tests had succeeded before
  the crash. Now it reads the log into memory and: fails on any crash marker (`appError
  called` / `Fatal error:` / `LowLevelFatalError` / `Assertion failed`), fails if
  `Test Started` count > `Test Completed` count (truncation), fails if any `Result={Fail}`,
  and fails if zero results (crash on boot / empty filter). Keep the script **ASCII-only** —
  Windows PowerShell 5.1 reads it as ANSI, so an em-dash breaks quote parsing (hit twice).
- **UObjects can't be stack-allocated in tests.** `EnvConfigSpec` initially did
  `UMetaHumanLLMSettings Settings;` on the stack, which fatal-errors in UObject construction
  and crashed the whole suite. Use `NewObject<UMetaHumanLLMSettings>(GetTransientPackage())`.
- **MetaHuman EULA.** MetaHuman assets may be rendered/animated freely but may **not** be
  used to train or enhance AI models. This project only renders/animates in response to LLM
  output — it never feeds MetaHuman geometry/animation into training — so it should be
  compliant, but flag to counsel before any commercial ship.
