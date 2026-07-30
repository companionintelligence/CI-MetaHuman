# The virtual being: engine-drift and ecosystem review

> Written 2026-07-29. Every claim was checked against the two engine installs on this machine, the
> sibling repositories' source, or this project's own source. Where something is inferred rather
> than measured, it says so.
>
> Engines measured: **UE 5.5.4** (CL 40574608) and **UE 5.8.1** (CL 56057345 — note *5.8.1*, not
> 5.8.0). 5.6 and 5.7 were not available, so every boundary placed at 5.6 is an inference and is
> flagged as one.

## The headline

Three separate things were reviewed, and each produced one finding that outranks everything else
around it.

1. **Engine drift is nearly a non-issue in C++, and is exactly one line in JSON.** Of ~30 engine
   symbols this plugin touches, 24 are source-identical between 5.5.4 and 5.8.1 and 5 more changed
   compatibly. The whole cross-version strategy was failing on a single missing `"Optional": true`.
2. **The LLM wiring points at an endpoint that does not exist.** CI-Hub does not proxy inference at
   all, so `POST <CI_HUB_URL>/chat/completions` 404s in every configuration.
3. **The face driver's blink is dead in two of its three code paths** — a pre-existing bug, found
   while validating the curve vocabulary.

---

## 1. UE 5.5 ↔ 5.8 feature drift

### Corrections to this repo's own docs

| Believed | Measured |
|---|---|
| 5.5 has better Pixel Streaming support | **`PixelStreaming2` ships non-beta in BOTH**, at `Engine/Plugins/Media/PixelStreaming2`. So does legacy `PixelStreaming`, and so does `PixelStreamingPlayer` (beta in both). Choosing 5.5 buys no Pixel Streaming capability 5.8 lacks. The AMD render wall in `FOLLOW-UP.md` is a Mesa/RADV bug on gfx1151, not an engine-version difference — the two were conflated. |
| 5.8 has better MetaHuman rigs | **Confirmed, and the gap is larger than stated.** 5.5.4 ships *only* `MetaHumanSDK` (2 modules) plus a zero-module `MetaHumanRuntime` redirect stub whose own description reads "Deprecated plugin now redirected to MetaHumanSDK". 5.8.1 adds an entire top-level `Engine/Plugins/MetaHuman/` tree that does not exist in 5.5: `MetaHumanAnimator` (28 modules), `MetaHumanCharacter`, `MetaHumanCoreTechLib`, `MetaHumanLiveLink`, `MetaHumanCrowd`, `MetaHumanCoreML`, `MetaHumanCalibrationProcessing`, `MetaHumanAnimationTools`. `UMetaHumanCharacter` genuinely does not exist in 5.5 — `grep` over its whole MetaHuman and RigLogic trees returns nothing. |
| 5.8 has MCP support | **Confirmed and total.** `ModelContextProtocol`, `MCPClientToolset`, `AllToolsets`, `ToolsetRegistry`, `AIAssistant` and ~25 `*Toolset` plugins are 5.8-only. 5.5.4 has none of it. |
| `BuildSettingsVersion.Latest` makes the project portable | It compiles on both, but **it makes 5.8 the *stricter* engine, not 5.5**. `Latest` = V5 on 5.5.4 and V7 on 5.8.1; V6 adds `bStrictInlineConformance` and `UndefinedIdentifierWarningLevel=Error`, V7 adds `ReturnType`, `Dangling` and `UnreachableCode` warnings-as-errors. Identical source is held to five extra error-level checks on 5.8. The `Target.cs` comment implies the risk runs the other way. |
| — | `EngineIncludeOrderVersion` has **no value pinnable across both**: 5.5.4 tops out at `Unreal5_5`, and 5.8 marks `Unreal5_1..Unreal5_5` `[Obsolete("unsupported")]` with `Oldest = Unreal5_6`. `Latest` is genuinely the only working choice — the one place the risky-looking option is correct. |

### The actual blocker (fixed)

`MetaHumanLocalLLM.uproject` marked **eleven** 5.8-only plugin references `"Optional": true` and
missed exactly one: `AIAssistant`. On 5.5.4 that is a hard `BuildException` from UBT
(`UEBuildTarget.cs:4936`) and a `PluginMissing` cook failure (`PluginManager.cpp:2483`).

`"Optional": true` was verified to be honored identically by both engines on both the UBT side and
the runtime/cook side, so the pattern was right — it was one omitted flag. This is precisely the
hazard `Deployment/Dockerfile.amd`'s own comment predicted: *"the 5.8 editor ADDS plugins to the
.uproject on its own (this is how AIAssistant appeared), and it adds them as REQUIRED"*. The
Dockerfile's strip-list papered over it for container cooks; a direct 5.5 build had no such cover.

**Fixed** — one line added to the `.uproject`.

### What genuinely needs a shim

Almost nothing that this plugin uses *today*, which is worth stating plainly rather than inventing
work. `GetSkeletalMeshAsset`, `USkeletalMesh::FindMorphTarget` and `SetMorphTarget` — the only three
engine calls `UCompanionFaceComponent` makes — are declared identically in both engines.

The real differences, and where they will bite:

| Area | 5.5.4 | 5.8.1 | Consequence |
|---|---|---|---|
| **Component curve readback** | not present | `USkeletalMeshComponent::GetCurveValue(FName, float, float&)` and `GetAnimCurves()` (`SkeletalMeshComponent.h:664/:667`) | Shimmed in `CISkeletalMeshCompat.h`; 5.5 routes via `UAnimInstance::GetCurveValue`, which exists in both. Needed by ANIMATION-PLAN's curve-verification path. |
| **Pixel Streaming audio producer** | plain C++ class; `IPixelStreaming2Module::CreateAudioProducer()` | `UINTERFACE`; `PushAudio` is `final`; register via `IPixelStreaming2Streamer::AddAudioProducer` (absent in 5.5). `CreateAudioProducer` is `UE_DEPRECATED(5.7)` | **The one Pixel Streaming API that actually differs.** On the critical path for TTS audio *and* for the phoneme timing that drives the mouth. Flagged `CI_WITH_PS2_AUDIO_PRODUCER_UINTERFACE`. |
| **PS2 audio sinks** | raw `IPixelStreaming2AudioSink*` | `TWeakPtr<IPixelStreaming2AudioSink>` | Flagged `CI_WITH_PS2_WEAK_AUDIO_SINKS`. |
| **PS2 module layout** | 6 modules | 8 (adds `PixelStreaming2Settings`, `PixelStreaming2RTC`); several public headers moved to `PixelStreaming2Core` | `Build.cs` module list differs; includes still resolve. |
| **Legacy curve API** | `AddCurveValue(UID)`, `FSmartName` curve identifiers still compile | **deleted**, not deprecated | A *forward*-compat trap: write it on 5.5, it hard-fails on 5.8. |
| **RigLogic** | 10.2.5, "3Lateral", beta | 13.2.5, "Epic Games", non-beta; `FAnimNode_RigLogic` private API rewritten (5 → 13 methods); export flipped to `MinimalAPI`+`UE_API` so the *type* is no longer dllexported | Same path, same module names, same headers across a 3-major-version jump — a path- or name-based probe reports "no change" and is badly wrong. |

### Traps found that are not version drift

- **`EAutomationTestFlags` needs no shim.** I expected one; it is already `enum class` in *both*,
  with `EditorContext`/`ProductFilter` byte-identical and `ApplicationContextMask` as a free
  constant. The migration predates 5.5.4. Writing an `#if` here would have *introduced* the
  breakage it was meant to prevent.
- **`UE_VERSION_NEWER_THAN_OR_EQUAL` is 5.8-only.** Using the obvious idiom inside a compatibility
  header would break that header on the older engine. Only `UE_VERSION_NEWER_THAN` and
  `UE_VERSION_OLDER_THAN` are common.
- **`UWorldSubsystem::OnWorldBeginPlay` / `PostInitialize` stopped being no-ops in 5.8.** They now
  set `bHasCalledBeginPlay` policed by `ensureAlwaysMsgf`. An override missing its `Super::` call
  compiles on both and works on 5.5, then fires an ensure on 5.8. Both current subsystems are
  correct; this is a latent trap for the next one, invisible to a 5.5-only test pass.
- **`EARFaceBlendShape` has 61 enumerators, not 52** — identical in both engines. The last nine are
  `HeadYaw/Pitch/Roll` and `LeftEye`/`RightEye` `Yaw/Pitch/Roll`: head and eye **pose** channels,
  not blendshapes. Any `i < 52` loop or `Num() == 52` assertion is mis-sized. Latent, not
  migration-related.
- **Content, not code, is the hard cross-version constraint.** `.uasset` serialization is
  forward-only: the 5.5 editor cannot open 5.8-saved assets. No `#if` fixes that, which is why the
  new test harness builds a *content-free* project (below).
- **`FName` comparison ignores case, and that quietly changes what "canonical curve name" means.**
  `FName("JawOpen") == FName("jawOpen")`, so any allow-list or viseme lookup built on `FName` is
  case-insensitive whether you intended it or not. This is benign *inside* the engine — morph
  targets are looked up by `FName` too, so a rig authored with different capitalisation still
  resolves — but it means an `FName`-based assertion cannot verify wire spelling. The siblings are
  JavaScript and C#, where `viseme_kk` and `viseme_KK` are different strings and only one exists on
  a Ready Player Me mesh. The interop layer therefore fixes the outbound spelling in its name tables
  and asserts it via `ToString()`, while staying lenient on inbound parsing. Found by a test failing
  against my own wrong assumption, not by reading headers.

---

## 2. What was built

```
Plugins/CICompanion/Source/CICompanion/
  Public/Compat/CICompanionCompat.h      version gate + CI_WITH_* feature flags, each with its evidence
  Public/Compat/CICompatReport.h         runtime-queryable mirror of those flags
  Private/Compat/CICompatReport.cpp      the compatibility TABLE, as a pure function of (major, minor)
  Public/Compat/CISkeletalMeshCompat.h   mesh/curve shims; one real polyfill (TryGetCurveValue)
  Public/Voice/CIVisemeInterop.h         ARKit-52 allow-list, OVR-15 names, word-timing envelope
  Private/Voice/CIVisemeInterop.cpp
  Private/Tests/CompatSpec.cpp           version table, compiled binding, diff reporting
  Private/Tests/FaceVocabularySpec.cpp   curve-name guards
  Private/Tests/VisemeInteropSpec.cpp    interop round-trips
Scripts/RunCompatMatrix.ps1              builds + tests against BOTH engines
```

### How a compile-time shim gets runtime tests

This is the awkward part of testing a polyfill, and it deserves stating: a spec running on 5.8
physically cannot execute the 5.5 `#if` branch. Testing the shim by calling it is a tautology — it
tests the branch the compiler already picked.

The decision is therefore separated from the `#if` that acts on it:

- `FCICompatReport::ExpectedFor(major, minor)` — a **pure function**, no preprocessor. Callable for
  any version on any engine, so **the 5.5 answers are asserted even on a machine with only 5.8
  installed**. Most assertions live here, including monotonicity (a capability must never appear
  and then vanish at a later version) and mutual exclusion.
- `FCICompatReport::Compiled()` — what the macros actually resolved to in this binary. One check
  that it equals `ExpectedForRunningEngine()`. Run on 5.5 it validates the 5.5 branches; on 5.8, the
  5.8 ones.

A wrong or contradictory `-D` override surfaces as a named flag in a diff rather than as
`false != true`.

`Scripts/RunCompatMatrix.ps1` closes the loop by building and running the suite against both
engines. It generates a throwaway project containing only a stub module and a junction to the
plugin — no `Content/`, so the 5.5 editor never has to open a 5.8-saved asset. It reuses
`RunTests.Common.ps1`'s pass/fail discipline (crash markers, started-vs-completed truncation, zero
results) and adds two guards of its own: an all-skipped matrix **fails** rather than exiting 0, and
a single-engine pass prints a warning that the layer is not actually covered.

### Verification status

```
Engine Build Tests                 (filter: MetaHumanLocalLLM.Backend)
------ ----- -----
5.8    OK    PASS (121)
5.5    OK    PASS (121)
```

The plugin **compiles, links and passes its whole backend suite on both engines** — so both sides of
every `#if` in this layer are exercised, not just the one the authoring engine picked.

Backend coverage goes from 57 to **121** cases. Worth noting separately: the *pre-existing* 57 had
never been run against 5.5 at all, because `RunTests.Common.ps1` defaults to UE 5.8. They pass.

The three new specs also caught three wrong assumptions in their own first draft, which is the
argument for writing them:

- `FName` comparison ignores case, so two "case is significant" assertions were testing something
  the engine does not do (see the trap note above).
- `FJsonValue::TryGetString` **succeeds on a number** — `{"words":[5]}` silently parsed as the word
  `"5"`. That was a real weakness in the parser, not just a bad test; `FromJson` now checks
  `Type == EJson::String` and rejects the payload.

Everything above marked "identical" or "no shim needed" is now backed by a build rather than by
reading headers. That distinction matters: reading headers produced two errors in this work that
only a compiler caught — `Log` is not in scope inside `ModuleRules` in either engine (and a Build.cs
that fails to compile takes the whole rules assembly with it, reporting as a baffling *"Expecting to
find a type … named …EditorTarget"*), and `KINDA_SMALL_NUMBER` is deprecation-wrapped in **both**
engines, which is a live hazard given 5.8 compiles under `BuildSettingsVersion.Latest` = V7
warnings-as-errors. The new code uses `UE_KINDA_SMALL_NUMBER`, which exists in both.

Still **not** verified, and worth being explicit about since the deployment target is Linux:
everything here was measured on Windows installs. Plugin `PlatformAllowList`s demonstrably differ
between versions (5.8 adds Android/IOS and a `Win64:arm64` deny-list to PixelStreaming2), and the
leading hypothesis for the unproven audio path — `-RenderOffScreen` forcing SDL's null audio driver
— is a Linux-only code path. A Linux pass of this same matrix is the obvious next verification step.

---

## 3. CI-Hub and CI-Marketplace: the LLM wiring is wrong

**`CI_HUB_URL` points at an endpoint that does not exist.** CI-Hub is an app/Docker orchestrator and
an inference *control plane*; it does not proxy inference. Its own source says so
(`inference.controller.ts:30-35`: *"The Hub does NOT proxy inference requests. Apps talk to the
Ollama container … directly"*), and enumerating all 190 paths in its `swagger.json` finds no `/v1`,
no `*chat*`, no `*completions*`, no `/api/chat`, no `/api/tags`. `POST <CI_HUB_URL>/chat/completions`
404s in every configuration — so `FMetaHumanLLMEnvConfig`'s entire CI-hub path is misconfigured, not
merely unproven.

What the Hub actually does is hand out connection info. It composes the OpenAI-compatible base URL
as `${ollamaBaseUrl}/v1` — i.e. `http://host.docker.internal:11434/v1` from inside a container.

| Today | Should be |
|---|---|
| `CI_HUB_URL` (no `/v1`) | `CI_LLM_BASE_URL`, already including `/v1` |
| `CI_HUB_TOKEN` | `CI_LLM_API_KEY` — Ollama's `/v1` takes the non-secret literal `ollama` and validates nothing |
| `CI_HUB_MODEL` | `CI_CHAT_MODEL` |
| `MHLLM_BACKEND` | Hub `CI_*` convention (`MHLLM_*` and `CI_HUB_*` have zero occurrences in the Hub repo) |

On `OLLAMA_MODEL=gemma4:e2b`: `GAP-ANALYSIS.md` calls this a wrong model name. That is half right and
worth correcting — the tag **is** Hub-canonical (`curated-models.ts:68`, and it is the cpu-only-tier
default). The bug is that it is hardcoded and unpulled on the host, not malformed. The fix is
discovery at startup via `GET /api/tags`, failing loudly with the available list, rather than
editing the constant.

Also: **`WHISPER_SERVER_URL` and `PIPER_SERVER_URL` have nothing to point at.** `piper` appears
nowhere in the Hub repo; whisper exists only as a catalog row bound to a different backend.

### Shipping as a marketplace app

CI-Marketplace is a git-of-JSON catalog (510 apps): `apps/<slug>/config.json` +
`docker-compose.json` (JSON, camelCase, **not** YAML), schema enforced by zod in
`__tests__/apps.test.ts` on every PR. The good news is that GPU is first-class and already used by
10 apps (`deploy.resources.reservations.devices`, plus `devices:` for `/dev/dri` passthrough), and
edge auth is provided — **`oauth2-proxy` should be deleted from the compose** in favour of
`hub_integration.edge_auth`.

The genuinely hard part has no precedent: `grep` across all 510 apps for `3478|coturn|turnserver`
returns **zero hits**. There is no TURN convention and no WebRTC media-path precedent in the
catalog, and the host `port` is a globally-unique reservation — a collision makes the app
uninstallable with no user workaround.

---

## 4. Sibling puppets: the contracts worth adopting

`CI-Spatial-Companion-WebXR`, `CI-Spatial-Companion-Unity` and the archived
`ZARCHIVE-CI-Agent-WebSpatial` converged independently on contracts this puppet should match. The
good news: **UE's `EViseme` is already the same OVR-15 vocabulary**, so only a name bridge was
needed — now in `CIVisemeInterop.h`.

- **Viseme names**: `viseme_sil, viseme_PP, viseme_FF, viseme_TH, viseme_DD, viseme_kk, viseme_CH,
  viseme_SS, viseme_nn, viseme_RR, viseme_aa, viseme_E, viseme_I, viseme_O, viseme_U`. Mixed case is
  load-bearing — it is baked into every Ready Player Me avatar's blendshape list. UE's only
  divergence is spelling `IH`/`OH`/`OU` where the siblings use `I`/`O`/`U`.
- **Word timing**: `{ words, wtimes, wdurations }` in milliseconds ("WhisperData"), used whether the
  timings come from a real aligner or an approximation. Implemented, with conversion straight to
  `FVisemeSpan` — and unlike `FVisemePlanner::PlanFromText`'s 165 WPM guess, a track built from
  measured word windows **cannot drift** against its audio.
- **Not yet adopted, recommended:**
  - *Normalized SSE envelope*: `{"type":"chunk","content":…}` / `{"type":"done"}` /
    `{"type":"error","message":…}`, one JSON object per `data:` line.
  - *Breath chunking* — the load-bearing timing primitive: accumulate deltas, cut once the buffer is
    ≥20 chars **and** the fragment matches `/.*?[.,!?]/`. This is what makes streamed speech sound
    paced rather than arriving in one block.
  - *Barge-in*: one monotonic `interrupt` integer per user turn, with the in-flight adapter holding
    a **getter** over the newest value so it self-cancels. `SubmitUserText` already cancels; the
    getter pattern is more robust.
  - *Three-tier config* (Unity): CVar/override → environment variable → default.
    `FMetaHumanLLMEnvConfig` is env-only, so there is no runtime override path.
  - *Tool schema*: `say`, `gesture`, `look_at`, `walk_to`, `detect_objects`, returning plain
    strings. **No sibling implements tool-calling against Ollama yet** — this is greenfield for the
    whole org, and the UE puppet is the one with a body to act with.
- **Nobody has long-term memory.** WebXR calls it their flagship gap; Unity and WebSpatial have
  none. This project's `ci-memory` integration would be the org's first.

---

## 5. Pre-existing bugs found while validating

Ranked. None were introduced by this work; all were found by reading the code the tests touch.

1. **The companion "brain" free-falls out of the world.** `UCompanionRuntimeSubsystem` spawns an
   `ACharacter` at `Z=-10000` with an active `CharacterMovementComponent` and no floor. It reaches
   terminal velocity, crosses `KillZ`, and `FellOutOfWorld` destroys it after roughly four minutes —
   `Companion` goes null and `SubmitUserText` returns false forever, with no log. The 5s greeting
   fires first, so it presents as *"it worked once, then went deaf."* Fix: derive from `AInfo`, or
   disable movement and collision after spawn.
2. **Blink is dead in two of three paths.** `ApplyCurves` zeroes every curve in
   `Profile.AllCurves()` — which deliberately includes `BlinkCurves` — and the self-test branch never
   re-adds the blink weight, so the eyes never blink during the diagnostic loop, directly
   contradicting the comment above it. Separately, the idle branch's `if (Blink > 0.0f)` guard means
   the frame that *ends* a blink never writes 0, latching the eyelids ~28% closed at 30fps until the
   next blink. Given `GAP-ANALYSIS.md` ranks eyes as the cheapest aliveness win available, this
   matters more than its size suggests.
3. **Streaming corrupts multi-byte UTF-8.** Both chat clients diff `GetContentAsString()` by TCHAR
   length. A character split across a network chunk decodes wrong once, is counted into
   `LastProcessedLength`, and is never corrected — so the correct character is skipped and the
   garbage one has already been emitted. LLM replies routinely contain smart quotes, em-dashes and
   emoji. Fix: diff raw bytes and hold back trailing incomplete sequences.
4. **Dangling reference across a re-entrant callback.** Both clients hold a raw `FActiveRequest&`
   borrowed from a `TMap` across `OnToken`, which can re-enter `SendChat`, `Add` to the map and
   rehash it.
5. **Audio-thread lambda captures raw `this`.** `UWhisperSpeechToText` registers a generator delegate
   with no `EndPlay`/`BeginDestroy` teardown; `bIsListening` is a plain bool read on the audio
   thread and written on the game thread.
6. **Two lip-sync systems, one reachable.** The runtime subsystem spawns
   `AMetaHumanCompanionCharacter::StaticClass()` directly, never a Blueprint subclass — so
   `OnSetVisemeWeights`, `OnSetEmotion`, `OnSpeak` and `OnConversationError` are all no-ops, and the
   entire Piper envelope path, the emotion mapper's output and `UCompanionHUDWidget` are dead code.

Also noted: test specs have no `#if WITH_DEV_AUTOMATION_TESTS` guard, so test code compiles into the
shipping Runtime module. The new specs match existing house style rather than diverging; a
project-wide guard pass is a separate decision.
