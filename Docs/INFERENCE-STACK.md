# Inference stack — where cognition and voice should run

> Written 2026-07-30. Latency figures are measured on our own hardware, not quoted from vendors.
> Everything else is marked as claim or recommendation.

## The measurement that reframes the project

The companion was slow because it was thinking on the wrong machine, and thinking harder than it
needed to. Two independent factors, each worth roughly an order of magnitude.

| Model | Host | Thinking | Warm latency, one-line greeting |
|---|---|---|---|
| `gemma4:latest` (19.9 GB) | core-2 | n/a | **>60s** (unusable) |
| `gemma4:e2b` (7.2 GB) | core-2 | n/a | **~12s** (77s cold) |
| `qwen3.6:27b` | beta-1 | on | **16–21s** |
| `qwen3:8b` | beta-1 | on | 2.8–4.5s |
| **`qwen3.6:27b`** | **beta-1** | **off** | **814ms** ← shipped |
| `qwen3:8b` | beta-1 | off | **262ms** |

Two conclusions, both counter-intuitive enough to be worth stating plainly:

1. **A 27B model is now faster than a 2B model was.** Moving from core-2's Strix Halo iGPU to
   beta-1's discrete RDNA3 turned a 12-second wait into 814ms *while more than tripling the
   parameter count*. Model size was never the bottleneck; the accelerator was.
2. **Chain-of-thought cost ~20× for nothing.** Ollama returns reasoning in a separate `thinking`
   field that never reaches the mouth, so every one of those seconds bought output no user or
   viseme planner would ever see. Disabling it is a one-line request-body change.

## The hardware

| Box | Accelerator | gfx | VRAM | Verdict |
|---|---|---|---|---|
| **core-2** | Radeon 8060S iGPU (Strix Halo) | gfx1151 | shared | Renders the stream well. **Not a cognition host.** |
| **beta-1** | 2× Navi 31 (RX 7900-class) | gfx1100 | 2 × 25.7 GB | ROCm 6.4. **This is where inference belongs.** |

Splitting them is the right architecture regardless of software: core-2's GPU is already busy
rendering and VP8-encoding a live stream, and inference would contend for the same silicon.

## What is deployed now

Ollama 0.30.10 on beta-1, already installed and GPU-backed, reached from core-2 through an SSH
tunnel on `127.0.0.1:11435` (`Deployment/core2/ollama-tunnel.service`, a `--user` systemd unit with
lingering, so no root and it survives reboot).

The tunnel exists for a specific reason worth remembering: beta-1's `ollama.service` **declares**
`OLLAMA_HOST=0.0.0.0:11434`, but the running process predates that edit and is still bound to
`127.0.0.1`. Applying the operator's own setting needs a service restart with root on a box that
serves other work. If that service is ever restarted, delete the tunnel and point
`OLLAMA_BASE_URL` at `http://100.124.211.75:11434` directly.

## AMD Lemonade Server — recommended next, and now trialled

[Lemonade](https://lemonade-server.ai/) is AMD-sponsored and open source, and it is a better fit
for this project than assembling the stack by hand.

**Trialled on beta-1, 2026-07-30** — `ghcr.io/lemonade-sdk/lemonade-server:latest` (759 MB) pulled
and run in Docker with ROCm passthrough (`--device /dev/kfd --device /dev/dri`, render/video
groups), bound to `127.0.0.1:13305` so it is not exposed off-box. It came up healthy and correctly
detected the hardware: 498.9 GB RAM, a 24 GB GPU memory pool, no NPU, no NVIDIA.

Endpoints verified by probe on our own hardware (405 = route exists but requires POST; 404 would
mean absent):

| Endpoint | Result | |
|---|---|---|
| `/api/v1/chat/completions` | 405 | chat ✓ |
| `/api/v1/audio/transcriptions` | 405 | **Whisper STT ✓** |
| `/api/v1/audio/speech` | 405 | **Kokoro TTS ✓** |
| `/api/v1/embeddings` | 405 | embeddings ✓ |

So the "one service for chat + STT + TTS" claim is confirmed on the box we would deploy it on, not
merely on a product page. Note the API is served under `/api/v1/...`, **not** the `/v1/...` the
marketing implies — worth knowing before pointing a client at it. The container is left running and
idle (no models downloaded, so it holds no VRAM); remove with
`docker rm -f lemonade-trial` on beta-1.

Backends and endpoints:

- `/api/v1/chat/completions` — llama.cpp with **ROCm or Vulkan**, or ONNX/OGA on NPU
- `/api/v1/audio/transcriptions` — **Whisper** (whisper.cpp: ROCm/Vulkan/NPU/CPU)
- `/api/v1/audio/speech` — **Kokoro TTS**

Why that matters here specifically: `Docs/GAP-ANALYSIS.md` lists "no Whisper deployed" and "no
Piper deployed" as separate open gaps. Lemonade closes **both, plus chat, in a single deployment**,
on hardware we already own, from a vendor we already standardise on. It also removes the need to
run and monitor three services with three different API shapes.

It covers both our GPUs. [`lemonade-sdk/vllm-rocm`](https://github.com/lemonade-sdk/vllm-rocm/)
ships portable vLLM+ROCm builds targeting **gfx110X (beta-1's RX 7900) and gfx1151 (core-2's Strix
Halo)** with no separate ROCm or PyTorch install, and Lemonade downloads, launches and routes to it
automatically. A Docker image exists, which suits core-2's compose-based deployment.

**Migration cost is not zero, and it is not symmetrical:**

| Piece | Effort | Why |
|---|---|---|
| Chat | **~none** | `UOpenAIChatClient` already speaks this shape; set `MHLLM_BACKEND=openai` and point `CI_HUB_URL` at Lemonade. |
| STT | small | `UWhisperSpeechToText` posts multipart to a whisper.cpp server; Lemonade's endpoint is the OpenAI shape. Adaptation, not a rewrite. |
| TTS | **new class** | `UPiperTextToSpeech` speaks Piper's own JSON API. Kokoro at `/v1/audio/speech` is a different contract — a sibling implementation of the existing TTS interface. Kokoro is also the better-sounding voice, so this is an upgrade, not just plumbing. |

## Why not the alternatives

**Keep hand-rolled Ollama + Piper + Whisper.** What we have. Works, and Ollama's model management
is genuinely pleasant. But it is three services, three APIs, two of them not yet deployed, and
Ollama's ROCm support is [reported as the less reliable AMD path in 2026](https://idfs.ai/blog/amd-gpus-for-ai-inference-2026)
versus llama.cpp/vLLM. We are getting 814ms from it today, so there is no emergency.

**vLLM directly.** Highest throughput, and the right answer if this ever serves many concurrent
users — its advantage is continuous batching, which a single-companion workload never exercises.
For one user at a time it adds operational complexity for a benefit we would not measure. Reach for
it when there are many simultaneous visitors, not before.

**NVIDIA.** Straightforwardly faster per card — ROCm is generally placed at
[~75–85% of CUDA](https://www.kunalganglani.com/blog/rtx-4090-vs-rx-7900-xtx-for-llm) for this class
of work — and CUDA remains the smoother path. But we are already answering in **814ms with a 27B
model**, comfortably inside conversational tolerance, on hardware that is bought and installed.
Buying NVIDIA to fix a latency problem we no longer have would be spending money for headroom
nothing currently needs. The honest case for NVIDIA here is not inference speed; it is that UE 5.8
crashes on RADV (`HANDOFF.md` Lesson 9) and Pixel Streaming has no hardware encoder on this AMD box
(Lesson 14) — those are *rendering* pains, and they are not solved by an inference purchase either.

## Recommendation

1. **Keep Ollama on beta-1 for now.** 814ms is good, it is deployed, and it is verified.
2. **Lemonade is trialled and its endpoints verified** (above). The remaining unknown is not
   whether it exposes chat + Whisper + Kokoro — it does, on our hardware — but whether it matches
   Ollama's 814ms once a model is actually downloaded and loaded. Pull one model into it and
   re-run the same one-line-greeting benchmark before committing to a migration.
3. **Do the TTS client against Kokoro rather than Piper** if that trial succeeds — it is the same
   amount of work as wiring Piper, for a better voice and one less service.
4. **Do not buy NVIDIA for cognition.** Revisit only for the rendering problems, and only with
   Lesson 9 and Lesson 14 measured on a real card first.
