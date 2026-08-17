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
| **Facial animation visibly works** | ❌ **NO — unresolved, see §4** |
| Real container / Pixel Streaming | ⛔ not validated — host limitation, §6 |

**The face does not visibly animate, and the cause is not yet known.** Everything else below works.

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

## 4. The face does not animate — open investigation

Nothing in the pipeline reports an error, and the mouth does not move.

### Measured

With emotion and gaze **off** and the self-test genuinely running, all 15 visemes render an
identical closed mouth — including `AA` (`V_Open` 1.0 + `Jaw_Open` 0.72). The control is what makes
this conclusive:

| Region | Mean frame delta |
|---|---|
| Mouth | 2.10 |
| **Cheek (control)** | **1.90** |
| Eyes | 1.62 |

The mouth moves no more than a patch of cheek. No localized deformation anywhere — no visemes, and
no detectable blink.

### Ruled out, with evidence

| Hypothesis | Evidence against |
|---|---|
| Self-test never ran | fixed; `selftest=1`, still no motion |
| Tick ordering (anim clobbers writes) | `AddTickPrerequisiteComponent(Mesh)` changed nothing |
| Weights don't survive the frame | `FACESURVIVE 'V_Open' written=0.806 survived_next_frame=0.806 held` |
| Compose/write broken | `V_Open` composes to 0.62–0.99; every write accepted |
| GPU morph path | forcing `r.MorphTarget.Mode 0` + skin cache off: 2.07 vs 1.82, unchanged |
| Nanite skeletal mesh | `nanite ENABLED = False` on all three meshes |
| MinLOD / wrong LOD | `min_lod = 0` |
| Morph targets absent or empty **in source** | `MorphDataSpec`: **42/42 curves have LOD0 vertex deltas** |
| Wrong actor driven | authoring spawns one `SkeletalMeshActor` (`SK_Nia`) tagged `CompanionFace` |

### Next probe

Interrogate `FSkeletalMeshRenderData`'s morph vertex buffers at runtime — the cooked-build
equivalent of the delta question. `UMorphTarget::MorphLODModels` is **stripped by the cook** once
deltas are baked into render data, so it must not be used as a runtime health check (see §7).

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

**Typeface deviation.** The guide mandates Montserrat. It is not installed here and not in
`CI-Common/assets` (web surfaces pull it from Google Fonts, which Slate cannot), and no geometric
alternative is present. Substituted the engine's **Roboto** with the weight ladder mapped —
Montserrat 200→Light, 400→Regular, 500→Medium, 600/700→Bold. Roboto is neo-grotesque where
Montserrat is geometric; at 9–12 px over a moving background the weight and colour relationships
carry the brand and those are exact. **Do not call the HUD fully brand-compliant until a real
Montserrat UFont asset lands.**

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
