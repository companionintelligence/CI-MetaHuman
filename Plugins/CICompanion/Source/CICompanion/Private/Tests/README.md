# MetaHumanLocalLLM tests

Two suites today, run via Unreal's Automation Spec framework (`FAutomationSpecBase`),
both headless (no live Ollama server, no rendering required):

## Backend — `MetaHumanLocalLLM.Backend.*`

- `OllamaClientSpec.cpp` — `UOllamaClient::BuildRequestBody` (request shape) and
  `FOllamaStreamParser` (NDJSON streaming: single line, partial-line buffering across
  chunks, multi-line chunks, malformed-line skipping, reset). These are the two pure,
  HTTP-free units of the Ollama client — the HTTP glue itself (`FHttpModule` request
  wiring) is intentionally thin and not unit tested, since it requires a live server to
  meaningfully exercise; see "Not covered" below.
- `EmotionMapperSpec.cpp` — table-driven text→emotion→animation-category mapping.
- `ConversationHistorySpec.cpp` — system-prompt seeding, turn ordering, reset, **the turn cap and
  persistence**. The cap cases exist because `/api/chat` has no server-side session: the whole
  transcript is re-sent every turn, so an uncapped history makes each turn cost more than the last
  until the request overruns the context window — at which point the server truncates *from the
  front*, silently discarding the persona. The tests therefore assert the system prompt survives 50
  turns, that `Num()` plateaus, and that eviction happens in **whole pairs** so the transcript never
  begins on an assistant turn (a history starting mid-exchange reads to the model as though it had
  spoken unprompted). The persistence cases pin that the persona comes from configuration and never
  from the file, that a corrupt or missing file changes nothing, that a `system` role smuggled into
  a hand-edited file is refused, and that a reset reaches the disk.
- `CIStreamUtf8Spec.cpp` — **the property both streaming clients silently violated**: the decoded
  text must not depend on how the bytes were chunked. Both clients diffed `GetContentAsString()` by
  TCHAR count, so a multi-byte character split across a chunk boundary changed length once completed
  and the cursor was left pointing into a string that had shifted underneath it — dropping or
  doubling glyphs on any reply containing an em dash, a curly quote, an accent or any CJK, which
  were then accumulated *and spoken aloud*. The headline case replays a mixed 1/2/3/4-byte payload
  at every chunk size from one byte upward and asserts all of them decode identically. Neither
  client spec could see this: they feed the parser whole, well-formed strings, exercising the parser
  and never the cursor in front of it.
- `CompanionGazeSpec.cpp` — the eye driver (`FCompanionGazeState`). Guards the two failure modes
  that are invisible to everything else this project measures: **wall-eye**, since ARKit spells the
  same physical direction as "Out" on one eye and "In" on the other, so pairing the curves by name
  rather than by meaning crosses her eyes on every glance while a frame grab still shows moving
  eyes and a curve-count check still shows 8/8 resolved; and **negative morph weights**, which
  extrapolate geometry inside out on a rig that does not clamp. Also pins that saccades are
  ballistic rather than eased (an eye that glides reads as a doll being turned by hand), the
  listener/speaker eye-contact asymmetry, and the 30 s backstop that stops a dead backend leaving
  her in the thinking aversion for the session.
- `CompatSpec.cpp` — the UE 5.5↔5.8 compatibility layer (`MetaHumanLocalLLM.Backend.Compat`).
  Split so that a compile-time shim is genuinely testable: the *compatibility table*
  (`FCICompatReport::ExpectedFor(major, minor)`) is a pure function with no `#if` in it, so the
  5.5 answers are asserted even when only 5.8 is installed; a single *compiled binding* check
  then proves the preprocessor selected the row the table specifies for the engine actually
  running. Run it on both engines and both `#if` branches are covered.
- `FaceVocabularySpec.cpp` — guards the curve **names** the face driver writes. The failure this
  catches is silent: `SetMorphTarget` on a name the mesh lacks does nothing at all, so one typo
  reads as a rig bug. Validates `FFaceProfile::ARKit()` against the canonical ARKit-52 allow-list
  (the `mouthSmileLeft` vs `mouthSmile_L` trap from ANIMATION-PLAN §5.3), asserts CC and ARKit
  stay disjoint vocabularies, and pins the 52-vs-61 distinction — `EARFaceBlendShape` declares
  **61** enumerators, the last 9 being head/eye *pose* channels, so any `i < 52` loop is mis-sized.
- `VisemeInteropSpec.cpp` — the bridge to the org's other embodied agents: the Oculus/OVR-15
  viseme names spelled exactly as CI-Spatial-Companion-WebXR/Unity/WebSpatial spell them
  (mixed case is load-bearing — `viseme_kk` lowercase, `viseme_PP` uppercase), and the
  `{ words, wtimes, wdurations }` millisecond timing envelope those repos pass between TTS and
  their viseme generator.

Run: `Scripts/RunBackendTests.ps1`

## Frontend — `MetaHumanLocalLLM.Frontend.*`

- `CompanionHUDSpec.cpp` — drives the native Slate HUD (`SCompanionHUD`) via its
  `Simulate*` test hooks (no real mouse/keyboard input needed): text submission, empty
  submission is ignored, mic toggle state + callback, chat log append/clear. Also pins the
  `MaxRetainedChatEntries` cap — the HUD is an overlay on the companion, and an unbounded
  log grew until it covered the character, so the tests assert both the tracked array *and*
  the scroll box rows stop at the cap with the newest line kept and the oldest evicted.

Run: `Scripts/RunFrontendTests.ps1`

## Not covered yet (documented, not silently dropped)

This mirrors CI-Spatial-Companion-WebXR's 5-layer strategy (unit → component → visual
capture harness → E2E → render matrix) as a naming analogy, but only the first two
layers exist here so far:

- **L3-equivalent — visual capture harness.** The WebXR sibling calls this layer the
  "keystone": most rig bugs (a viseme that never fires, an emote that doesn't blend)
  are invisible to unit tests and only show up as motion. An Unreal analog would drive
  the real MetaHuman skeletal mesh/AnimBP in a dedicated test level and capture
  screenshots (`AutomationScreenshotOptions` / functional test actors) at intervals to
  prove visemes/emotes are actually changing frame-to-frame. **Blocked on a MetaHuman
  asset existing in the project** (see the manual checklist in the implementation
  plan) — there's nothing to point the harness at yet.
- **L4-equivalent — scenario/E2E.** A scripted user-input sequence against a
  deterministic (mocked) Ollama backend, asserting end-to-end UI state. Straightforward
  to add once `AMetaHumanCompanionCharacter` has a real level/Blueprint to run in.
- **L5-equivalent — render matrix.** N/A until there's more than one avatar/quality
  tier to validate across.
- **Live HTTP streaming behavior.** `UOllamaClient`'s actual `FHttpModule` request/
  response wiring (progress-diffing, cancellation mid-flight, retry) is exercised by
  the pure `FOllamaStreamParser`/`BuildRequestBody` unit tests but not by an
  integration test against a real or mocked HTTP server. Worth adding if/when the
  project gets a CI runner with a disposable Ollama instance.
