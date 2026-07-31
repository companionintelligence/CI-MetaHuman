# Gap Analysis — the virtual being

> Written 2026-07-28. Every claim below was checked against the running system or the source, not
> inferred from the design docs. Where something is unverified, it says so.

> **UPDATE 2026-07-28 (later the same day).** The headline gap below is FIXED: the companion is now
> instantiated at runtime and its replies drive the mouth. Blink and a text→viseme planner landed
> with it. Rows marked ✅ **NEW** in the matrix changed as a result. One claim below was also WRONG
> and is corrected in place: `gemma4:e2b` **does** exist on the host — I read that off a truncated
> listing. The real problem with it is latency, not absence.
>
> **VERIFIED LIVE 2026-07-30.** Cooked, deployed, and confirmed on the stream with `CI_HUB_URL`
> present and no workaround, so the backend-precedence fix is proven in a shipped build rather
> than only in unit tests:
> ```
> Companion spawned and bound (backend=ollama model=gemma3:1b).
> Companion replied (58 chars): Well hello there! Welcome to my little corner of the past.
> Speaking: 52 viseme spans over 5.59s.
> ```
> Round trip 0.63s. Face curves resolve 16/16. 141 backend tests pass, 0 fail.

## The headline

**The shipped build contains no companion.** Everything works except the being.
*(Fixed — see the update above; kept for the record because it framed the work.)*

The diorama renders, the resident stands in it with real materials and a looping idle, and the
stream serves at 720p. But `AMetaHumanCompanionCharacter` — the class that owns a conversational
turn — is **never instantiated anywhere in the cooked game**. It is referenced only by its own
`.cpp` and the HUD widget. Nothing spawns it, and the Python-authored map cannot: it is authored in
a content-only project that cannot reference plugin C++ (Lesson 6).

So the LLM client, the streaming parser, the emotion mapper, the STT/TTS components and their 57
passing tests are, in the shipped artifact, **dead code**. Worse, until today the cook tree
(`cook-m2`) had no `Plugins/` directory at all, so none of it was even compiled into the build.

This reframes the roadmap. The gap is not "add speech to a working companion" — it is "there is no
companion process running; the diorama is a renderer with a mannequin in it." The good news is that
the missing piece is small and structural rather than a body of unwritten features.

## Capability matrix

| Capability | State | Evidence |
|---|---|---|
| **Body / presence** | ✅ Working | Nia renders with 23 authored PBR material slots, correct scale and facing. |
| **Idle animation** | ✅ Working | `MF_Idle` on the component; verified by diffing two live frames (6.4% of her pixels changed). |
| **Face rig** | ✅ Capable | 286 morph targets, all 15 CC visemes, full FACS expression set. `Docs/reference/nia-morph-targets.txt`. |
| **Face driver** | ✅ Proven in the cooked build | `UCompanionFaceComponent` + `UCompanionFaceSubsystem`. Cooked and deployed 2026-07-28: `Face profile 'CC': all 14 curves resolved on 'SK_Nia'` — morph targets **survive the cook**, which was the real risk. Self-test ran; mouth shape varies across captured frames. |
| **Viseme mapping** | ✅ **NEW** Working | Viseme→curve for CC and ARKit, plus `FVisemePlanner` text→viseme with digraph collapsing, vowel/consonant duration, punctuation silence and cross-fade. 17 tests. |
| **Phoneme timing** | ⚠️ **NEW** Estimated, not aligned | `FVisemePlanner` times spans from a speaking rate — good enough to read as speech, but it will drift against real audio. `PlanForDuration` rescales to a measured clip once TTS exists. True phoneme alignment still needs the patched Piper (ANIMATION-PLAN §1.2). |
| **Speech out (TTS)** | ⚠️ Code only | `UPiperTextToSpeech` → `USoundWaveProcedural` → `AudioComponent` exists. **No Piper service deployed** (`PIPER_SERVER_URL=` empty). Audio over Pixel Streaming never verified. |
| **Hearing (STT)** | ⚠️ Code only | `UWhisperSpeechToText` exists and is tested. **No Whisper service deployed** (`WHISPER_SERVER_URL=` empty). |
| **Cognition (LLM)** | ⚠️ **CORRECTED** Working but slow | Ollama is up and reachable (host networking). `gemma4:e2b` **exists** (7.2 GB) — my earlier "missing" claim was read off a truncated list. Measured: `gemma3:1b` <1s, `gemma4:e2b` ~12s warm / ~77s cold, `gemma4:latest` >60s warm. Deploy now defaults to `gemma3:1b` for responsiveness. |
| **Persona** | ✅ Exists | `MetaHumanLLMSettings::SystemPrompt`, seeded per conversation. |
| **Short-term memory** | ✅ Exists | `UOllamaConversation` holds turn history in memory. |
| **Long-term memory** | ❌ Missing | No integration with anything. `ci-memory` runs on the same host, unused. Every session starts blank. |
| **Emotion → face** | ❌ Missing | `UEmotionMapper` derives emotion from text, but nothing applies it: `OnSetEmotion` is a Blueprint event with no Blueprint, and `FaceProfile` has no emotion shapes yet — though Nia has the FACS poses for them. |
| **Eyes / gaze / blink** | ⚠️ **NEW** Blink done, gaze missing | Randomised 2–6s blink with asymmetric close/open, composited over speech. Gaze/saccades (ANIMATION-PLAN §2) still unstarted. |
| **Micro-motion / breathing** | ❌ Missing | ANIMATION-PLAN §3 unstarted. |
| **Gesture / body language** | ❌ Missing | `EAnimationCategory` exists as an enum; no animation set is wired to it. |
| **User input** | ❌ Missing | Known bug #23: `DefaultPawn` ignores `PlayerStart`, free-look inert. No text or audio channel from browser to app. **Nobody can address her.** |
| **Turn-taking / barge-in** | ⚠️ Code only | `SubmitUserText` cancels in-flight responses. Unreachable without an input path. |
| **HUD** | ⚠️ Code only | `UCompanionHUDWidget` is a UMG widget nothing creates or adds to a viewport. |
| **Delivery** | ✅ Working | Pixel Streaming on AMD/RADV, software VP8, `:8090`. |
| **Reproducibility** | ⚠️ Improving | Cook now runs from a clean tree of committed source; `cook-m2` had drifted so far it lacked the plugin entirely. |
| **Durability** | ❌ Fragile | Docker state on core-2 has been fully destroyed several times (`docker system prune --volumes --all --force` appears repeatedly in shell history) plus reboots. No restart policy; restoring costs an 85 GB pull and a 20-minute cook. |

## The gaps that matter, in order

**~~1. Nothing instantiates the companion.~~ DONE.** `UCompanionRuntimeSubsystem` spawns the
orchestrator at `BeginPlay`, hidden below the floor as a pure brain, and binds `OnCompanionSpoke`
to the face driver. Original text follows.

**1. Nothing instantiates the companion.** Everything else is downstream of this. The fix is the
same pattern the face driver just proved: a runtime subsystem that spawns/owns the orchestrator at
`BeginPlay` and binds it to the tagged resident, so the map stays purely visual. Small, structural,
and it turns ~2,000 lines of tested dead code into a running agent.

**2. No input path.** She cannot be addressed. Pixel Streaming has a data channel; the smallest
useful version is text from the browser to `SubmitUserText`. Without this there is no interaction
loop regardless of how good the face gets. Fixing #23's camera/pawn problem is related but separable.

**3. No voice services deployed.** Piper and Whisper are configured by empty env vars and no
containers exist. Piper is the harder one and it is on the critical path twice — for audio *and*,
per ANIMATION-PLAN's thesis, for the phoneme timing that drives the mouth.

**4. The LLM model name is wrong.** A one-line fix, but as configured the first turn errors.

**5. Phoneme→viseme is unwritten.** The face can be driven; nothing yet decides *what* to drive it
with. This is the actual lip-sync work, and it is well specified in ANIMATION-PLAN §1.

**6. No eyes.** Blinks and saccades are cheap, need no audio pipeline, and buy more perceived
aliveness per hour of work than anything else on this list. A face that never blinks reads as a
corpse even when the mouth is perfect.

## Recommended sequence

Each step is independently verifiable in pixels or logs, and none blocks on a service that is not
yet deployed except where noted.

1. **Prove the face moves** (in flight) — self-test cook, capture, look.
2. **Spawn the companion** — runtime subsystem owning `AMetaHumanCompanionCharacter`, bound to the
   tagged resident. Fix `OLLAMA_MODEL` at the same time. Verify: a log line showing a real model
   reply to a hardcoded prompt at startup.
3. **Eyes** — procedural blink + saccade on the existing morphs. Verify: frame diff on the eye
   region. Cheap, and it makes every subsequent demo look alive.
4. **Input channel** — Pixel Streaming data channel → `SubmitUserText`. Verify: type in the browser,
   see the reply in the log.
5. **Deploy Piper, wire audio** — verify audio actually reaches the browser, which is unproven and
   is a genuine risk in a WebRTC/VP8 path that has only ever carried video.
6. **Phoneme→viseme** — surface Piper's alignments, implement `FVisemeTrack`, drive the face
   driver. Verify: mouth shapes correlate with speech, not just movement.
7. **Emotion → face**, then long-term memory via the `ci-memory` service already on the box.

## Honest risks

- **Audio over Pixel Streaming is unverified.** If it does not work, the whole speech path needs a
  different delivery route (e.g. audio over the data channel, played by the browser). Worth testing
  early — step 5 could invalidate assumptions in steps 6–7.
- **Piper alignment needs a patched model** per ANIMATION-PLAN §6; that is called out there as the
  linchpin of the phoneme approach and has not been validated.
- **The environment is not durable.** Any of this can be wiped between sessions. Until the cooked
  image is pushed to a registry or saved to disk, every wipe costs hours.
- **Nia's licence says "Allows usage with AI: No."** The user has stated licensing is handled; it is
  recorded here only because this is a document about an AI-driven character.
