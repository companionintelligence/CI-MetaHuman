# Avatar audit — 2026-08-16

Full-stack audit of the embodied companion: build, specs, services, speech, visemes, scene and UI,
against a native Win64 build cooked from `feat/gaze-memory-and-input-wiring` plus the fixes below.

> Capture media (~40 MB of PNG/GIF) is gitignored — it is reproducible output, not an artifact.

---

## 1. Verdict

| Bar item | Result |
|---|---|
| Compiles on the supported engine | ✅ UE **5.8** |
| Automation Specs pass | ✅ **284/284** backend + 28 frontend, against a freshly built binary |
| Chat turn completes, audio correct | ✅ typed turn + opening beat, Tier-1 phoneme alignment |
| **Facial animation visibly works** | ✅ **RESOLVED — see §4** |
| Real container / Pixel Streaming | ⛔ not validated — host limitation, §6 |

The face animates. It was never broken: it was **under-driven by roughly an order of magnitude**,
which is indistinguishable from broken when you look at it, and which defeated four separate
measurements before a method was found that could see it (§4, §7).

---

## 2. Fixes that landed and are verified

| Fix | Evidence |
|---|---|
| **`MHLLM_FACE_SELFTEST` was inert in every cooked build.** `CompanionFaceSubsystem` started the self-test; `CompanionRuntimeSubsystem::ApplySettings` stopped it a moment later because `bFaceSelfTest` was never seeded from the env var. The one diagnostic meant to prove the morph path in a shipped build could never run there. | log went `selftest=0` → `selftest=1` |
| **`KokoroModel` defaulted to `Kokoro-82M-ONNX`**, which live Lemonade rejects with 404 `model_not_found`. Only `docker-compose.face.yml` carried the working override, so every other path (native build, bare editor) was silently mute. Now `kokoro-v1`. | audit ran with `KOKORO_MODEL` unset and she spoke |
| **Kokoro TTS had no request timeout** — the only HTTP client in the plugin without one. A server that accepted and went quiet left the turn permanently un-voiced. | — |
| **Negative `MHLLM_MAX_TURNS` erased the conversation.** `IsNumeric()` accepts `-1`; `MaxMessages = turns * 2` then made `Excess` exceed the history, wiping it (persona included) on every append. | rejected with a warning |
| **Tape reels never span.** 24 reel actors spawned at default Static mobility while `UTapeReelSpinner` rotated them. | **29,342 warnings/run → 0** |
| **HUD restyled to CI-Common canon** (§5). | — |
| **`MorphDataSpec`** — new test asserting morph targets carry vertex deltas, not just names. | 42/42 pass |

### Viseme jaw weights — changed, NOT verified

`FaceProfile.cpp` vowel `Jaw_Open` raised (duration-weighted mean 0.094 → 0.157; over voiced time
0.216 → 0.360; `OU` 0.10 → 0.38), guarded by two new specs asserting jaw **magnitude** rather than
mere presence — the old tests passed at 0.094 because they only checked `> 0.0`.

**These numbers have never been seen to move a pixel**, because the face does not animate at all
(§4). The arithmetic is sound; the magnitudes may well need re-tuning once deformation works.

---

## 3. Verified working

- **Cognition** on beta-1 (AMD Radeon RX 7900 XTX, ROCm): LLM median **1324 ms**. The documented
  AMD/ROCm path, not a substitution — a run that accidentally used this laptop's local Ollama
  measured **31 155 ms**, a 24× difference confirming the accelerator was the bottleneck.
- **Voice loop** `RunConversationE2E.py`: **6/6 turns**, round-trip similarity median **1.000**.
  TTS median 891 ms, STT 93 ms.
- **Typed interaction** through the real Slate chat box: `"What do the tape reels behind you do"` →
  305-char reply → 19.20 s speech → 323 phonemes → **271 viseme spans**, `source=ALIGNMENT`.
- **Scene**: 18 troffers, 12 IBM 729 tape units + 24 reel actors, 22 cabinets, operator console,
  6 vector displays, 3 CRT panels, room tone.

---

## 4. The face — resolved

Three defects stacked. Fixing only the middle one produces the "fish mouth" that prompted this.

### 4.1 The self-test could never run in a cooked build

`UCompanionFaceSubsystem` starts it on `MHLLM_FACE_SELFTEST=1` and logs `Face self-test running.`;
`UCompanionRuntimeSubsystem::ApplySettings` then ran with `bFaceSelfTest` at its `false` default and
called `StopSelfTest()`. The env var was never seeded into the settings model, so the one diagnostic
that exists to prove the morph path in a shipped build was the one thing that could not run there.
Both facts appeared in the same log: `Face self-test running.` next to `selftest=0`.

### 4.2 Under-driven by ~10×

Reallusion CC's `V_*` morphs are subtle. At unity, lip deformation sits far below the idle head
animation on top of it, so it reads as a dead face — and defeats any time-domain measurement.

The self-test drives one viseme per 0.4 s, i.e. **2.5 Hz**, while the idle cycle is ~0.23 Hz. Those
separate cleanly. Measuring lip aperture at the drive frequency against an **undriven nose control**:

| gain | lips @2.5 Hz | nose (control) | lips/nose | Appearance |
|---|---|---|---|---|
| 1.0 | 0.129 | 0.017 | 7.5× | barely parted |
| 2.0 | 0.267 | 0.016 | 16.3× | still subtle |
| **3.0** | 0.260 | 0.023 | 11.2× | **open, teeth visible** |
| 5.0 | 0.343 | 0.029 | 12.0× | distorted oval |

`Config/DefaultEngine.ini` had been granting the renderer `r.MorphTarget.MaxBlendWeight=5.0` all
along, while the code clamped to a hardcoded `1.0` — none of that headroom was reachable. Gain
defaults to 3.0 (`MHLLM_FACE_GAIN`) and applies to the **viseme layer only**: `AddEmotion` keeps
FACS weights ≤0.45 deliberately ("a full-strength FACS pose reads as a caricature") and blink is
already a full close at 1.0, so a uniform gain would give a mugging face and eyes that slam shut.

### 4.3 Unbounded composition tore the lip

Once visible, ~1.9 s into a test utterance the upper lip stretched with dark artifacts at both
corners. `FFaceProfile::Accumulate` clamps each curve to 1.0 **independently**, so two overlapping
visemes can each reach full strength and then both get multiplied — a pose nobody sculpted.

Bounded by scaling the composed set by one factor, `Effective = min(Gain, VisemePoseCap / Peak)`,
which preserves the ratios that make a pose read as a particular viseme. Cap 2.0
(`MHLLM_FACE_POSE_CAP`).

### 4.4 Verified on real speech

Same utterance, same regions, before and after the cap:

| | LIPS speech | vs silence | Cheek control |
|---|---|---|---|
| Before cap | 11.49 rev/s | 6.98× | 1.51× |
| **After cap** | **11.36 rev/s** | 6.51× | 1.39× |

11.5 reversals/s ≈ 5.75 Hz, inside the phoneme band. Articulation survived the cap — the prediction
that would have falsified the fix (lip rate collapsing toward the 1.65 rev/s silence baseline) did
not occur, and the torn frame became a clean rounded O.

### Ruled out along the way, with evidence

| Hypothesis | Evidence against |
|---|---|
| Tick ordering (anim clobbers writes) | `AddTickPrerequisiteComponent(Mesh)` changed nothing |
| Weights don't survive the frame | `FACESURVIVE 'V_Open' written=0.806 survived_next_frame=0.806 held` |
| GPU morph path | forcing `r.MorphTarget.Mode 0` + skin cache off: unchanged |
| Nanite skeletal mesh | `nanite ENABLED = False` on all three meshes |
| MinLOD / wrong LOD | `min_lod = 0` |
| Morph targets empty in source | `MorphDataSpec`: **42/42 curves have LOD0 vertex deltas** |
| Wrong actor driven | authoring spawns one `SkeletalMeshActor` (`SK_Nia`) tagged `CompanionFace` |

`UMorphTarget::MorphLODModels` is **stripped by the cook** once deltas are baked into render data,
so it must never be used as a runtime health check — doing so produced a false "ZERO vertex deltas"
alarm that read exactly like a smoking gun (§7).

---

## 5. HUD — CI-Common branding

Tokens from `CI-Common/docs/UI-STYLE-GUIDE.md` §3.2 dark mode: `--card #0a222e`,
`--primary #abd4d8`, `--muted-foreground #8f98a3`, `--border #2c676d`, radius 10/8/6 px. Dark is
required, not preferred: the HUD composites over a dim 3D set and is captured into the same back
buffer Pixel Streaming encodes.

Transcript entries are cards with an uppercase speaker label — **teal for her, muted for the
visitor** — scannable by colour before a word is read. Only her lines carry the teal hairline;
outlining both would spend the accent to say nothing. Width-capped at 620 px so text does not run
across her face in the portrait framing.

**Typeface — now actually Montserrat.** It briefly shipped with Roboto substituted, on the mistaken
conclusion that no Montserrat existed anywhere available. It does: `CI-Capture/assets/fonts` carries
Light / Regular / SemiBold TTFs under the SIL Open Font License, and CI-Hub and CI-Server both ship
a variable woff2. Those three faces are now vendored into `Content/Fonts` and imported as
`UFontFace` assets.

Two notes on how, because both were dead ends first:

- The composite font is built **in C++** (`CIBrandStyle.h`), not as a `UFont` asset. Creating a
  `UFont` needs a live Slate application, which the `-run=pythonscript` commandlet never has — it
  asserts `CurrentApplication.IsValid()` and takes the editor down. The face assets *do* import
  headlessly, so composing in code keeps the whole font path reproducible from the command line.
- `FTypeface::AppendFont`'s public overload takes a **filename**, which is exactly the path that
  breaks in a cooked container. The faces are referenced as `FFontData(asset)` instead.

Only three weights are vendored against the guide's five, so the ladder is approximated:
200→Light, 400/500→Regular, 600/700→SemiBold. Falls back to Roboto if the assets are absent, so a
content-light checkout gets the wrong typeface rather than no text.

---

## 6. Not validated

**Pixel Streaming / container path.** `core-2` is reachable and healthy but has **no MetaHuman
images or containers at all** — absent, not stopped. It runs unrelated live CI-Marketplace
services. This machine cannot cook Linux (`Failed to SetupSDK for platform 'Linux'`, no
`C:\UnrealToolchains`) and its Docker daemon is down. Host limitation, not failure.

**UE 5.5.** Every tracked `Content/` asset is package version **1018**; UE 5.5.4 reads at most
**1013**. `BuildNative.ps1`'s `-Engine UE_5.5` default and `ARCHITECTURE.md`'s "UE 5.5 for the
shipped cook" are therefore both wrong. Left unchanged pending a decision.

---

## 7. Method note — five false conclusions

Recorded because the failure mode is systemic, not incidental. This system reports success at every
layer (`42/42 curves resolved`, `Face self-test running`, `Lip-sync source=ALIGNMENT`) while doing
nothing, so any check that trusts a success message rather than measuring an effect yields a false
pass.

| Wrong conclusion | Why it was wrong | What caught it |
|---|---|---|
| "Lip-sync works" | capture began ~10 s AFTER the utterance ended | log timestamps |
| "7.55 reversals/s proves articulation" | gaze switches to Speaking mode with more saccades; that was head motion | a static control region |
| "Writes land — `ok`" | read-back one line after write re-reads its own map | sampling at the next tick |
| "Source asset is fine" | authoring had failed; the stub map loaded and the driver never bound | printing the map size |
| "ZERO vertex deltas — root cause" | read editor-only data in a cooked build, where the cook strips it | a spec run in the editor |

Every one was caught by a **control**, and every control should have existed before the claim.
