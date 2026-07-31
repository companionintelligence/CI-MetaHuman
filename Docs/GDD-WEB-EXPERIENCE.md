# Eliza — Design Document
### The Companion Intelligence AI assistant, as a web experience

> Written 2026-07-31. Describes the experience as designed AND records what is actually built,
> because the two differ and the difference is the roadmap. Every claim marked ✅ has been
> observed running; ⚠️ is built but unproven; ❌ is not built. Nothing here is aspirational
> unless it says so.

---

## 1. The pitch

You open a link. You are looking down a 1970s mainframe hall — orange back wall, ranks of tape
cabinets, the hum of transformers. The camera moves through it and settles behind a woman. She
turns to face you and says:

> *"Welcome to Companion Intelligence, I'm Eliza. How can I help you?"*

Then you talk to her, and she answers — running entirely on hardware in the room next door.

**The thesis in one line: an AI assistant should be somebody, somewhere, not a text box.**

Eliza is named for Weizenbaum's 1966 ELIZA. That is the joke and also the argument: the first
program people confided in ran on a mainframe, and sixty years later we are still deciding what
it should feel like to talk to one. Putting her in that room makes the lineage the set dressing.

## 2. Why this shape

| Decision | Reason |
|---|---|
| **A person, not a chat window** | A face gives turn-taking, attention and warmth for free. People already know how to talk to somebody who is looking at them. |
| **Pixel Streaming, not WebGL** | Real-time rendering of a photoreal human is not a browser workload. Streaming moves it to a GPU we control and leaves the client a `<video>` tag — a phone gets the same fidelity as a workstation. |
| **Local models** | Companion Intelligence's product claim is that your companion is *yours*. A cloud API would contradict it. Everything runs on the tailnet: Ollama on beta-1, Kokoro and Whisper via Lemonade, nothing leaves. |
| **1970s machine room** | Distinguishes the product from every white-void assistant, and earns the name. It also solves a practical problem: a period room full of hard surfaces justifies the ambient hum that covers streaming artefacts. |
| **Portrait 9:16** | The experience is a *call*. Phones are the majority case and portrait is what a call looks like there. |

## 3. The opening — shot by shot ⚠️ OPT-IN

> **The default deployment no longer plays this.** `MHLLM_OPENING_SHOT` defaults to **0**, so a
> normal run lands straight in the static call framing. The move below is what you get with
> `MHLLM_OPENING_SHOT=1`, which is the launch-stream configuration. The default flipped because a
> moving camera is a surprise for anyone iterating on conversation, and because the solved call
> framing should not require an undocumented variable to reach.

Authored as timed beats in `UCompanionStageDirector`, not as a Sequencer asset (§8.2 explains
why). Times are configurable; defaults shown.

| t | Beat | Purpose |
|---|---|---|
| 0.0s | Hold, camera deep in the hall behind her | Establishes the room before the person. Pixel Streaming clients take ~10s to negotiate, so a delay here is what stops the opening playing to an empty room. |
| 3.0s | Camera begins its move down the aisle | The set is the first character. |
| 14.0s | Camera settles behind her shoulder | She is unaware; the viewer is a guest who has just walked in. |
| 14.0s | She turns to the lens | The moment of address. This is the beat the whole opening exists for. |
| 16.0s | *"Welcome to Companion Intelligence, I'm Eliza. How can I help you?"* | Verbatim, always. |
| 16.0s+ | Hands over to live conversation | The shot ends; the assistant begins. |
| every 40s | Replays while idle | A live stream has no other way to show its opening to someone who arrives late. |

**Typing interrupts it.** A visitor who speaks first is answered immediately and the camera cuts
to live framing. Making someone wait out a camera move after they have spoken is the rudest
thing an assistant can do.

## 4. The conversation ⚠️

```
you type ──▶ HUD ──▶ Ollama (beta-1, qwen3.6:27b) ──▶ reply text
                                                        │
                          ┌─────────────────────────────┤
                          ▼                             ▼
                    Kokoro TTS                   FVisemePlanner
                    (Lemonade, ROCm)          text ──▶ viseme track
                          │                             │
                          ▼                             ▼
                    audio clip ─── measured duration ──▶ re-time
                          │                             │
                          ▼                             ▼
                  Pixel Streaming audio         morph targets on SK_Nia
```

Measured end to end on beta-1, 10 scripted turns:

| Stage | Median | Note |
|---|---|---|
| LLM (`qwen3.6:27b`) | **1428ms** | 814ms for a one-liner. Thinking disabled — it costs 16–21s and never reaches the mouth. |
| TTS (Kokoro) | **1166ms** | 131–184 wpm depending on the line. |
| STT (Whisper) | **66ms** | Not yet wired to input. |
| Round-trip fidelity | **1.000** | TTS→STT similarity; what she says is what comes back. |

## 5. Presence — the details that sell it

| Element | State | Notes |
|---|---|---|
| Photoreal character | ✅ | Reallusion CC "Nia", 23 authored PBR material slots, real skin shading. |
| Idle animation | ✅ | Baked `MF_Idle`; the Control Rig is a Blueprint and would not survive the Linux cook. |
| Lip-sync | ✅ | 15 CC viseme morphs, cross-faded, timed to measured audio length. |
| Blinking | ✅ | Randomised 2–6s, asymmetric close/open. Its absence is what makes a good render read as a mannequin. |
| Score | ⚠️ | 32s machine-room loop at −27dBFS: 50Hz mains hum + harmonics, fan noise, minor-9th drone. Synthesized (§8.4). |
| Branding | ⚠️ | Facility wordmark on the back wall, in the register of the era. |
| Emotion → face | ❌ | `UEmotionMapper` derives it; nothing applies it. The FACS poses exist on the rig. |
| Gaze / saccades | ❌ | She blinks but does not look around. |
| Breathing / weight shift | ❌ | ANIMATION-PLAN §3. |

## 6. What is NOT built ❌

Listed plainly because a design document that only describes the good parts is a brochure.

1. **Voice input.** Whisper is deployed and fast; nothing captures microphone audio from the
   browser and routes it in. Today Eliza is talked to by typing.
2. **Audio has never been verified over Pixel Streaming.** The client is written, the WAV parser
   handles Kokoro's format, and the track negotiates — but no capture has yet measured non-zero
   energy arriving in a browser. Until it does, treat "she speaks" as unproven. This path has
   already produced one silent track and one segfault.
3. **Memory.** Every session starts blank. `ci-memory` runs on the same host, unused.
4. **Multi-user.** One stream, one visitor. A second connection joins the same camera.
5. **Mobile-native controls.** Portrait framing is done; touch input is not.
6. **Barge-in.** She cannot be interrupted mid-sentence by voice, only by typing.

## 7. Experience principles

**She should never be silent and still at the same time.** Silence is fine; stillness is fine;
together they read as "crashed". Blink and the score exist to hold the floor while nothing else
is happening.

**Latency is a design material, not a bug to be hidden.** A 1.4s reply is not instant, so the
experience must fill that time — the HUD shows "thinking…", and her idle continues. Pretending
the gap does not exist is what makes assistants feel broken.

**Never fake understanding.** If the model returns nothing, she says so. A confident wrong answer
from something with a human face is worse than from a text box, because the face buys trust the
answer has not earned.

**The room is the trust argument.** Everything in shot is period hardware; nothing suggests a
data centre in another country. The set is a continuous, wordless statement that this runs here.

## 8. Technical architecture

### 8.1 Topology

```
 Browser ──WebRTC── core-2 (Strix Halo iGPU)          beta-1 (2× Navi 31, ROCm 6.4)
                    ├─ UE 5.5 packaged app           ├─ Ollama  :11434  qwen3.6:27b
                    ├─ Pixel Streaming signalling    └─ Lemonade :13305 Kokoro + Whisper
                    └─ SSH tunnel :11435 ─────────────────────▶
```

Rendering and cognition are split deliberately. core-2's iGPU renders acceptably but manages
only ~12s per reply as an LLM host; beta-1 answers in well under a second but is not where the
stream lives. Each box does the thing it is good at.

### 8.2 Why behaviour lives in C++ subsystems, not Blueprints

The map is authored **headlessly by Python in a content-only project** that cannot reference
plugin C++ or embed a Blueprint. So the level is a purely visual asset, and everything that acts
is attached at `BeginPlay` by world subsystems:

- `UCompanionFaceSubsystem` — finds the resident by **actor tag** (labels are editor-only and do
  not survive a cook) and attaches the face driver
- `UCompanionRuntimeSubsystem` — spawns the conversational actor, owns the HUD, routes turns
- `UCompanionStageDirector` — runs the opening beats

This is not a stylistic preference; it is the only architecture that survives the pipeline.

### 8.3 Rig-agnostic face

`EViseme` is a vendor-neutral vocabulary. `FFaceProfile` maps it to concrete curve names, with
**CC** (Nia) and **ARKit** (MetaHuman) profiles. Nia carries 0 of 10 sampled ARKit names and all
15 CC visemes, so this split is load-bearing rather than speculative.

### 8.4 The score is generated, not licensed

`Scripts/MakeAmbience.py` synthesizes the loop from stdlib. Three reasons: no licence question
about shipping it, it can be tuned to this specific room, and — the real one — every partial is
placed at an **exact integer number of cycles per loop**, so the seam is sample-accurate and
needs no crossfade. Measured discontinuity: 0.0009.

## 9. Roadmap

**Next** — prove audio actually arrives in a browser; wire microphone → Whisper; emotion → face.

**Then** — gaze and saccades; memory via `ci-memory`; barge-in.

**Later** — multi-user; a second character; the room reacting to conversation (panel lamps on
activity).

## 10. Open questions

- **Does the uncanny valley bite?** A photoreal human who is *almost* right can be worse than a
  stylised one. Untested with real users.
- **Is 1.4s conversational?** It feels close. It has not been tested with anyone who was not
  waiting for it.
- **Does the opening wear out?** It replays every 40s, which is right for a kiosk and possibly
  wrong for a returning user.
- **Is the dress right?** She currently wears a cocktail dress and heels in a machine room. It is
  a striking image and arguably the wrong one for an enterprise assistant. Art-direction call.

---

*Companion Intelligence — Eliza. See also `Docs/GAP-ANALYSIS.md` (what is real), `Docs/GDD.md`
(the diorama), `Docs/ANIMATION-PLAN.md` (viseme pipeline).*
