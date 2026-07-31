# Inference stack — where cognition and voice should run

> Rewritten 2026-07-30. The first version of this document argued from a hardware assumption that
> was simply wrong: it treated the fleet as AMD-primary and reasoned outward from there. It is not.
> The Windows workstation is **NVIDIA**, and **most machines on the tailnet are NVIDIA**. beta-1
> and core-2 are the AMD exceptions, not the rule. That inverts a chunk of the reasoning, so the
> reasoning is rebuilt here rather than footnoted.
>
> Latency figures below are measured on our own hardware, not quoted from vendors. Everything else
> is labelled as claim, inference or recommendation — and there is a section near the end listing
> exactly what we have never measured, which currently includes **every NVIDIA machine we own**.

## The measurement that reframes the project

The companion was slow because it was thinking on the wrong machine, and thinking harder than it
needed to. Two independent factors, each worth roughly an order of magnitude.

| Model | Host | Thinking | Warm latency, one-line greeting |
|---|---|---|---|
| `gemma4:latest` (19.9 GB) | core-2 | n/a | **>60s** (unusable) |
| `gemma4:e2b` (7.2 GB) | core-2 | n/a | **~12s** (77s cold) |
| `gemma3:1b` | core-2 | n/a | <1s |
| `qwen3.6:27b` | beta-1 | on | **16–21s** |
| `qwen3:8b` | beta-1 | on | 2.8–4.5s |
| **`qwen3.6:27b`** | **beta-1** | **off** | **814ms** ← shipped |
| `qwen3:8b` | beta-1 | off | **262ms** |

Two conclusions, both counter-intuitive enough to be worth stating plainly:

1. **A 27B model is now faster than a 2B model was.** Moving from core-2's Strix Halo iGPU to
   beta-1's discrete Navi 31 pair turned a 12-second wait into 814ms *while more than tripling the
   parameter count*. Model size was never the bottleneck; the accelerator was.
2. **Chain-of-thought cost ~20× for nothing.** Ollama returns reasoning in a separate `thinking`
   field that never reaches the mouth, so every one of those seconds bought output no user and no
   viseme planner would ever see. Disabling it is a one-line request-body change.

Neither conclusion is about a GPU vendor, and that matters for the rest of this document. Both
would have come out identically on NVIDIA silicon: a discrete card with dedicated VRAM beats an
iGPU sharing system memory, and unread reasoning tokens are wasted everywhere. The vendor question
is a different question, and it is answered further down on different evidence.

## The voice loop, measured end to end

`Scripts/RunConversationE2E.py` runs whole conversation turns through
prompt → LLM → reply text → TTS → wav → STT → transcript and checks every joint. Ten turns,
prompts deliberately varied in *shape* (a long answer, a numeric one, a one-word-risk one):

| Stage | Service | Median |
|---|---|---|
| LLM | Ollama, `qwen3.6:27b`, thinking off | **1428ms** |
| TTS | Lemonade → Kokoro | **1166ms** |
| STT | Lemonade → Whisper | **66ms** |
| TTS→STT text similarity | — | **1.000** |

The LLM median of 1428ms sits above the 814ms in the table before it, and the two are not in
conflict: 814ms is a *one-line greeting*, while these prompts ask for one to two sentences. The
gap is generated-token count, not a regression — though note that is an inference from the prompt
set, not something we isolated with a controlled token count.

A similarity median of **1.000** means Whisper transcribed our own synthesis back word-for-word on
most turns. That is a weak test of speech *quality* — Whisper hears a clean signal with no room and
no microphone — and a strong test of *plumbing*: wrong sample rate, empty audio, a truncated clip
or a silently-500ing service all show up as garbled or missing text.

**Kokoro's WAV is not the shape UE expects, and that is a permanent tax.** It returns 24kHz mono
32-bit **float** with a `data` chunk size of `0xFFFFFFFF` — the streaming "length unknown"
placeholder. UE's audio mixer runs at 48kHz, so every clip is resampled on the way in.
`FWavAudioReader` had to learn two things to accept it at all: IEEE float format 3, and that
`0xFFFFFFFF` means "read to the end" rather than a negative length. Both are in
`Private/Voice/WavAudioReader.cpp` with the reasoning inline. This detail is the single most
concrete argument for looking at a 48kHz-native TTS eventually — see Voicebox below.

## The hardware, as it actually is

| Box / class | Accelerator | Stack | Role |
|---|---|---|---|
| **core-2** | Ryzen AI Max+ 395 "Strix Halo" iGPU, gfx1151, shared memory | Ubuntu 26.04, ROCm-era Mesa | Runs the UE app + Pixel Streaming. **Measured: not a cognition host.** |
| **beta-1** | 2× discrete Navi 31 (RX 7900-class), gfx1100, 2 × 25.7 GB VRAM | ROCm 6.4, Threadripper PRO 7985WX 64c/128t, 498 GB RAM | Cognition + voice. Ollama 0.30.10 and Lemonade Server. **Measured: excellent.** |
| **The NVIDIA nodes** | CUDA cards — the Windows workstation and most of the tailnet | CUDA | **Never benchmarked on this stack. Not one token.** |

Splitting render from inference is right regardless of vendor: core-2's GPU is already busy
rendering and software-VP8-encoding a live stream, and inference on the same silicon would contend
for it. That was true when we thought the fleet was AMD and it is still true now.

What changes with the correction is the *shape of the fleet*. We are not an AMD shop with an
NVIDIA exception; we are a mostly-NVIDIA fleet with two strong AMD boxes, one of which is
measurably serving a 27B model in 814ms and is already deployed, tunnelled and monitored. Those
two facts pull in opposite directions and the honest answer respects both.

## The real asymmetry is software, not FLOPS

The previous version of this doc leaned on a per-card figure — ROCm placed at
[~75–85% of CUDA](https://www.kunalganglani.com/blog/rtx-4090-vs-rx-7900-xtx-for-llm) for this class
of work — and treated that as the AMD-vs-NVIDIA argument. That was the wrong axis. A 15–25%
throughput difference is invisible next to the failure mode that actually costs us days: **a model
that ships CUDA kernels and nothing else.**

The asymmetry is in the software ecosystem, and it is real:

- Voice and vision projects overwhelmingly ship **CUDA first**. A prebuilt wheel with CUDA kernels
  lands on day one; ROCm support arrives later, or arrives as "build it from source", or arrives
  for datacentre gfx targets and not consumer ones.
- gfx1151 — core-2's iGPU — is the most exposed target we own. APU and consumer architectures are
  routinely the last to get official ROCm support, when they get it at all.
- gfx1100 — beta-1's discrete cards — is far better served, but "better served" still means
  reading a project's README for a ROCm section before assuming anything will run.

Getting this wrong looks like: pick a new phoneme aligner or emotion-conditioned TTS, discover the
CUDA extension has no ROCm path, spend a day on a source build against ROCm 6.4, and end up with
something slower than a CUDA node would have been out of the box. The cost is engineering time, not
tokens per second.

**Lemonade is the exception that proves the rule, and that is precisely why it was chosen.**
[Lemonade](https://lemonade-server.ai/) is AMD-sponsored and open source, so ROCm is its first
target rather than its third. It serves chat (llama.cpp on ROCm or Vulkan), Whisper STT
(whisper.cpp) and Kokoro TTS from **one OpenAI-compatible service**, and
[`lemonade-sdk/vllm-rocm`](https://github.com/lemonade-sdk/vllm-rocm/) ships portable vLLM+ROCm
builds targeting **both gfx110X (beta-1) and gfx1151 (core-2)** with no separate ROCm or PyTorch
install. Nothing in the CUDA world needed to exist for this to work on our AMD boxes, which is
exactly the property we were shopping for.

The same asymmetry cuts the other way in one place worth naming: Lemonade's whole reason for
existing is AMD. On an NVIDIA node we would not use it, and we would be back to either assembling
three services with three API shapes, or adopting something vendor-agnostic like Voicebox.

## Voicebox — the vendor-agnostic option, unmeasured

[Voicebox](https://github.com/jamiepine/voicebox) is MIT-licensed and is the most interesting
candidate for anything that has to run on *both* halves of a mixed fleet. Facts, none of them
measured by us:

- REST on `127.0.0.1:17493` — `/generate`, `/speak`, `/transcribe` — plus an MCP server. It is
  **not OpenAI-compatible**, which is a real cost: `UOpenAIChatClient` and the Kokoro client speak
  the OpenAI shape today, so adopting Voicebox means a new client class, not a config change.
- Backends: **CUDA / ROCm / DirectML / MLX / CPU**, with Docker deployment. That list is the point.
  A project that treats ROCm as a first-class backend alongside CUDA is one we can deploy on beta-1
  *and* on an NVIDIA node without maintaining two stacks.
- Its **LuxTTS** engine is **48kHz native** — which is UE's mixer rate, so the Kokoro resample
  disappears — needs ~1 GB VRAM, and runs at ~150× realtime on CPU. English-only, which is fine for
  this project and would not be for a product.
- Its **Qwen3-TTS** path supports delivery instructions ("speak slowly", "whisper"). That is
  directly interesting for a companion, where `UEmotionMapper` already derives an emotion that
  currently drives nothing.
- **Whisper Turbo is ~8× faster than Whisper Large** with minimal quality loss. Our STT median is
  already 66ms, so this matters for a future batch or multi-avatar workload, not for one user.

## Where each workload should live

| Workload | Node class | Why |
|---|---|---|
| **UE render + Pixel Streaming** | core-2 (AMD, today) | Deployed and working at 720p. But see below — this is the one workload with a *measured* AMD penalty, and it is a rendering penalty, not an inference one. |
| **LLM chat** | **beta-1 (AMD)** | Measured 814ms on a 27B model. Deployed, tunnelled, 51 GB of VRAM with idle capacity. Moving proven work to unmeasured hardware is a downgrade until someone measures. |
| **TTS (Kokoro)** | **beta-1, via Lemonade** | Measured 1166ms median. Shares one service and one API shape with STT; splitting it across boxes buys nothing and costs a tunnel. |
| **STT (Whisper)** | **beta-1, via Lemonade** | Measured 66ms median. Same argument. |
| **Anything CUDA-only** — new TTS engines, phoneme aligners, vision, research code | **An NVIDIA node** | Reasoning from ecosystem maturity, **not from local measurement.** If a component's README has no ROCm section, put it where its kernels already work rather than spending a day porting it. |
| **Hardware video encode** | **An NVIDIA node, untested** | HANDOFF Lesson 14: HW encode is NVENC or AMF; on AMD/RADV/Linux neither works, so we fall back to software VP8. NVENC exists on the fleet already and has never been tried for this. |
| **Concurrency / multi-avatar** | Open | Everything measured here is one user at a time. See PLAN-MULTI-AVATAR.md. |

The row that changed most with the hardware correction is the CUDA-only one. When the fleet was
assumed AMD, "this project only ships CUDA kernels" was a dead end that had to be worked around
with a source build or an alternative model. It is not a dead end any more — it is a scheduling
decision, and the answer is usually "run it on a node where it already works."

## What is deployed now

Ollama 0.30.10 on beta-1, GPU-backed, reached from core-2 through an SSH tunnel on
`127.0.0.1:11435`. Lemonade Server on beta-1 serving chat, Whisper and Kokoro, reached on
`127.0.0.1:13305`. Both are `--user` systemd units with lingering, so no root and they survive
reboot; see `Deployment/core2/` for the units and `Deployment/core2/README.md` for the
verify-with-a-status-code rule.

Two things about this deployment are easy to get wrong and cost real time when you do:

**Lemonade serves its API under `/api/v1/...`, not the `/v1/...` the OpenAI convention implies.**
Probing on beta-1 returned 405 (route exists, needs POST) on `/api/v1/chat/completions`,
`/api/v1/audio/transcriptions`, `/api/v1/audio/speech` and `/api/v1/embeddings`. A client pointed at
`/v1/...` gets 404 and looks like a dead service.

**The tunnels exist because the services bind to beta-1's loopback**, not because we like tunnels.
beta-1's `ollama.service` *declares* `OLLAMA_HOST=0.0.0.0:11434`, but the running process predates
that edit and is still bound to `127.0.0.1`; Lemonade publishes to `127.0.0.1` by container port
mapping. If Ollama is ever restarted with its own declared setting applied, delete the tunnel and
point `OLLAMA_BASE_URL` at `http://100.124.211.75:11434` directly.

## Why not the alternatives

**Move cognition to an NVIDIA node.** The tempting move now that the fleet is known to be mostly
NVIDIA, and the answer is still no — for one reason only, which is that we would be trading a
measured 814ms for an unmeasured number. beta-1's dual Navi 31 has 51 GB of VRAM, is serving a 27B
model inside conversational tolerance, and has idle capacity; the tunnels, units and E2E harness are
all built around it. None of that is worth discarding on the strength of an ecosystem argument that
does not apply to `qwen3.6:27b` on Ollama, which runs perfectly well on ROCm today. Revisit this the
day someone posts a measured NVIDIA number from `RunConversationE2E.py`.

**Keep hand-rolled Ollama + Piper + Whisper.** Partly what we have: Ollama does chat, Lemonade does
voice. Ollama's model management is genuinely pleasant and its ROCm path is delivering 814ms, so
there is no emergency. The case for collapsing chat into Lemonade too is one less service and one
API shape, not speed — and it should wait until it can be A/B'd against the 814ms.

**vLLM directly.** Highest throughput, and the right answer if this ever serves many concurrent
users — its advantage is continuous batching, which a single-companion workload never exercises. For
one user at a time it adds operational complexity for a benefit we would not measure. Reach for it
when there are many simultaneous visitors, not before.

**Buy hardware.** No, and the reasoning has changed even though the conclusion has not. The old
argument was "we only have AMD and it is fast enough." The real argument is better: it is fast
enough *and* the NVIDIA capacity we might want for CUDA-only components is already sitting on the
tailnet, unbenchmarked. Spend the measurement before spending money.

## What we have not measured

Stated plainly, because several recommendations above rest on it:

1. **No NVIDIA node has been benchmarked on this stack at all.** Not the workstation, not any
   tailnet node, not one token of LLM output and not one second of TTS. Every NVIDIA recommendation
   in this document is reasoning from **ecosystem maturity**, not from local measurement, and should
   be read at that strength.
2. **Lemonade's chat endpoint has never been timed with a model loaded** and compared like-for-like
   with Ollama's 814ms. Its voice endpoints are measured; its chat is only known to exist.
3. **Voicebox has never been installed on anything of ours.** Every claim about it above comes from
   its documentation. The 48kHz-native and 150×-realtime figures in particular are its numbers, not
   ours.
4. **The ROCm-vs-CUDA gap on our own models is unmeasured.** The 75–85% figure is someone else's
   benchmark on other cards, and it is cited here only to argue that it is the *wrong thing to
   optimise for*.
5. **Audio over Pixel Streaming is still unverified** (GAP-ANALYSIS.md). If it fails, the delivery
   route changes and some of the TTS reasoning here changes with it.
6. **Everything is single-user.** No concurrency, batching or multi-avatar load has been measured
   anywhere.

## Recommendation

1. **Keep beta-1 as the cognition and voice host.** It is measured, deployed and has idle capacity.
   Proven beats plausible.
2. **Do not move the working path to NVIDIA on ecosystem reasoning alone.** The ecosystem argument
   is about *new* components, and it does not apply to a stack that is already running.
3. **Benchmark one NVIDIA node — this is the highest-value unknown here.** The harness already
   exists: run the one-line-greeting test and `Scripts/RunConversationE2E.py` against a CUDA node and
   get a comparable number. Until that exists, half of this document is inference.
4. **Default new CUDA-first components to an NVIDIA node** rather than porting them to ROCm. Reach
   for the port only when the component must live next to something already on beta-1.
5. **Time Lemonade's chat endpoint** against the same one-line greeting before considering a chat
   migration. Collapsing three services into one is worth doing for operability, not at a latency
   cost we never measured.
6. **The strongest case for NVIDIA on this project is rendering, not inference.** Lesson 9 (UE 5.8
   SIGABRTs on RADV) and Lesson 14 (no hardware encoder, software VP8) are both AMD/Linux graphics
   problems, and both are now testable on hardware we already own without buying anything. That
   experiment is worth more than any inference benchmark.
