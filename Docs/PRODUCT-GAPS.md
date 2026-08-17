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

~~**But no audio has ever reached a browser.**~~ **RESOLVED 2026-08-06.** It reaches a browser and
it is audible: speech measured at `peakRms` 0.0769 against a 0.0211 room-tone floor, 3.65x. UE had
been transmitting correctly all along — the stock frontend simply never attached the inbound audio
track to the `<video>` element it built from the video track alone. One script in the already-mounted
`Deployment/pixelstreaming/player.html` closes it. See `REPO-STATE.md` §5 for the measurements and
for why this hid so well.

~~Also missing: **barge-in**.~~ **DONE.** `UCompanionRuntimeSubsystem::CancelUtterance` now stops the
audio, the viseme track and the pending alignment timer together. `StopSpeaking` had zero callers in
the entire repo, so an interrupted reply used to keep being *mouthed* in silence.

## 3. ~~She forgets everyone, immediately~~ — RESOLVED 2026-08-06

`UOllamaConversation` now caps retained turns and persists the transcript as JSON
(`MHLLM_MEMORY=1`, `MHLLM_MEMORY_PATH`). Verified end to end on core-2 against a bind-mounted
path, which is the only version of the claim that means anything — an in-container file would
prove only that one process can read what it just wrote:

```
turn 1   "My name is Liam and my favourite colour is orange."
turn 2   "What is my name and my favourite colour?"
         -> "Your name is Liam and your favorite color is orange! 🧡"
docker compose restart project
         -> Conversation memory loaded: 4 message(s) recalled from '/memory/conversation.json'.
```

The cap is not a nicety. `/api/chat` has no server-side session, so the whole transcript is re-sent
every turn until it overruns the context window — at which point the server truncates from the
FRONT and the first casualty is the system prompt. She does not error when that happens; she stops
being Eliza.

Still open: this is a flat per-installation transcript, not identity. Two visitors share one
memory, and `ci-memory` on the same host remains unused.

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

**The framing does NOT read correctly, and this doc previously said it did.** Captured from the
live stream on core-2, 2026-08-06 (`Deployment/core2/bank_chat_audio.mjs`):

- **The default stream is landscape 1280x720, not the composed 720x1280.** `docker-compose.amd.yml`
  passes `-ResX=720 -ResY=1280` and its comment calls that "portrait 9:16 by default" — but without
  `-ForceRes` those values are only a *hint*, and UE under `-RenderOffscreen` falls back to its own
  1280x720 default. Every viewer who has ever opened `:8090` has seen the landscape crop. The
  composed portrait call has effectively never shipped.
- **Neither aspect currently gives the intended shot.** In landscape she sits small and centred, the
  cabinet tops form a hard horizontal band straight across the frame at her waist, and the back wall
  is a large flat orange field over the top third. Forcing portrait via `?MatchViewportRes=true`
  overcorrects into a tight bust shot with the hall almost entirely gone and a blown-out ceiling band
  across the top ~15%. The `author_diorama.py` framing math derives distance from a *horizontal* FOV
  while `DefaultEngine.ini` pins `AspectRatio_MaintainYFOV`, so the two disagree about which axis is
  being held — which is why neither window shape lands on the shot the maths describes.
- **Props render pale — confirmed, and it is as bad as issue #22 says.** The cabinets read as light
  grey office furniture against a reference of dark charcoal, blue panels and chrome. In frame this
  is what makes the room look like cubicles rather than a mainframe hall.
- **The ceiling blows out.** The top strip is near-white and featureless in both aspects, which is
  what makes the portrait crop in particular look unfinished.
- **Emoji render as `�` in the chat log.** She replies with 🧡 and the HUD shows a replacement
  box. The text is *correct on disk* (verified `f0 9f a7 a1` = U+1F9E1 in the persisted transcript),
  so this is purely a missing glyph in the Slate font — Roboto has no emoji coverage. Either strip
  non-BMP characters before display or fall back to a font that has them.
- **Wardrobe mismatch.** A white cocktail dress and heels in a 1970s machine room. Deliberate
  (it is Nia's shipped outfit) but it undercuts the premise.
- ~~**No period audio.**~~ Room tone ships and is audible in the browser — it is the 0.017 RMS floor
  the speech measurement sits on top of. Tape-reel and card-reader detail is still absent.

Framing is the expensive one to iterate: the camera is authored into the map, so every attempt costs
an author + cook cycle (~20 min). Re-derive the maths against `MaintainYFOV` **before** the next
cook rather than dialling it in by eye.

## Ranked, by product value per unit of effort

Items 1–4 of the original ranking are **done and verified on the live stream** (2026-08-06):

1. ~~**Input path**~~ — ✅ text in, over the stream, no click needed. And the **mic button is now
   actually connected**: `OnMicToggled` had been published to no subscriber since it was written, so
   it toggled its own colour and did nothing else. Every piece either side of it had passing tests;
   the wire between them was never a piece, which is how it survived a 300-spec suite.
2. ~~**Verify audio over Pixel Streaming**~~ — ✅ and fixed. §2 above.
3. ~~**Gaze + emotion → face**~~ — ✅ eyes move on the live stream (eye-region luma differs across
   frames sampled a second apart; a welded stare is bit-identical there).
4. ~~**Long-term memory**~~ — ✅ survives a container restart. §3 above.

Remaining, re-ranked on what the captures actually show:

1. **Framing.** The composed portrait call has never shipped — every viewer has seen the landscape
   crop. This is now the single largest gap between the product as designed and the product as
   served. §7.
2. **Prop materials.** Issue #22. The room reads as cubicles, not a mainframe hall.
3. **Observability** — you cannot operate what you cannot see, and today we read logs over SSH.
4. **Identity, not just persistence.** Memory is one flat transcript per installation; two visitors
   share it.
5. **Auth and multi-session** — required before anyone outside the tailnet sees it.

## What I would NOT do next

- **Buy NVIDIA for inference.** 814ms on hardware already owned. See `INFERENCE-STACK.md`.
- **Migrate to Lemonade for chat.** It is verified and would collapse three services into one, but
  Ollama is working; do it when TTS/STT move, not before.
- **Add more tests of components in isolation.** 152 pass while the integration was broken. The
  conversation E2E harness is the format that finds real defects.

---

## Addendum — later the same day

Three things changed after the above was written.

**A scripted opening shot landed.** `UCompanionStageDirector` flies the camera down the hall,
settles into the call framing, turns her to the lens and delivers a fixed brand line
("Welcome to Companion Intelligence, I'm Eliza."). This deliberately does NOT go through the LLM: a
brand statement must be word-for-word identical every run and must not wait on a model round trip
while the camera is holding on her face. `MHLLM_GREETING` is now `none` by default so the two
openings cannot collide.

It also changes the demo's shape — a visitor now sees an intentional establishing shot rather than
landing mid-scene. That partially compensates for gap #1: with no input path, a scripted opening is
the only thing that makes a correct build look different from a broken one.

**Viewport sizing is now a knob, and the answer is a client-side setting.** Stream resolution moved
from hardcoded to `STREAM_RES_X` / `STREAM_RES_Y` (default portrait 720x1280). To make the stream
track the viewer's window instead, no rebuild is needed:

    http://core-2:8090/?MatchViewportRes=true

UE re-renders at the browser viewport, and because the camera's aspect constraint was released the
framing follows. **The trade is real and worth stating:** in a landscape window that turns a
composed portrait call into a wide shot with a small subject. The 9:16 composition only holds if
the window is roughly portrait — so "fit the window" and "keep this framing" are in tension, and
which one wins is a product decision, not a bug.

**The hall reads.** Cabinets flank a receding aisle to the orange back wall, at the close spacing
the reference photographs use. Gap #7's remaining item is unchanged: the props themselves still
import pale against a reference that is dark charcoal and chrome.
