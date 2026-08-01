# Documentation index

**Eliza** — a locally-hosted embodied AI companion. A Reallusion Character Creator avatar in a
1970s mainframe room, rendered in Unreal Engine 5.5, delivered two ways: a Linux container cook
streamed to browsers via Pixel Streaming, and a native Windows executable. All inference is local:
Ollama for cognition, Kokoro for speech, Whisper for hearing, on AMD ROCm hardware we own.

---

## Start here

| Read this | If you want to |
|---|---|
| **[`ARCHITECTURE.md`](ARCHITECTURE.md)** | Understand the whole system: what runs where, how a turn travels, what every subsystem owns. **The spine — start here.** |
| **[`LEARNINGS.md`](LEARNINGS.md)** | Not repeat any of the expensive mistakes. Traced to commits, with the root cause and the rule each produced |
| **[`HANDOFF.md`](HANDOFF.md)** | Get oriented as a new maintainer. The original fifteen-lesson brief, written 2026-07-18 |

## Reference

| Document | Covers |
|---|---|
| [`FACE-PIPELINE.md`](FACE-PIPELINE.md) | The viseme vocabulary, the CC/ARKit `FFaceProfile` abstraction, how blink and emotion composite **over** speech, the curve-zeroing pass, and the second lip-sync path that is dead |
| [`USER-FLOWS.md`](USER-FLOWS.md) | Kiosk visitor and native desktop user; the scripted opening; the chat box and its focus behaviour over Pixel Streaming |
| [`BUILD-AND-DEPLOY.md`](BUILD-AND-DEPLOY.md) | The Linux container cook, the native Windows build, the shared definition of the set, and the verification gates that refuse a stale tree |
| [`CONFIGURATION.md`](CONFIGURATION.md) | Every `MHLLM_*`, `OLLAMA_*`, `KOKORO_*`, `WHISPER_*`, `CI_*` variable: default, effect, precedence |
| [`TESTING.md`](TESTING.md) | The five verification layers, and the discipline: exit codes **and** `Started == Completed`, pixels not logs, audio energy not track existence |
| [`ROADMAP.md`](ROADMAP.md) | What does not exist yet, ranked — and the buy-vs-build conclusion |

## Design and background

| Document | Covers | Status |
|---|---|---|
| [`ANIMATION-PLAN.md`](ANIMATION-PLAN.md) | The original cross-platform lifelike-animation design: phoneme pipeline, procedural eyes, micro-motion, and a full solution-landscape survey | Design authority for §1–3 and §5–6. **§4's Georgy "BUY" verdict is superseded** by [`ROADMAP.md`](ROADMAP.md) §4.2. Its ARKit tables predate the CC resident — see the dated note at the top of the file |
| [`SCENE-AUTHORING.md`](SCENE-AUTHORING.md) | What the set *contains*: props, reels, colour, and the exposure investigation | Current |
| [`INFERENCE-STACK.md`](INFERENCE-STACK.md) | Where cognition and voice should run, measured across the fleet | Current |
| [`COMPAT-AND-ECOSYSTEM-REVIEW.md`](COMPAT-AND-ECOSYSTEM-REVIEW.md) | UE 5.5 ↔ 5.8 drift, the CI-Hub finding, sibling-agent contracts, and pre-existing bugs found while validating | Current; several of its §5 bugs are still open |
| [`GDD.md`](GDD.md) | The game design document — world, character, experience | Background |
| [`GDD-WEB-EXPERIENCE.md`](GDD-WEB-EXPERIENCE.md) | The web-delivery experience design | Background |
| [`PLAN-MULTI-AVATAR.md`](PLAN-MULTI-AVATAR.md) | Casting more than one resident, and the traps recorded in advance | Plan |
| [`HANDOFF-METAHUMAN-SKIN.md`](HANDOFF-METAHUMAN-SKIN.md) | The MetaHuman skin/material investigation | Historical — the resident is now Reallusion CC |
| [`GAP-ANALYSIS.md`](GAP-ANALYSIS.md) | Engineering-level gap analysis, 2026-07-28 | Superseded by [`ROADMAP.md`](ROADMAP.md); kept for the record |
| [`PRODUCT-GAPS.md`](PRODUCT-GAPS.md) | Product-level gap analysis, 2026-07-30 | Superseded by [`ROADMAP.md`](ROADMAP.md); kept for the record |
| [`FOLLOW-UP.md`](FOLLOW-UP.md) | Earlier status and gotchas, including two false-pass test-runner bugs | Historical |
| [`reference/`](reference/) | Raw probe output, e.g. `nia-morph-targets.txt` | Data |

## Elsewhere in the repo

| Path | Covers |
|---|---|
| `Plugins/CICompanion/Source/CICompanion/Private/Tests/README.md` | Per-spec detail: what each unit test guards and why |
| `Deployment/core2/README.md` | The core-2 build loop, the service tunnels, typing over the stream, durability |
| `Deployment/README.md` · `Deployment/core-2-runbook.md` | Deployment and operational runbooks |

---

## Conventions used in these docs

| Marker | Means |
|---|---|
| **`IN FLIGHT`** | Uncommitted in the working tree on 2026-07-31 — specifically `UKokoroPhonemeAligner` and `FVisemePlanner::PlanBest`. Check `git log` before assuming it is still pending |
| **UNVERIFIED** | Stated because it was reported or inferred, but not confirmed against source, a measurement, or the commit history |
| A commit hash | The claim is traceable — `git show <hash>` for the full post-mortem. Many commit messages in this repo are the primary source |
| [VOICE-PIPELINE.md](VOICE-PIPELINE.md) | TTS, phoneme alignment, measured latency, the timing fallback ladder, and the remaining backlog |

Every document dated 2026-07-31 was written against `main` at commit `dea0437`.
