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
- `ConversationHistorySpec.cpp` — system-prompt seeding, turn ordering, reset.

Run: `Scripts/RunBackendTests.ps1`

## Frontend — `MetaHumanLocalLLM.Frontend.*`

- `CompanionHUDSpec.cpp` — drives the native Slate HUD (`SCompanionHUD`) via its
  `Simulate*` test hooks (no real mouse/keyboard input needed): text submission, empty
  submission is ignored, mic toggle state + callback, chat log append/clear.

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
