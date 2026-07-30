# Product gap analysis — what is missing to ship this as a product

> Written 2026-07-30, superseding the engineering-level `GAP-ANALYSIS.md` from earlier the same
> day (which asked "why doesn't the being work?" — that is now largely answered). This one asks a
> different question: **what stops a person other than us from using this?**
>
> Every "have" below was verified against the running system today. Every "missing" is stated as a
> gap, not a plan.

## Where the product actually is

She renders, thinks and moves her mouth. A visitor at `:8090` sees a photoreal resident in a 1970s
machine room who greets them unprompted, in a portrait video-call frame, with lip movement in time
with her reply — cognition on beta-1's RDNA3 at ~1.1s per turn.

That is a **demo**, not a product. The distance between the two is almost entirely in the list
below, and the single largest item is not a feature at all: **nobody can talk back to her.**

## 1. The blocking gap: there is no way in

| | |
|---|---|
| **State** | She speaks. She cannot be spoken to. |
| **Why it matters** | Every other feature is downstream. Memory, emotion, persona and voice are all invisible to a visitor who can only watch. |
| **What exists** | `SubmitUserText()` on the companion; `UCompanionRuntimeSubsystem::SubmitUserText` as a single entry point; Pixel Streaming's data channel is open and already carries input events. |
| **What is missing** | Nothing routes browser → app. No text box, no mic button, no data-channel handler. |
| **Cheapest real version** | A text input on the player page → PS data channel → `SubmitUserText`. Hours, not days, and it converts a demo into a conversation. |

Everything after this point assumes that gap is closed, because none of it is reachable without it.

## 2. She has no voice yet, and the last mile is unproven

TTS and STT now work as *services* — verified over 10 turns, Kokoro 1166ms and Whisper 66ms median,
round-trip transcription accuracy 1.000. `UKokoroTextToSpeech` is implemented and the WAV decoder
now survives real Kokoro output (it previously segfaulted on it).

**But no audio has ever reached a browser.** Pixel Streaming on this stack has only ever carried
video. Until one clip plays through it, the entire voice path is theoretical, and if it fails the
fix is a reroute (e.g. play TTS in the browser from the audio bytes) rather than a patch. This is
the highest-risk unknown remaining and it is a ~1 hour experiment.

Also missing: **barge-in**. `SubmitUserText` cancels an in-flight LLM response, but nothing stops
audio already playing, so a visitor who interrupts will be talked over.

## 3. She forgets everyone, immediately

`UOllamaConversation` holds turn history in RAM for one session. Close the tab and she has never
met you. There is no identity, no persistence, no recall.

`ci-memory` is already running on the same host, unused. For a *companion* — the actual product
premise — this is arguably a more serious gap than voice: a companion that cannot remember you is a
chatbot with a face.

## 4. She has one expression and no gaze

| Capability | State |
|---|---|
| Visemes | ✅ 15 CC morphs driven from text |
| Blink | ✅ randomised 2–6s |
| Emotion → face | ❌ `UEmotionMapper` derives emotion from text; **nothing applies it**. `FaceProfile` has no emotion shapes, though Nia carries the full FACS set (`Brow_*`, `Mouth_Smile_*`, `Cheek_Raise_*`). |
| Eye contact / gaze | ❌ Her eyes never track the camera. In a video-call framing this is the single most visible absence — a person who never looks at you reads as absent. |
| Head motion | ❌ Idle animation only; she does not nod, tilt or orient toward the speaker. |
| Gesture | ❌ `EAnimationCategory` is an enum with no animation set behind it. |

The rig can already do all of this. The gap is wiring, not assets — which makes it unusually cheap
per unit of perceived aliveness. **Gaze first**: it costs least and buys most.

## 5. Nothing is observable, and one visitor is the limit

- **No metrics.** Turn latency, failure rate and audio underruns are visible only by reading
  container logs over SSH. There is no dashboard and no alert; the stream can be down and nobody
  knows until someone looks.
- **Single session.** One UE process, one streamer, one viewer. A second visitor shares the same
  camera and the same conversation. Multi-tenancy is not a tuning exercise — it is a second UE
  instance per viewer, or a redesign.
- **No auth.** `:8090` is open on the tailnet. The OIDC gate has never worked: the issuer was
  wrong, and `CI_HUB_TOKEN` is still the literal placeholder `__NEEDS__` (`HANDOFF.md` Lesson 15).
- **No cost/roll-back story.** Deploying is a 25-minute cook; reverting is another. There is no
  "ship the previous build" button.

## 6. The environment eats itself

Not a feature, but it directly costs delivery speed. `docker system prune --volumes --all --force`
appears repeatedly in core-2's shell history and has destroyed, more than once: the 85 GB engine
image, the cooked application, the running stream, and the fast-preview image. Each occurrence
costs roughly half an hour before any work can resume.

Partly mitigated today — the preview image is now archived as a 17 GB tarball that prune cannot
touch — but the cooked app and engine image are still exposed, and nothing pins the deployment to
survive a reboot.

## 7. Art direction is close but not finished

The portrait framing and the machine hall now read correctly. Remaining:

- **Props render pale.** The GLB cabinets import washed out, where the reference is dark charcoal
  with blue panels and chrome trim. This is the long-standing issue #22 and is the biggest single
  gap between our frame and the reference.
- **Wardrobe mismatch.** A white cocktail dress and heels in a 1970s machine room. Deliberate
  (it is Nia's shipped outfit) but it undercuts the premise.
- **No period audio.** A machine room's defining quality is its NOISE — tape reels, card readers,
  air handling. Silence is a large miss for a scene built on atmosphere.

## Ranked, by product value per unit of effort

1. **Input path** — converts a demo into a product. Nothing else matters until this exists.
2. **Verify audio over Pixel Streaming** — retires the biggest unknown; cheap, and its failure would
   invalidate the current voice plan.
3. **Gaze + emotion → face** — the rig is ready; highest perceived-aliveness return of anything here.
4. **Long-term memory** — the difference between a chatbot with a face and a companion.
5. **Observability** — you cannot operate what you cannot see, and today we read logs over SSH.
6. **Prop materials + room audio** — finishes the world.
7. **Auth and multi-session** — required before anyone outside the tailnet sees it.

## What I would NOT do next

- **Buy NVIDIA for inference.** 814ms on hardware already owned. See `INFERENCE-STACK.md`.
- **Migrate to Lemonade for chat.** It is verified and would collapse three services into one, but
  Ollama is working; do it when TTS/STT move, not before.
- **Add more tests of components in isolation.** 152 pass while the integration was broken. The
  conversation E2E harness is the format that finds real defects.
