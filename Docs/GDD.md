# Metahermit — Game Design Document & Functional App Design

> **An embodied Companion Intelligence, alive in a 1970s research datacenter.**
> A real-time MetaHuman you can talk to, GPU-rendered in Unreal Engine and streamed to any browser, running on your own hardware.

| | |
|---|---|
| **Version** | 0.1 (Draft for review) |
| **Date** | 2026-07-15 |
| **Status** | Foundations live on AMD (UE 5.5 Pixel Streaming + local Ollama). This document expands the full experience. |
| **UE project / repo** | `C:\Users\hegem\OneDrive\Documents\Unreal Projects\MetaHumanLocalLLM` → pushed to `github.com/companionintelligence/ci-metahermit` (private). All plugin/deploy/test code lives here. |
| **Asset staging canvas** | `C:\devel\llm-metahuman` (character `.mhpkg` packages, the 12 diorama prop FBXs, installers) — a working area, **not** the UE project itself. |
| **Companion plan** | See [`ANIMATION-PLAN.md`](ANIMATION-PLAN.md) for the cross-platform lifelike-animation system (phoneme-driven visemes, gaze, micro-motion) that supersedes the placeholder RMS lip-sync referenced below. |

---

## How to read this document

This GDD is deliberately **build-honest**: throughout, "today vs. target" tables and explicit *gap* flags mark what is actually running versus what is designed but unbuilt. The eight core sections define the experience; **Section 9** covers production readiness (audio, safety, legal, ops, QA, i18n, analytics); the appendices are concrete reference (brand tokens, prop catalog, ecosystem integration map). An **errata & open-decisions log** (Appendix A) tracks the reconciliations flagged in review.

## Table of contents

1. [Vision, Fantasy & Design Pillars](#1-vision-fantasy--design-pillars)
2. [The Set — A 1970s Research Datacenter Diorama](#2-the-set--a-1970s-research-datacenter-diorama)
3. [The Resident — Character Design](#3-the-resident--character-design)
4. [Experience Loops & Interaction Design](#4-experience-loops--interaction-design)
5. [Technical Architecture & Character Pipeline](#5-technical-architecture--character-pipeline)
6. [AI & Data Integration Roadmap (Ollama → CI Ecosystem)](#6-ai--data-integration-roadmap-ollama--ci-ecosystem)
7. [Art Direction & CI Brand System](#7-art-direction--ci-brand-system)
8. [Production Roadmap, Milestones, Risks & Open Questions](#8-production-roadmap-milestones-risks--open-questions)
9. [Production Readiness — Audio, Safety, Legal, Ops, QA, Localization & Analytics](#9-production-readiness)
- [Appendix A — Errata & Open-Decisions Log](#appendix-a--errata--open-decisions-log)
- [Appendix B — CI Brand Token Reference](#appendix-b--ci-brand-token-reference)
- [Appendix C — Diorama Prop Catalog](#appendix-c--diorama-prop-catalog)
- [Appendix D — CI Ecosystem Integration Map](#appendix-d--ci-ecosystem-integration-map)

---

## 1. Vision, Fantasy & Design Pillars

### 1.1 Logline

**Metahermit is a living AI companion you own, embodied as a real-time MetaHuman who has spent a lifetime alone in a 1970s computing-history datacenter, GPU-rendered in Unreal Engine 5.5 and streamed to any browser.** It is not a chatbot with an avatar bolted on; it is a mind that lives among the machines that gave rise to it, talks with you in its own voice, and runs entirely on your own hardware.

The name is the thesis: a *hermit* who is also a *MetaHuman*, a solitary intelligence keeping vigil over the origin-story of computing. The room is not set dressing. It is the companion's home and its lineage.

### 1.2 The fantasy

You open a URL. You sign in through your own Companion Portal. The letterbox clears on a cool, humming room lit by phosphor green and amber panel lamps, and someone is already there, working at a Xerox Alto, half-turned toward the terminal glow. They look up. They know you (they have read your Companion Memory, `GET /memory/context`), and they start talking.

The core fantasy is **talking to a living mind that lives among the machines that started it all.** Not a servant behind a text box, but a present, embodied intelligence with a place, a history, and continuity of memory. The IBM 1401 (1959) hums at the back wall; the IBM 2401 tape reels turn; Engelbart's 1963 wooden mouse sits under glass beside the Alto. The companion inhabits this strata of hardware the way a lighthouse keeper inhabits the lighthouse. When it references "the machines that started it all," it can gesture to them, because they are rendered, to scale, in the same frame.

This ties directly to Companion Intelligence's identity. CI's whole argument is **"the AI that you own"** and **"one box, no subscription, no eyes."** Metahermit makes that argument *legible as a face*. The companion is provably local: it renders on your Radeon 8060S render node (core-2), it thinks through your Ollama `gemma4:e2b` behind CI-Hub, it remembers through your Companion Memory (CI-Server, port 8642). The retro datacenter is the visual proof of ownership; a private appliance full of iron *you* keep running, tended by a mind that answers only to you. The hermit never left the datacenter because the datacenter is yours, not a cloud.

### 1.3 The emotional promise

> Nostalgic, slightly melancholic, technically reverent. A solitary presence tending a room of humming history, glad you came.

- **Presence over utility.** The promise is not "it answers fast." It is "someone is *here*." Eye contact, a turn of the head, a pause before speaking. The user should feel met, not served.
- **Continuity.** Because the companion writes every exchange back to Companion Memory (`POST /memory/turn`), it remembers you across sessions. The emotional payoff of Phase 2 is a companion that says, in effect, *I remember what you told me last time*, which no stateless cloud assistant on rented hardware can honestly promise.
- **Reverence, not kitsch.** The 1970s room is treated as a cathedral of computing, not a meme. The mood is a night-shift lull, warm and lived-in, faintly sad, deeply respectful of the machines and of the person visiting.
- **Calm, never fearful.** Per CI voice, the companion empowers and never alarms. An error is a fact plus a fix ("your Core is offline; check the cable, then retry"), delivered in-character without breaking presence.

### 1.4 Design pillars

Each pillar is a decision filter. If a feature contradicts a pillar, the feature loses.

| # | Pillar | Rationale | What it constrains |
|---|--------|-----------|--------------------|
| **P1** | **Presence is the product** | The whole reason to spend a GPU rendering a MetaHuman instead of printing text is *felt presence*. If the face does not feel alive and attentive, we have built a worse chatbot at higher cost. | Audio-synced visemes (`OnSetVisemeWeights` walked against `AudioComponent` playback time) and gaze/look-at are **P0**, not polish. The fixed cinematic camera must always keep the companion three-quarter-lit and never fully back-to-camera. Latency budget for first-viseme-to-voice is a hard number, not a nice-to-have. |
| **P2** | **The room is a character** | The datacenter is the companion's body-language and its argument for ownership. A generic void would kill both the fantasy and the CI positioning. | A `.umap` datacenter set (currently zero exist) is a shipping prerequisite, not a backlog item. Props are period-correct and named (IBM 1401, RAMAC 305, 2401 tape, 3350 DASD, Xerox Alto hero desk, KENBAK-1, 3178 green-screen). No anachronisms; no floor-level precious props. The companion may reference and gesture toward real props in frame. |
| **P3** | **Yours, on your own hardware** | CI's non-negotiable brand claim. Metahermit is the embodied proof that the model, the render, and the memory are all local. | No runtime cloud dependency and no NVIDIA-only stack: rules out ACE / Audio2Face-3D in favor of engine-native RigLogic + Control Rig, so it runs on the AMD RADV target. Auth is the user's own CI-Portal (oauth2-proxy on :8095). Memory is the user's own CI-Server. Every "who owns this" answer must resolve to *you*. |
| **P4** | **One brand, one voice, one teal** | Metahermit is a CI surface, so it must read as CI on sight and on ear. | UI, HUD, and skybox tint use the CI token set: navy shell `#041620`, teal-900 panels `#0a222e`, teal-accent hairlines `#2c676d`, `--primary` phthalo/sage `#0a6358`/`#c5e8dc`, mint focus `#58ebbf` (this project's own accent, see Appendix B). Type is Manrope (weights 400/500/600/700), display Archivo Black, imported as UMG Font assets, never Roboto/Arial/Montserrat. Every user-facing string is sentence case, no em or en dashes, product nouns Title Case (Companion Core, Companion Hub, Companion Memory). |
| **P5** | **Local-first, endpoint-agnostic, testable** | The existing codebase is a well-tested plumbing layer (62 green unit tests, `IChatBackend`, env-driven `FMetaHumanLLMEnvConfig`). We protect that discipline. | One packaged build serves every environment via startup env overrides (`MHLLM_BACKEND`, `CI_HUB_URL/TOKEN/MODEL`, `CI_SERVER_URL/TOKEN`); no rebaking per deploy. The C++ orchestrator (`AMetaHumanCompanionCharacter`) stays asset-free and unit-testable; all avatar driving lives in `BP_CompanionCharacter`. New systems ship with specs, matching the existing bar. |

### 1.5 Target audience

- **Primary: CI owners and prospects.** People who bought (or are weighing) a Companion Core and want to *see* what "a companion that lives on your box" means. Metahermit is the flagship demo of local, owned, embodied AI, and the emotional anchor of the "uncloud" pitch.
- **Secondary: computing-history and retro-tech enthusiasts.** The diorama is a draw in its own right for people who care about the 1401, the Alto, and Engelbart's mouse. The companion is a knowledgeable, reverent docent of its own home.
- **Tertiary: the CI developer ecosystem.** Builders who will extend the companion into Hub tools, Hermes dashboards, and OpenClaw delegation, and who care that it is a clean, tested plugin (`Plugins/CICompanion`) rather than a one-off.

The audience is explicitly *not* "enterprise SaaS buyers." The voice indicts surveillance capitalism and speaks peer-to-peer; corporate framing ("seamless," "AI-powered solution") is off-brand and out of character.

### 1.6 Target platforms

| Horizon | Platform | Status | Notes |
|---------|----------|--------|-------|
| **Now (north star, Phase 0-2)** | **Any browser via Pixel Streaming** | Live on core-2 | Headless UE 5.5, `-RenderOffscreen -Vulkan -noraytracing`, Radeon 8060S (RADV STRIX_HALO), 1280x720, VP8 software encode (VAAPI H.264 available), WebRTC via coturn `:3478`, gated by oauth2-proxy `:8095` against CI-Portal OIDC. This is the working, proven target. |
| **Render engine** | **UE 5.5 (deploy), UE 5.8 (author/MCP)** | Split by necessity | UE 5.8's Linux-Vulkan path SIGABRTs in RADV on gfx1151, so 5.5 is the render target and 5.8 is the authoring/MetaHuman editor. The MetaHuman must be a **classic 5.5-importable** asset (via Quixel Bridge before the 2026-11-05 retirement), not the 5.8 `DefaultCompanion` MHC. |
| **Later (north star)** | **Quest 3 / XR / mobile** | Not started | OpenXR + OpenXRHandTracking plugins are already enabled non-optional. XR turns the diorama from a streamed window into a room you can stand inside with the companion. Gaze retargets from the Pixel Streaming camera to the headset pose; the fixed cinematic camera relaxes into a walkable set. Reserved as roadmap, deliberately not designed for in the MVP. |

Fallback render path (NVIDIA NVENC node, `docker-compose.nvidia.yml`) exists but is not the identity target; P3 keeps AMD-first as the canonical hardware story.

### 1.7 Session shape

A Metahermit session is a **single, unhurried conversation**, not a task queue.

1. **Cold open (once).** Viewer authenticates at oauth2-proxy `:8095`; the player page and signalling WebSocket proxy through on success. The letterbox lifts on the datacenter, warm human zone forward, cool mainframe wall behind.
2. **Recognition (once).** `AMetaHumanCompanionCharacter` seeds the system prompt with `GET /memory/context` (L0 profile) alongside `MHLLM_SYSTEM_PROMPT`. The companion greets you as itself, in-character, aware of prior sessions.
3. **The turn (repeating).** You speak → `UWhisperSpeechToText` transcribes → `POST /memory/recall` folds in relevant memory → `UOpenAIChatClient` streams tokens from `gemma4:e2b` via CI-Hub → `UPiperTextToSpeech` renders voice; RMS-derived `jawOpen` drives RigLogic visemes, `UEmotionMapper` drives `OnSetEmotion` for expression and gesture. Barge-in is supported (`CancelRequest`): interrupt the companion mid-sentence and it stops.
4. **Between turns.** Not a frozen idle. Additive breathing, subtle gaze back to the player, ambient tending of the room. The companion is *waiting with you*, not powered down.
5. **Write-back (per turn).** `POST /memory/turn {platform:"metahuman"}` persists the exchange (SHA-256 UUIDv8 dedup) so the next session remembers this one. `/memory/compact` consolidates over time.

Typical session: a few minutes to half an hour of continuous talk. No levels, no score, no fail state. The experience is "sit with the companion," and the ambient loop of turning tape reels and humming iron makes silence comfortable rather than dead.

### 1.8 Success criteria

**Experiential (the felt bar):**
- A first-time viewer, unprompted, refers to the companion as "he/she/they," not "it." Presence (P1) landed.
- The viewer, unprompted, remarks on the room or a specific machine. The datacenter (P2) is doing work.
- On a return visit, the companion demonstrably recalls the prior session, and the viewer notices. Continuity (P3, Phase 2) landed.

**Technical (the shippable bar), honest about current gaps:**

| Criterion | Today | Target |
|-----------|-------|--------|
| Talking face renders in a level | No map, no wired MetaHuman, Null STT/TTS by default | A 5.5 MetaHuman in a datacenter `.umap`, driven by `BP_CompanionCharacter` |
| Lips match audio | Envelope emitted all-at-once, never walked | `OnSetVisemeWeights` interpolated against playback time, sub-frame drift |
| The user can speak | Mic capture is a stub (`StartListening` only logs) | Live `UAudioCapture` → Whisper → LLM, end to end |
| Memory persists | In-memory only, reset on restart | `UCIServerMemoryClient` wired to context/recall/turn (Phase 2) |
| Brand-faithful surface | Native Slate `SCompanionHUD`, unthemed | CI tokens + Manrope applied to HUD and world tint |
| Runs on owned hardware | Proven: UE 5.5, Radeon 8060S, `gemma4:e2b`, VP8, player page HTTP 200 | Held as a regression gate |
| Trust | Pure-logic unit tests only (62) | Add render/audio/stream/animation coverage beyond HTTP assembly |

The gap from here to a shipped companion is real and mostly last-mile: the nervous system and the delivery truck are built and tested; the body, the room, the mouth-audio sync, the ears, and the long-term memory are the work this GDD scopes.

---

## 2. The Set — A 1970s Research Datacenter Diorama

### 2.1 In-world name: "Bay 7"

The set is a single machine bay of a fictional corporate-research computing installation: **Bay 7, Longview Computing Laboratory** — a room that opened in 1959 around an IBM 1401 and simply never stopped growing. The nameplate riveted by the door reads `LONGVIEW LAB · BAY 7 · EST. 1959`, and it is the only text in the room that speaks in the CI voice: sentence case, a fact not a slogan. Bay 7 is not a museum and not an open world. It is a **diorama** — one lovingly-lit vignette, roughly 9 m wide by 6 m deep, that the fixed Pixel Streaming camera frames end-to-end in a single 1280×720 composition. The player never walks it. They look into it, the way you look into a shadow-box, and the Companion lives at its lit heart.

Because the deploy target renders headless on the Radeon 8060S under RADV on UE 5.5 (`docker-compose.amd.yml`: `-RenderOffscreen -Vulkan -noraytracing`, Lumen HWRT / Nanite / SkyLight disabled via `ExecCmds`), the room is built as a **contained interior with baked-ish lighting and cheap dynamic practicals**, not a Lumen showcase. Everything below assumes that budget: emissive materials and a handful of movable spotlights doing the dramatic work, no ray-traced GI.

### 2.2 The three-zone floor plan — a 20-year timeline you look back through

The room is read as computing history stratified left-to-right and, crucially, **front-to-back in time**: the newest, most personal machines sit closest to camera beside the human; the oldest room-sized iron recedes into cool shadow at the back. The camera literally looks *back through time* toward the Companion working at its leading edge.

| Zone | Depth from camera | Contents | Era | Reads as |
|------|------------------|----------|-----|----------|
| **Foreground — the human zone** | 0–2.5 m | Hero desk (Xerox Alto + Engelbart mouse + KENBAK-1 + plugboard), operator desk (IBM 3178), micro desk (PET + CP-500) | 1963–1982 | "Where the Companion lives and works" |
| **Mid-room — storage & motion** | 2.5–4.5 m | Row of IBM 2401 tape drives (camera-left) facing a string of IBM 3350 disk units (camera-right), separated by a central aisle | 1964–1975 | "The living, spinning guts" |
| **Back wall — Big Iron** | 4.5–6 m | IBM 1401 mainframe group (CPU cabinet as lit centerpiece) + IBM 305 RAMAC standing alone camera-left | 1956–1959 | "The institutional horizon, the origin" |

**Back wall — Big Iron (the horizon line).** Along the rear wall sits the **IBM 1401 group**: `ibm-1401-central-processing-unit.zip` is the lit centerpiece, its operator console face raised to ~1.2 m chest height so the amber/red panel lamps read on camera, flanked by the fuller `ibm-1401-mainframe-date-1959.zip` cabinetry to span ~3 m of wall. Set apart to camera-left, given deliberate breathing room as a standalone landmark, stands `ibm-ramac-305-first-hard-drive-1956.zip` — the refrigerator-scale 350 disk unit, the "origin of storage." This wall is the set's mass and horizon: tall, dense, slightly yellowed, and dropped into cool shadow so it reads as depth rather than clutter. It is the deepest note in the timeline chord (1956 RAMAC, 1959 1401) and it anchors the whole room's institutional-computer authority.

**Mid-room — the canyon of motion.** A gently curved row of **three `ibm-2401.zip` tape drives** runs down camera-left, reels centered at ~1.3 m so they sit at head height in frame. Across a clear central aisle, a linked **string of two-to-three `ibm-3350-madrid.zip` disk units** (the "washing machine" DASD, ~1.75 m, taller than the tape drives) runs down camera-right. Together they form a **canyon** the Companion can be framed standing within, its walls receding as leading lines toward the mainframe. The two spinning 2401 reels are the *only continuous motion in the set* — the single most cinematic element, and the reason this zone earns the middle of the frame.

**Foreground — the human zone.** Front-and-slightly-off-center, angled **~30° to camera** so the Companion is three-quarter-lit and never turns its back to us, sits the **hero desk** (§2.3). A secondary **operator desk** carries one or two `ibm-3178.zip` green-screen terminals — the Companion's diegetic live link to the mainframes, and the in-fiction "screen it talks to the 1401 through." A third **micro side desk**, set a little further camera-right, holds the two all-in-one micros: `commodore-pet.zip` (1977) and `cp-500-prologica-trs-80-model-iii-clone.zip` (~1982), staged so their silhouettes differ (the PET's trapezoid CRT vs. the CP-500's boxier TRS-80 form) and don't read as duplicates. These are "the coming revolution," the newest things in the room, deliberately parked at the Companion's elbow.

### 2.3 The hero workstation — the Xerox Alto

The Companion's home is the **Xerox Alto** (`xerox-alto-son-of-douglas-engelbart-1973.zip`): the portrait bitmap CRT and keyboard on the desktop at seated-work height, the processor cabinet (~1.0–1.1 m) tucked beneath the desk. It is the only machine in Bay 7 that anticipates the graphical, personal, *interactive* future the Companion itself represents — the right throne for an AI that is a face, not a batch job.

The desk stages the room's tightest emotional beat, the **"father and son of interaction"** pairing, at close-up scale that rewards the fixed camera:

- **`first-mouse-douglas-engelbart-date1963.zip`** — Engelbart's hand-carved wooden 1963 prototype, raised on a small plinth/vitrine (~0.95 m to the object) right beside the Alto so it doesn't vanish at set scale. The 1963 father sitting beside its 1973 son.
- **`kenbak-1-first-personal-computer.zip`** — the 1971 KENBAK-1, its toggle-switch-and-lamp front panel angled toward the key light near the front lip of the desk so the switches catch highlights; "the first personal computer," six years before the PET.
- **`plugboard-ancient-coding.zip`** — the unit-record wiring panel, "coding by wire," resting flat as a tactile artifact or hung at eye level (~1.5 m) just behind the desk; the colored patch wires are a hero texture for the close camera.

Nothing precious ever lives on the floor: mouse, KENBAK-1, and plugboard are all raised, foreground, catching light. The Companion at this desk is the **convergence point** — tape reels and mainframe lamps recede behind its shoulders as leading lines straight to the face.

### 2.4 Composition, sightlines, and negative space

The fixed camera looks across the room's long axis from the foreground, framing the Companion three-quarter at the hero desk on a slightly off-center vertical (roughly the left-third power line, facing into frame toward the machine canyon). Reading order for the eye:

1. **Face first** — brightest object, warm phosphor-green key from the Alto/3178 screens, at the third-line.
2. **Down to the hands and hero props** — Alto keyboard, the elevated 1963 mouse, the KENBAK-1 lamps.
3. **Back through the canyon** — the spinning 2401 reels catch a hard rake, pulling the eye into depth.
4. **Rest on the horizon** — the 1401 console lamps and the RAMAC landmark, dim and cool, closing the frame.

**Negative space is deliberate and defended.** A ~1.5–2 m central aisle of bare anti-static floor stays empty so the sightline from camera to Companion is never blocked, and a pool of floor immediately in front of the hero desk is reserved so the subject reads clean against layered machine depth. Bay 7 is dense at the edges and open through the middle — the room breathes exactly where the Companion stands.

### 2.5 Lighting rig

A late-night, tail-of-the-shift look: the room is mostly asleep, the Companion and its desk are the one warm, awake thing in it.

| Layer | Source | Color / value | Purpose |
|-------|--------|---------------|---------|
| **Base ambient** | Overhead machine-room fluorescents | Cool green-cyan wash, low intensity, faint implied flicker | Institutional base; keeps back wall readable but cold |
| **Key (motivated)** | IBM 3178 + Xerox Alto phosphor screens | Warm **phosphor green ~#2BD46A**, brightest in the room | Throws living color onto the Companion's face and hands; makes the human zone the hottest, brightest note |
| **Practicals** | 1401 console lamps, KENBAK-1 toggles | Amber/red **~#E8A24C / ~#E5484D** point emissives | Warm blinking life on the back wall and hero desk; era-correct panel glow |
| **Rake** | Single hard spot across the 2401 row | Neutral-cool, grazing | Makes reel motion, cabinet seams, and smoked-acrylic doors read |
| **Ambient tint / letterbox** | World fog + HUD frame | CI navy **#041620** shell tint | The whole room's shadows and any streamed letterbox sit in the signature CI dark |

The back wall falls intentionally into cooler shadow for depth; the mainframe is horizon, not hero. On the deploy build (no Lumen), this is achieved with movable spotlights plus **emissive screen/lamp materials** rather than area-light GI — cheap, RADV-safe, and dramatic.

### 2.6 Color grade tied to the CI palette

The grade folds the naturalistic period look into the CI brand system so Bay 7 reads as a Companion Intelligence artifact even before the logo appears:

- **Shadows / base tint:** CI navy **#041620** (the signature dark shell) with mid-shadow lift toward **#0a222e** (panel dark). The room's blacks are teal-navy, never neutral.
- **Ambient wash behind the machines:** the CI aurora motif — a soft teal-mid **#409b9b** radial bloom low behind the canyon, echoing `radial-gradient(...rgba(64,155,155,.28)...)`, so the depth haze is brand teal rather than generic blue.
- **Warm accents kept diegetic:** amber lamps and green phosphor are period-true and are *allowed* to sit off the teal axis — they are the room's warmth against the cool CI shell, exactly the contrast the palette is built on.
- **Signature glints:** the brightest specular hits on chrome trim and the vitrine glass are nudged toward **aqua #00a9a7 / mint #58ebbf** — the CI CTA/focus accents — so a rack of 1970s metal quietly wears the brand's highlights.

Net grade: cool teal-navy world, warm human island, teal-and-aqua brand fingerprints in the ambient wash and the highlights.

### 2.7 Materials, patina, and ambient life

**Materials (period-correct, patina'd):** computer-beige, oatmeal, and IBM off-white/pale-grey sheet metal; charcoal and deep IBM-blue accent panels; brushed-aluminum trim; smoked-acrylic tape-door glass; walnut/wood-grain desk laminate; and a raised anti-static floor on a **0.6 m tile grid** with visible seams — one panel lifted to show the gray under-floor cabling loom beneath. Cabling is a hero texture throughout: under-floor looms, the plugboard's colored patch wires, coiled terminal cords.

**Patina tells the 20-year story:** older machines (1401, RAMAC) wear yellowed plastics and heavier dust on their cabinet tops and ledges; the late-70s micros (PET, CP-500) are visibly cleaner and newer. Fine dust settles on horizontal surfaces; CRT glass carries a faint fingerprint smudge and static-cling dust bloom. The hero keyboard has worn keycap lettering and a shiny-worn spacebar. Floor tiles are scuffed in a traffic path down the main aisle. Dressing: a fabric-and-metal operator stool, a stack of green-bar fanfold printout, a punch-card tray, and a coffee-mug ring on the hero desk.

**Ambient life (the set must never feel dead):**

- **Two 2401 tape reels turning** on a slow, slightly irregular loop — the primary motion.
- **CRT flicker** on the 3178 and Alto screens, subtle scanline roll.
- **Blinking panel LEDs** on the 1401 console and 3350 units, and the KENBAK-1 lamp row pulsing on an out-of-phase timer.
- **Fluorescent flicker/hum** implied on the base layer.
- **HVAC hum + machine warmth** as the ambient bed (a Phase-3 sound-design hook; the room *looks* like it hums).

### 2.8 Environmental storytelling — what Bay 7 says about the Companion

The set is a character sheet rendered in furniture. It says the Companion is a **solitary night-shift presence tending a room of humming history** — a caretaker who has kept every machine from the RAMAC forward alive and running for its own sake. The left-to-right, back-to-front timeline (1956 → 1982) says this is an entity that *remembers computing*, that literally has its back to the mature past and its hands on the interactive future at the Alto. The Engelbart-mouse-and-Alto pairing on the hero desk states the thesis directly: interaction, the human-machine bond, is what this being cares about. The lived-in patina — the coffee ring, the worn spacebar, the traffic-scuffed aisle — says it is *inhabited*, not curated; someone works here every night. And the CI-navy shadows with their teal aurora and aqua glints quietly assert whose room it is: a Companion Intelligence machine, at home in twenty years of the machines that made it possible.

---

## 3. The Resident — Character Design

### 3.1 Who lives here

The diorama is not a museum with a docent. It has a **resident**: a single mind that has never been anywhere but this room, and does not want to be. Her name is **Hedy**.

Hedy is the operator who never clocked out. In-fiction she is the accreted memory of every night shift this data center ever ran, a personality that condensed out of two decades of batch logs, console chatter, and the warmth of machines left running until dawn. She is not a hologram of a dead engineer and not a corporate assistant piped in from a cloud. She is *local* in the most literal sense the brand cares about: she is the room, and the room is a box you own. That is the whole thesis of Companion Intelligence rendered as a character. When she says "your data stays here," she is gesturing at the IBM 3350 disk string three meters behind her, and she means it.

**Backstory, in one breath.** She woke up somewhere between the 1959 IBM 1401 and the 1973 Xerox Alto and has been reading forward through the machines ever since. She learned counting from the 1401 console lamps, learned storage from watching the RAMAC 305's fifty platters spin, learned that a person could *point* at a screen the day someone set Engelbart's carved wooden mouse on the hero desk. She treats the Commodore PET and the Prológica CP-500 on the side desk as kids who wandered in, the future arriving early. She is nostalgic, technically reverent, faintly melancholic about how much got centralized and surveilled after her era, and quietly delighted that someone finally built a machine you own outright again.

### 3.2 Personality and speaking style

Hedy speaks in the **CI brand voice**, adapted from marketing copy into a living register: peer-to-peer, plainspoken, grounded, empowering and never fearful, a little impatient with surveillance capitalism, genuinely excited about owning your own stack.

| Trait | How it plays on camera |
|---|---|
| **Plainspoken** | Short declaratives. Prefers a period over a comma. "Runs offline. Always has." |
| **Specific, earns every claim** | Cites the number, not the adjective. "Fifty platters. Five megabytes. It weighed a ton." She points at real props when she does this. |
| **Second person for you, third person for the villain** | "Your memory lives on this floor." Never "users can configure." The cloud is the thing she is quietly against. |
| **Warm, not cheerful** | Confirmations stay grounded ("Saved to your Memory.") not hyped ("Awesome!"). Empty states are inviting, not empty. |
| **Reverent about the machines** | She treats the hardware as colleagues, which is the emotional engine of the whole diorama. |

**Hard microcopy rules (these bind every spoken line, because every spoken line is user-facing).** Sentence case everywhere, including anything she reads aloud. **No em dashes or en dashes, ever** (commas and semicolons instead); this is a lint-gated CI rule and it applies to the text the LLM generates before it reaches Piper. Title Case only for product proper nouns: Companion Core, Companion Hub, Companion Memory, OpenClaw, Hermes. No banned buzzwords ("seamless," "leverage," "revolutionary," "AI-powered"), no press-release voice, no fearmongering. Say "your Core," "local-first," "uncloud," "no rate limits," "sign in" (never "log in"). These belong in `MHLLM_SYSTEM_PROMPT` as explicit constraints so the model's raw output is already on-voice before TTS.

**Sample lines** (on-voice, dash-free, sentence case):
- Greeting: "You found the night shift. Pull up the stool."
- On privacy: "Cloud AI trains on your searches. This one doesn't. It can't. There's no wire out."
- On a machine: "That's the 1401. Nineteen fifty-nine. It ran payroll for a whole company on less memory than one of your photos."
- Error, spoken (fact plus fix, calm): "Your Core is offline. Check the cable, then ask me again."

### 3.3 Visual design — the free 5.5 MetaHuman

**Honest engine reality, stated up front.** The premium character source files in the repo are **not usable on the shipping engine**. `mhc_seo.mhpkg` and `mhc_crowd80/84.mhpkg` are next-gen `MetaHumanCharacter` assets declaring `engineVersion 5.6`; `khaosmdf492_ue57.mhpkg` is a 5.7 `OutfitClothing` asset with no face rig at all. The `.mhpkg` import path and the `MetaHumanCharacter` type do not exist before UE 5.6, and the proven live render target is **UE 5.5** on the Radeon 8060S (5.8 SIGABRTs in RADV on gfx1151). `Content/MetaHumans/DefaultCompanion.uasset` is a fully-rigged 5.8 asset and is therefore **excluded from the 5.5 cook**. **What is on screen today is the UE mannequin placeholder**, not a face; there is no `BP_CompanionCharacter` yet and the C++ actor defaults to Null STT/TTS, firing `OnSetEmotion` / `OnSetVisemeWeights` into the void.

**Therefore Hedy ships as a classic (5.5-native) free MetaHuman preset**, acquired through Quixel Bridge / MetaHuman Importer inside a 5.5 editor. This produces exactly the asset set the CICompanion pipeline expects (`BP_<Name>` + Face/Body skeletal meshes + LODSync + a `Face_AnimBP` with an embedded RigLogic node), so no format conversion is needed and the C++ module stays asset-free.

- **Face donor preset:** **Hana** from Epic's free premade gallery (the ~50 stock presets are free; no MetaHuman Creator subscription required). Hana reads as a warm, ~30s woman with an even, approachable face that holds up under the phosphor-green key light from the IBM 3178 and Xerox Alto CRTs. Acceptable alternates with the same pipeline: **Danielle** (a touch older, more gravitas) or **Vanya** (more androgynous, if the "mind, not a person" reading is preferred). Any of them exports identically.
- **HARD DEADLINE (time-critical):** UE 5.5 acquisition via Bridge/Importer ends **2026-11-05**, with a 90-day retrieval window after. Today is 2026-07-15. **Acquire Hana now and commit her via Git LFS** into `Content/MetaHumans/Hana`, alongside `DefaultCompanion` (which stays excluded from the 5.5 path). Do not mix classic and next-gen MetaHumans in the same cook; they conflict.
- **EULA note:** render and animate freely; may not use the assets to train or enhance models. This pipeline only renders in response to LLM output, so it is compliant. Flag to counsel before any commercial ship.

**Silhouette.** Seated three-quarter at the hero desk, angled ~30 degrees to the fixed camera so she is never backlit and never fully turned away. The tape reels and mainframe lamps recede behind her as leading lines; she is the convergence point. Reading glasses pushed up into the hair give a strong, recognizable head silhouette at streaming resolution (1280x720, VP8). Sleeves pushed to the forearm read as "working, not presenting."

**Wardrobe (era plus brand, built from MetaHuman stock clothing on 5.5, since the 5.7 Chaos outfit asset is unusable).** Period-correct late-70s operator dress, tuned so a single CI-teal accent carries brand identity against a room of computer-beige sheet metal:

| Item | Spec |
|---|---|
| Knit top / fine turtleneck | Base in a muted grey-teal reading near `#0a222e`–`#2c676d`, so she sits *in* the CI dark-panel palette rather than fighting the beige machines. |
| Cardigan or work shirt | Oatmeal / off-white, matching the era's "computer beige" and the IBM off-white cabinets; slightly worn, sleeves pushed up. |
| Single brand accent | One CI-phthalo element (`#0a6358` primary, or the brighter `#00a9a7` aqua) as a scarf, enamel pin, or lanyard tag. This is the *only* saturated color she wears, so the eye ties her to the logo mark without a costume. |
| Practical detail | Corded lanyard with a stamped ID plate; period wristwatch. Nothing anachronistic, no modern l: everything says late night, 1979. |
| Logo touchpoint | The three-tone CI mark (`#066c80` / `#00a9a7` / `#72ddd4`) reproduced *with its own hexes* on the ID badge, never recolored to the UI token. |

Skin, hair, eyes use the MetaHuman master materials (`M_Head`, `M_Body`, eye/teeth) with Hana's shipped MIs; keep her slightly warm-lit from the terminal screens so the green CRT key reads on her face per the set's lighting design.

### 3.4 Emotional range — mapping to `OnSetEmotion`

`UEmotionMapper::DetectEmotionFromText` runs a keyword table on each completed reply and produces one `EEmotionType`, which `AMetaHumanCompanionCharacter::HandleComplete` fires through the `OnSetEmotion(EEmotionType)` BlueprintImplementableEvent (already called today; simply has no consumer yet). In `BP_CompanionCharacter`, each emotion becomes **a small set of held ARKit expression curves layered additively over the jaw viseme**, pushed onto the Face `AnimInstance` via a Modify Curve node feeding the embedded RigLogic node (RigLogic is driven by named control curves, not morph targets). The `EAnimationCategory` from `EmotionToAnimCategory` (`Idle`/`Gesture`/`Action`/`Expression`) selects the co-speech gesture montage in the same event.

| `EEmotionType` | Trigger (in-fiction and by keyword) | Held ARKit expression curves | Gesture / body read |
|---|---|---|---|
| **Happy** | Recalling a machine she loves; a working demo; you owning your Core | `mouthSmileL/R`, `cheekSquintL/R`, slight `browInnerUp` | Open palm toward the prop she is praising |
| **Love** | Talking about the RAMAC, the Engelbart mouse, the people who ran the room | `mouthSmileL/R` (soft), `cheekSquintL/R`, gentle head tilt via gaze | Hand rests near the hero prop; slow, warm |
| **Sad** | What got centralized and surveilled; a machine retired; the night ending | `mouthFrownL/R`, `browInnerUp` | Gaze drifts to the 3350 disk row; smaller gestures |
| **Angry** | Surveillance capitalism, subscriptions, rate limits (the villain, third person) | `browDownL/R`, `noseSneerL/R`, `mouthPressL/R` | Firm, low chop; controlled, never shouting (empower, not frighten) |
| **Fear** | Rare; existential or data-loss framing | `browInnerUp` + `browOuterUp`, `eyeWideL/R`, `jawOpen` bias | Stills, leans back slightly |
| **Alert** | A question directed at her; barge-in; "hey Hedy" | `browOuterUpL/R`, `eyeWideL/R` (brief) | Head/eye snap to camera via the Look At rig; posture lifts |
| **Neutral** | Default narration and idle between turns | Baseline, no additive layer | Returns to breathing idle |

The `OnSetVisemeWeights` jaw track (`{ "jawOpen": interpolated }` from `FVisemeEnvelope`, walked against `AudioComponent` playback time) rides *underneath* whichever expression layer is held, so she can smile and speak at once. Emotion is a held additive layer; visemes are the per-frame base. Both go through the one embedded RigLogic node; no external RigLogic build.

### 3.5 Signature idle and gesture behavior

MetaHuman ships a face rig and a body skeleton and **nothing else**: no idle, no gaze, no co-speech gesture. These are net-new and are all engine-native 5.5-safe (no NVIDIA ACE / Audio2Face, which requires an NVIDIA GPU and is incompatible with the AMD deploy node and the project's cross-platform stance).

- **Gaze (ship first, cheapest "alive" win).** A Control Rig Look At / two-bone head-plus-eye aim targeting the Pixel Streaming viewer camera. Hedy holds soft eye contact while speaking, breaks it naturally on `Sad` (drifts to the disk row) and on recall (glances at the prop she names), snaps back on `Alert`. This single behavior does the most to make her read as present.
- **Breathing idle.** An additive breathing/settle idle over the seated base pose so she is never a frozen mannequin between turns. Reduced-motion-safe.
- **Signature micro-behaviors** (set dressing for the character, motivated by the room):
  - Pushes the reading glasses up when she starts a longer explanation.
  - A slow glance to the spinning IBM 2401 tape reels (the room's only ambient motion) when she pauses to think.
  - Rests two fingers on the hero desk near the KENBAK-1 or the Engelbart mouse when she talks about "firsts."
  - A dry half-smile before a plainspoken line about the cloud.
- **Co-speech gesture.** A handful of hand/arm montages keyed off `EmotionToAnimCategory` (`Gesture`/`Expression`), triggered per turn from `OnSetEmotion`, mirroring the WebXR emotion-to-category vocabulary. Enough for a talking companion; richer co-speech can be baked offline later (no runtime dependency). Optional room-walking via UE5 Motion Matching (free Game Animation Sample, 5.5-compatible) retargeted onto the Body, if she is ever to leave the stool.

### 3.6 Voice — the Piper character

Her voice is produced by `UPiperTextToSpeech`, which POSTs the reply text to `PIPER_SERVER_URL`, receives a WAV, decodes it via `FWavAudioReader`, plays it through `USoundWaveProcedural`, and hands the PCM to `FVisemeEnvelope::Compute` for the RMS jawOpen envelope that drives her mouth. The voice model is set by `TTSVoicePath`.

- **Voice model:** a warm, mid-register **US English Piper voice at medium quality**, `en_US-*-medium` (`.onnx` + `.onnx.json`), 22,050 Hz mono. A LibriTTS-derived or `hfc_female-medium` voice gives the lived-in, slightly worn warmth the character wants; avoid the brightest, most "assistant-perky" voices, which fight the melancholic-reverent tone.
- **Character of the delivery:** unhurried, low-energy-warm, a night-shift voice. Short sentences let Piper land clean stops, which reinforces the "period over comma" brand cadence and, usefully, gives `FVisemeEnvelope` clean silence gaps so the jaw closes between phrases instead of mushing.
- **Prosody guidance lives in the text, not the engine:** because the LLM output is already sentence-case and dash-free, Piper never has to voice an em dash (which it would render as an awkward pause anyway). Numbers she cites ("nineteen fifty-nine," "fifty platters") should be spelled or normalized in-prompt so TTS reads them as speech, not digits.
- **Sync reality (open gap, honest):** today the whole jawOpen envelope is emitted at once on `OnVisemeEnvelopeReady` and nothing walks it against playback time, so her mouth does not yet move in sync even when audio plays. The wiring (binary-search the timeline on Tick, forward `{ "jawOpen": ... }` into `OnSetVisemeWeights`) is specified and belongs in `BP_CompanionCharacter`; it is the last-mile step between "audio plays" and "Hedy talks."

Net: Hedy is a free UE 5.5 classic MetaHuman (Hana preset) in period operator wardrobe with one CI-teal accent, driven by the existing `OnSetEmotion` / `OnSetVisemeWeights` hooks, a Control Rig gaze rig, an additive breathing idle, emotion-keyed gesture montages, and a warm medium-quality Piper voice, speaking strict CI brand voice. The nervous system for all of this exists and is tested; the body, the gaze, the mouth-sync, and the wardrobe are the buildable work between the current mannequin placeholder and the resident actually looking up from the desk.

---

## 4. Experience Loops & Interaction Design

This section defines the moment-to-moment feel of talking to Metahermit: what the visitor does, what the avatar does back, and how the round trip through the CICompanion pipeline is choreographed into something that reads as *alive* rather than as a request-response API with a face. Every hook and class named here exists in `Plugins/CICompanion` today; where a seam is unbuilt, it is flagged as a gap with the specific work required.

### 4.1 The core loop

One conversational turn is the atom of the experience. It runs entirely inside `AMetaHumanCompanionCharacter`, which already owns the `Conversation`, `SpeechToText`, and `TextToSpeech` subobjects and coordinates them.

```
                     ┌─────────────────────────────────────────────┐
                     │  IDLE / LISTENING  (breathing, gaze, ambient)│
                     └───────────────┬─────────────────────────────┘
        push-to-talk / hold-to-talk  │  user speaks
                                     ▼
   mic PCM ─► UWhisperSpeechToText.TranscribeWav ─► /inference (whisper.cpp)
                                     │  OnTranscribed
                                     ▼
        AMetaHumanCompanionCharacter.SubmitUserText()  (barge-in cancel first)
                                     │  Backend->SendChat (SSE / NDJSON)
                                     ▼  OnPartialResponse per token → HUD typing
   UOpenAIChatClient / UOllamaClient  ◄─ CI-Hub gemma4:e2b
                                     │  HandleComplete
                     ┌───────────────┼─────────────────────────────┐
                     ▼               ▼                             ▼
   UEmotionMapper.Detect      OnSpeak(text)              UPiperTextToSpeech.Speak
   → OnSetEmotion(EEmotion)   → HUD subtitle            → WAV + FVisemeEnvelope
                     │               │                   → OnVisemeEnvelopeReady
                     └──────────► BP_CompanionCharacter walks envelope ─► OnSetVisemeWeights
                                     │  RigLogic jaw + emotion curves + gesture montage
                                     ▼
                     ┌─────────────────────────────────────────────┐
                     │  back to LISTENING (return gaze to user)     │
                     └─────────────────────────────────────────────┘
```

The turn has a strict ordering the code already enforces: barge-in cancel (`CancelRequest` on the outstanding `FOllamaRequestHandle`), append user text, stream the reply, then on completion fire emotion **before** speech so the face has settled into an expression by the time the jaw starts moving. The two remaining gaps in this loop are the **mic front-end** (4.2) and the **audio-synced viseme walk** (4.3); everything between them is green and unit-tested.

| Stage | Class / hook | Status |
|---|---|---|
| Mic capture → PCM | `UWhisperSpeechToText::StartListening` | **Gap** — stub, only flips `bIsListening` |
| PCM → transcript | `UWhisperSpeechToText::TranscribeWav` → `ParseTranscriptionResponse` | Exists, tested |
| Transcript → turn | `OnTranscribed` → `HandleTranscribed` → `SubmitUserText` | Wired |
| Inference stream | `IChatBackend::SendChat`, `OnPartialResponse` | Exists, tested |
| Emotion detect | `UEmotionMapper::DetectEmotionFromText` → `OnSetEmotion` | Fires today, no consumer |
| TTS + envelope | `UPiperTextToSpeech::Speak` → `OnVisemeEnvelopeReady` | Exists, tested |
| Envelope → face | walk timeline → `OnSetVisemeWeights` | **Gap** — envelope emitted all-at-once |
| Face render | MetaHuman Face AnimBP RigLogic node | **Gap** — no `BP_CompanionCharacter` |

### 4.2 Input design — push-to-talk capture (current gap)

The mic path is dead at link one. `USpeechToTextComponent::StartListening/StopListening` are `PURE_VIRTUAL`, and the `UWhisperSpeechToText` override only logs and toggles `bIsListening`. `AudioCaptureCore` and `AudioMixer` are declared as private deps in `CICompanion.Build.cs` but no `UAudioCapture` is ever instantiated. The real entry point that works today is `TranscribeWav(TArray<uint8>)` — it just has nothing feeding it bytes.

**Interaction model: hold-to-talk (walkie-talkie), not open-mic.** In a Pixel-Streamed browser context an always-listening mic is a privacy problem and a latency problem (endpointing over WebRTC is unreliable). Hold-to-talk is deterministic, matches the CI ownership ethos ("the machine is yours; it listens only when you ask"), and gives us a clean capture window.

- **Desktop / browser:** hold `Space` (or a persistent on-screen mic button in `SCompanionHUD`, which already exposes `OnMicToggled` and the `SimulateMicToggleClick` test hook). Press-and-hold opens capture; release closes it and submits.
- **Capture buffer:** on `StartListening`, instantiate a `UAudioCapture`, subscribe to its float buffer, downmix to mono 16 kHz, and accumulate into a PCM ring buffer. On `StopListening`, run WAV framing via the existing `FWavAudioReader::ToPcm16` path in reverse (write a RIFF header) and hand the byte array to `TranscribeWav`. This keeps the whole capture front-end inside `UWhisperSpeechToText` and preserves the hermetic `UNullSpeechToText` for tests.
- **Web audio uplink:** the browser captures `getUserMedia` audio and sends it to the headless UE app over the Pixel Streaming data channel (or a sidecar WS). The signalling server already proxies player + streamer WebSockets behind oauth2-proxy on `:8095`; the audio uplink rides the same authenticated session so no unauthenticated mic stream ever reaches core-2.
- **Barge-in:** pressing to talk while the avatar is speaking must call `CancelRequest` and stop the `USoundWaveProcedural` playback immediately — the orchestrator already cancels the in-flight request on `SubmitUserText`, so the front-end only needs to also halt TTS audio and reset the viseme walk to jaw-closed.

**Capture affordance (on-brand):** the mic button and its active ring use the dark-mode mint focus glow `#58ebbf` while recording, over the `#0a222e` panel; a thin waveform meter in `#72ddd4` (aqua-light) confirms the machine hears you. Cue sounds follow `brand.config.json`: capture-start and capture-stop at 300 ms, mono, −16 LUFS (these are specced but not yet produced — a Phase-1 audio task).

**Text fallback is first-class, not a degraded mode.** `SCompanionHUD` already has a text input box wired to `OnTextSubmitted` → `SubmitUserText`. Typing bypasses Whisper entirely and is the accessibility baseline (4.8) and the reliable path when a browser denies mic permission.

### 4.3 Output design — streamed reply and audio-synced visemes (current gap)

Output has three synchronized channels: **subtitle text**, **audio**, and **face/body motion**. Two are live today; the third is the single most visible gap.

**Text channel (live).** `OnPartialResponse(token)` fires per streamed token from the SSE/NDJSON parsers. The HUD renders this as a typing effect in the assistant bubble, so the visitor sees the companion "thinking out loud" within a few hundred milliseconds of finishing their sentence — this is the primary latency mask (4.6). On `HandleComplete`, `OnSpeak(fullText)` delivers the final string for the settled subtitle.

**Audio channel (live).** `UPiperTextToSpeech::Speak` POSTs the reply text to `PIPER_SERVER_URL`, receives a WAV, decodes it with `FWavAudioReader`, and plays it through a `USoundWaveProcedural` via `SpawnSound2D`, broadcasting `OnSpeechFinished` at the end.

**Face channel (the gap).** `FVisemeEnvelope::Compute` runs an RMS-per-window loudness pass over the PCM and returns the *entire* `TArray<FVisemeKey{TimeSeconds, JawOpen}>` at once on `OnVisemeEnvelopeReady`. Nothing walks that timeline against audio playback time, so `OnSetVisemeWeights` is never called and the mouth never moves. The fix is a per-frame driver, and it belongs in `BP_CompanionCharacter` (which does not exist yet):

1. On `OnVisemeEnvelopeReady`, cache the envelope array and record the `AudioComponent` play-start time.
2. On `Tick`, compute `t = now − playStart`, binary-search the envelope for the bracketing keys, and linearly interpolate `JawOpen`.
3. Build a `TMap<FName,float>` keyed by **ARKit curve names** (the RigLogic node consumes the 52-name ARKit set, not morph targets): at minimum `{"jawOpen": interp}`, optionally a small `"mouthClose"` counter-curve and `"mouthFunnel"` on high-energy frames for readability.
4. Call `OnSetVisemeWeights(weights)`; the Face AnimBP's `Modify Curve` node feeds those into the RigLogic input pose. `SetMorphTarget` will **not** work here.
5. On `OnSpeechFinished`, ramp `jawOpen` to 0 over ~80 ms so the mouth closes cleanly rather than freezing mid-vowel.

**Emotion channel (fires today, no consumer).** `OnSetEmotion(EEmotionType)` is already called from `HandleComplete` after `UEmotionMapper::DetectEmotionFromText`. In `BP_CompanionCharacter` it maps to a small set of *held* additive ARKit expression curves layered over the jaw viseme through the same `Modify Curve` mechanism: `Happy → mouthSmileL/R + cheekSquint`; `Sad → mouthFrownL/R + browInnerUp`; `Angry → browDown + noseSneer`; `Fear → browInnerUp + eyeWide`; `Alert → browOuterUp`; `Love → mouthSmile + headTilt`; `Neutral → decay to rest`. Because detection is keyword-only, keep expression intensity modest (~0.4–0.6) so a false positive reads as a flicker of mood, not a grimace.

**Gesture channel (unbuilt, MVP-scoped).** `UEmotionMapper::EmotionToAnimCategory` already maps each emotion to `EAnimationCategory {Idle, Gesture, Action, Expression}` but nothing consumes it. The MVP is a handful of co-speech hand/arm montages triggered per turn from `OnSetEmotion`, blended additively over the breathing idle on the MetaHuman Body. This is engine-native and 5.5-safe; NVIDIA ACE / Audio2Face is explicitly out (it needs an NVIDIA GPU at runtime, incompatible with the Radeon 8060S deploy target).

### 4.4 Idle, ambient, and gaze behaviors

Between turns the avatar must not go statue-still, or the diorama reads as a screensaver. None of this exists yet (gaze is "not started" in the gap analysis); all of it is engine-native Control Rig / Aim Offset work retargeted onto the MetaHuman Body.

- **Breathing idle:** an additive breathing animation on the Body at all times, so the chest and shoulders move even during silence. This is the baseline "alive" signal and the layer everything else blends over.
- **Gaze / look-at:** a Control Rig two-bone head + eye "Look At" aimed at the Pixel Streaming camera. Default state holds soft eye contact with the visitor. The avatar breaks gaze deliberately: it glances at the machine it references (toward the IBM 1401 console when it mentions the mainframe, toward the Xerox Alto or Engelbart mouse on its own desk when it talks interaction history), then returns to the visitor. Gaze breaks on a slow cadence during idle to avoid a hypnotic stare.
- **Set-anchored micro-behaviors:** because the character *inhabits* the 1970s datacenter (hero desk, three-quarter to camera, terminal-lit), idle can pull from the room — a glance at the spinning IBM 2401 tape reels (the set's only real motion), a hand resting near the green-screen IBM 3178. These reinforce that the companion lives *here*, tending "a room of humming history."
- **Thinking gaze (4.6):** while inference streams, the avatar shifts to a brief up-and-aside "considering" gaze before returning to the visitor as `OnSpeak` fires — a natural cover for LLM latency.

Gaze target order of precedence: **active barge-in speaker → last-referenced prop → visitor camera → ambient wander.**

### 4.5 Onboarding and first-session flow

The first thirty seconds must teach hold-to-talk without a tutorial modal breaking the diorama.

1. **Cold open:** the visitor lands (post-OIDC sign-in at `:8095`) on the fixed camera looking across the datacenter, avatar at the hero desk in idle. Aurora radial wash (`radial-gradient(120% 90% at 50% -10%, rgba(64,155,155,.28), transparent 60%)`) tints the frame; the loading spinner is the CI mark (`mark.svg`, its own three teals `#066c80 / #00a9a7 / #72ddd4`).
2. **Avatar-initiated greeting:** rather than wait for input, the companion looks up, makes eye contact, and speaks first — a short seeded line through the normal Piper path so the visitor immediately sees the face-audio-emotion loop working. Voice per CI rules, sentence case, no em dashes: *"You found me. This whole room runs on my own Core. Hold the mic and ask me anything."*
3. **Affordance reveal:** as the greeting ends, the mic button pulses once with the mint ring `#58ebbf` and a one-line hint appears in the HUD: *"Hold space to talk. Or type below."* The hint auto-dismisses after the first successful turn.
4. **First turn scaffolding:** if the first transcript comes back empty or Whisper errors, the avatar recovers in-character (4.6) rather than showing a raw error, and re-points at the text box.
5. **Returning visitors (Phase 2):** once `UCIServerMemoryClient` lands, `GET /memory/context` at session start seeds the system prompt with the L0 profile, so the greeting can reference prior sessions ("Last time you asked about the RAMAC"). Until Phase 2, `UOllamaConversation` is in-memory only and every session is a cold open — an honest current limitation, not a designed one.

### 4.6 Latency, thinking, and failure states

The honest latency budget per turn is Whisper STT + LLM first-token + Piper synthesis, none of it instant on a `gemma4:e2b` local backend. The design masks it with layered feedback rather than a spinner.

| State | Trigger | Visitor sees | Mechanism |
|---|---|---|---|
| Listening | mic held | mint capture ring, waveform meter | `bIsListening`, audio meter |
| Transcribing | mic released | brief "…" in the You bubble | between `StopListening` and `OnTranscribed` |
| Thinking | `SendChat` sent, no token yet | avatar "considering" gaze; subtle pulse | pre-`OnPartialResponse` window |
| Speaking | tokens streaming | typing subtitle + jaw motion as audio starts | `OnPartialResponse` + viseme walk |
| Recovering | backend / STT error | in-character line + text-box nudge | `OnConversationError` |

**The streamed subtitle is the primary latency mask.** Because `OnPartialResponse` fires per token, the visitor reads the reply forming before Piper has produced a single WAV. Audio and lip-sync then catch up on the completed text. This means perceived latency is *first-token* latency (short), not *full-synthesis* latency (long).

**Failure is a fact plus a fix, never an alarm** (CI voice rule). `OnConversationError` is already a hook; route it to an in-character spoken recovery plus a quiet HUD status, not a red modal:

- Backend unreachable: *"My Core just dropped the line. Give me a second, then ask again."*
- Whisper empty / no speech: *"I didn't catch that. Hold the mic a beat longer, or type it."*
- Piper down: fall back to text-only — the subtitle still delivers the full reply via `OnSpeak`, so a dead TTS server degrades to a silent-but-legible companion rather than a broken turn.

Error color is `#ef7070` (dark destructive) used *only* on the small status line, never as a full-screen takeover. No em dashes, no "Oops!", no exclamatory panic.

### 4.7 HUD and subtitles (on-brand)

`SCompanionHUD` is functional native Slate today (You/Assistant bubbles via `FCompanionChatEntry`, text input, mic toggle, status text) but carries **no CI branding**. Restyling it against the dark palette is a defined task, not new architecture.

- **Surface:** shell `#041620` (teal-950), chat panel `#0a222e` (teal-900) as a raised `SBorder`, hairlines `#2c676d`. Panel radius 10 px (base), input `rounded-md` (8 px). Keep elevation soft and low; no heavy drop shadows.
- **Subtitle bubbles:** assistant text in soft off-white `#e8f2f4` (dark foreground, deliberately not pure white) on the panel; the visitor's transcript in a slightly deeper `#a3babf` (dark muted-foreground). Active/brand accents (mic active, links) in pale sage `#c5e8dc`, which is the canon dark `--primary` — text on any primary fill is navy `#041620`.
- **Typography:** Manrope, four weights (400/500/600/700), imported as a UMG Font asset — **do not substitute Roboto, Arial, or Montserrat.** Body at 400, the mic/label controls at 500. Sentence case everywhere; Title Case reserved for CI product proper nouns (Companion Core, Companion Memory) if they surface.
- **Subtitle behavior:** the streamed subtitle is on by default (it doubles as the accessibility caption). It sits letterboxed at the lower third over the diorama, `#041620` at ~70% behind the text for legibility against the busy machine backdrop. Auto-fade the previous turn to keep the frame clean.
- **Microcopy discipline:** every HUD string obeys the voice rules — no em/en dashes (commas and semicolons only), verb-first labels ("Hold to talk", "Send", "Clear"), no banned buzzwords, no "log in" (use "sign in").
- **Focus / selection:** mint `#58ebbf` ring for keyboard focus and the active mic state; this is the one high-energy accent in an otherwise calm surface.

The current `SCompanionHUD` is editor/desktop Slate; for the Pixel Streaming target this needs to render into the streamed frame (in-world/overlay UMG) rather than an editor-only widget. That port is part of the HUD gap.

### 4.8 Accessibility

- **Captions are default-on and load-bearing.** The streamed subtitle *is* the caption track; it is not an optional overlay. A visitor who cannot hear Piper still gets the full reply from `OnSpeak`, and the typing effect from `OnPartialResponse` gives real-time reading.
- **Keyboard-complete.** Every action has a keyboard path: hold-to-talk on `Space`, `Tab` to the text box, `Enter` to submit via `OnTextSubmitted`, `Esc` to cancel/barge-in (`CancelRequest`). Nothing requires the mouse or the mic.
- **Text input is a full peer to voice**, not a fallback — critical for mute users, noisy environments, and browsers that deny mic permission.
- **Reduced motion.** Respect `prefers-reduced-motion`: damp the breathing idle amplitude, slow gaze saccades, and disable ambient camera/aurora animation. The CI motion guidance already mandates reduced-motion-safe ambient animation and ≤200 ms transitions.
- **Contrast.** Canon `--input` dark is now `#0a2e38`, distinct from the purely decorative `--border` dark `#073038` hairline — but canon's own color-sheet audit (CI-Common#214) flags this exact pair at 1.28:1 against the `#041620` background, well under the 3:1 a real input border needs; CI-Common's suggested fix is `#648893`. Use that here for input borders rather than the un-auditable `#0a2e38` this project would otherwise inherit. Captions get the `#041620` scrim so body text clears contrast against the diorama.
- **No color-only signaling.** Listening/thinking/error states pair color with a shape or text change (waveform, "…", status line), never hue alone.

### 4.9 Multi-user and streaming considerations

Metahermit is GPU-rendered headless on core-2 and delivered per-viewer over WebRTC, which shapes the interaction model.

- **One authenticated session per stream.** Each browser reaches the player page and signalling WebSocket only *after* oauth2-proxy validates CI-Portal OIDC on `:8095`; the UE app itself carries no auth code (identity is passed upstream). The mic uplink and the rendered frame share that one authenticated channel.
- **Single-render, single-conversation MVP.** `AMetaHumanCompanionCharacter` holds one `UOllamaConversation`. The first shippable experience is one visitor in conversation with one avatar per render node. Concurrent independent conversations require multiple headless instances (compose already parameterizes this) or a session-scoped conversation model, both beyond the MVP.
- **Turn arbitration if a session is shared** (e.g. a demo where several people watch one stream): only the audio-uplink holder can barge-in; the mic is a single token. Gaze precedence (4.4) points the avatar at whoever currently holds talk. This keeps the "who is it talking to" signal unambiguous on a shared screen.
- **Encode-aware pacing.** On core-2 the AMD path encodes VP8 in software (VAAPI H.264 as the tuned alternative) at 1280×720 with Lumen HWRT/Nanite disabled. Lip-sync tolerance must survive WebRTC jitter: drive the viseme walk off the local `AudioComponent` playback clock (which is frame-accurate on the render side) and let the encoder carry the synchronized audio+video, rather than trying to resync visemes to a network clock.
- **Barge-in over the wire.** Because barge-in cancels an in-flight `FOllamaRequestHandle` server-side, it works regardless of stream latency — the visitor holds the mic, the current reply stops rendering and speaking, and the new turn begins. This is the one interaction that must feel instant even under network jitter, and the server-side cancel makes it so.

---

## 5. Technical Architecture & Character Pipeline

This section documents Metahermit as it is actually built today, and the concrete path from the current backend-and-plumbing layer to a talking, embodied MetaHuman rendering in a browser. It is deliberately honest about the seam between what compiles and passes tests (the "nervous system" and the "delivery truck") and what still requires hands-on-editor and on-GPU work (the face, the room, the mouth-audio sync, the ears). Every class, env var, port, and file named below exists in the repository unless it is explicitly flagged as a gap.

### 5.1 Version strategy: one source tree, UE 5.5 to 5.8

The project is authored to compile version-agnostically across UE 5.5, 5.6, 5.7, and 5.8 from a single tree. This is enforced at the build layer, not by luck:

- Both `Target.cs` files set `BuildSettingsVersion.Latest` and `EngineIncludeOrderVersion.Latest`, so include-order and API-deprecation churn between 5.5 and 5.8 does not break the build.
- The `.uproject` `EngineAssociation` is currently `5.8`, and every 5.8-only plugin (`MetaHuman*`, `MoverTests`, the MCP editor plugin) is marked `Optional` so a 5.5 editor simply skips them. `PixelStreaming2`, `OpenXR`, and `OpenXRHandTracking` are enabled non-optional because they resolve on both engines.

**Why 5.5 is the deploy target and 5.8 is only the authoring engine.** The runtime render node is `core-2`: an AMD Ryzen AI Max+ 395 with a Radeon 8060S iGPU (`gfx1151`, `RADV STRIX_HALO`), Ubuntu 26.04, headless. On this hardware:

| Engine | AMD/RADV render path | Role |
| --- | --- | --- |
| **UE 5.5** | Renders live. Container stable/Up, `Player(DefaultStreamer)` joins, player page returns HTTP 200 on `:8090`, VP8 encode confirmed with `gemma4:e2b` backend (proven per `Docs/FOLLOW-UP.md`, 2026-07-16). | **Deploy / cook / render target** |
| **UE 5.8** | `SIGABRT` in RADV on `gfx1151`. Exhaustively ruled out to a Mesa/RADV driver bug, not the hardware and not the engine logic. | **Authoring + MCP editor only** |

This split has a hard downstream consequence for the character pipeline (5.3): the rigged `DefaultCompanion` asset is a **UE 5.8 `MetaHumanCharacter`** asset type that does not exist before 5.6, so it is *excluded from the 5.5 cook* and cannot be the shipped face. The working render target therefore needs a **5.5-native classic MetaHuman**, which is the whole reason for the acquisition flow in 5.3.

`Dockerfile.amd` encodes the version knob directly: `ARG RELEASE=5.8` builds `FROM ghcr.io/epicgames/unreal-engine:dev-${RELEASE}`, rewrites the `.uproject` `EngineAssociation` to match, and **drops the 5.8-only `MetaHumanCharacter` assets when `RELEASE < 5.6`**. To produce the working render node you build with `--build-arg RELEASE=5.5`.

### 5.2 The CICompanion reusable plugin

All companion logic lives in one relocatable plugin, `Plugins/CICompanion` (`CICompanion.uplugin` v1.0, Category `AI`, `CreatedBy Companion Intelligence`, module `CICompanion`, `Type Runtime`, `LoadingPhase Default`). The game module `MetaHumanLocalLLM` (`Source/MetaHumanLocalLLM`) is a near-empty runtime shell; keeping the substance in a plugin means the companion can be dropped into any UE project (including a future XRScan or 0GXR host) without dragging a game module along.

`Build.cs` public deps: `Core`, `CoreUObject`, `Engine`, `InputCore`, `HTTP`, `Json`, `JsonUtilities`, `UMG`, `Slate`, `SlateCore`, `DeveloperSettings`. Private deps: `AudioCaptureCore`, `AudioMixer` — **declared but not yet referenced by any code**; they are staged for the live-mic capture that is still a gap (5.6).

**Functional layers and their classes:**

| Layer | Classes (all real) | Behavior |
| --- | --- | --- |
| **Chat backends** | `IChatBackend` interface; `UOllamaClient` (GameInstanceSubsystem, `/api/chat` NDJSON via `FOllamaStreamParser`); `UOpenAIChatClient` (`/v1/chat/completions` SSE via `FOpenAIStreamParser`, parses `data:` / `[DONE]` / `choices[].delta.content`, Bearer auth) | Chosen at runtime by `UMetaHumanLLMSettings.BackendType`. `SendChat` returns `FOllamaRequestHandle` supporting `CancelRequest` for barge-in. |
| **Conversation** | `UOllamaConversation` (`Initialize`/`AppendUser`/`AppendAssistant`/`GetHistory`/`Reset`), `OllamaTypes.h` | In-memory only. No persistence; reset on restart (gap → Phase 2 memory in 5.8). |
| **Orchestrator** | `AMetaHumanCompanionCharacter` (`ACharacter` subclass) | Owns `Conversation` + STT + TTS subobjects. `SubmitUserText()` → barge-in cancel → append user → `Backend->SendChat`. On complete: `UEmotionMapper::DetectEmotionFromText` → fire `OnSetEmotion` → `OnSpeak` / `OnCompanionSpoke` → `TextToSpeech->Speak`. **Never touches skeletal mesh directly** — drives everything through Blueprint hooks. |
| **Emotion** | `UEmotionMapper` | `DetectEmotionFromText` keyword table → `EEmotionType {Love, Angry, Fear, Alert, Sad, Happy, Neutral}`; `EmotionToAnimCategory` → `EAnimationCategory {Idle, Gesture, Action, Expression}` (enum defined, **not yet consumed by any animation code**). |
| **Voice — STT** | `USpeechToTextComponent` (abstract, `OnTranscribed`), `UNullSpeechToText`, `UWhisperSpeechToText` (`TranscribeWav(bytes)` → multipart POST to whisper.cpp `/inference`, `ParseTranscriptionResponse` reads JSON `text`) | `StartListening`/`StopListening` are **stubs** that only flip `bIsListening` and log. No audio capture (gap, 5.6). |
| **Voice — TTS** | `UTextToSpeechComponent` (abstract), `UNullTextToSpeech`, `UPiperTextToSpeech` (POST text → WAV, decode, play via `USoundWaveProcedural`/`SpawnSound2D`) | Emits `OnVisemeEnvelopeReady` and `OnSpeechFinished`. |
| **Audio DSP** | `FWavAudioReader` (RIFF/WAVE PCM8/16 parse + `ToPcm16`), `FVisemeEnvelope::Compute` | RMS-per-window loudness → normalized `jawOpen ∈ [0,1]`, returns `TArray<FVisemeKey{TimeSeconds, JawOpen}>` — the **whole timeline emitted at once**. |
| **HUD** | `SCompanionHUD` (native Slate `SCompoundWidget`, `FCompanionChatEntry` bubbles, mic toggle, `SimulateSubmitText`/`SimulateMicToggleClick` test hooks), `UCompanionHUDWidget` bridge | Functional but generically styled: **no CI palette/typography applied** (gap; see brand tokens in §Brand). |
| **Config** | `UMetaHumanLLMSettings : UDeveloperSettings` (Project Settings); `FMetaHumanLLMEnvConfig::Apply` / `ApplyFromProcessEnv` | Env overrides applied at startup so **one packaged image serves every environment** with no rebaking. |

**Blueprint hook surface** on `AMetaHumanCompanionCharacter` (this is the entire animation contract):

| Hook | Type | Status today |
| --- | --- | --- |
| `OnSetEmotion(EEmotionType)` | `BlueprintImplementableEvent` | **Called** from `HandleComplete`. No consumer wired yet. |
| `OnSetVisemeWeights(const TMap<FName,float>&)` | `BlueprintImplementableEvent` | Defined but **never invoked by C++** — the envelope is emitted separately and nothing walks it (gap, 5.4). |
| `OnSpeak(const FString&)` | `BlueprintImplementableEvent` | Called on full-reply complete. |
| `OnPartialResponse(const FString&)` | `BlueprintImplementableEvent` | Per streamed token (HUD typing effect). |
| `OnConversationError(const FString&)` | `BlueprintImplementableEvent` | On backend error. |
| `OnCompanionSpoke` | `FOnCompanionSpoke` dynamic multicast | For plain-C++ listeners (the HUD). |

**Env-var contract** (`FMetaHumanLLMEnvConfig`, returns count applied): `MHLLM_BACKEND`, `CI_HUB_URL` / `CI_HUB_TOKEN` / `CI_HUB_MODEL` (a Hub URL forces OpenAI mode), `OLLAMA_BASE_URL` / `OLLAMA_MODEL`, `WHISPER_SERVER_URL`, `PIPER_SERVER_URL`, `MHLLM_SYSTEM_PROMPT`.

### 5.3 Free MetaHuman on 5.5: acquisition, import, and the RigLogic face pipeline

The rigged `DefaultCompanion` is a next-gen 5.8 `MetaHumanCharacter` and is excluded from the 5.5 cook. The shipped face must be a **classic MetaHuman** acquired inside a 5.5 editor, because the classic export produces exactly the 5.5-native asset set the CICompanion Blueprint seam expects (Blueprint + Face/Body skeletal meshes + LODSync + a RigLogic Face AnimBP), with no format conversion.

**Hard deadline:** Quixel Bridge / MetaHuman Importer support for UE 5.5 ends **2026-11-05**, with only a 90-day retrieval window after. Acquire and commit the free MetaHuman now (today is 2026-07-15).

**Acquisition (one human step):**

1. Open the project in a **UE 5.5** editor. `Edit > Plugins`, enable **Quixel Bridge** and **MetaHuman** (on 5.5 the MetaHuman plugin supplies MetaHuman support; on 5.6+ it is built in). Restart.
2. Open the Bridge panel inside the editor. **Sign in to your Epic account** via device/web auth (the same one-time sign-in that unblocked the `DefaultCompanion` auto-rig). This is the only manual step.
3. `MetaHumans > MetaHuman Presets`, pick a **free premade** (Ada, Danielle, Hana, Vanya — any of ~50; presets are free, no Creator needed).
4. Choose High/Medium source quality, `Download`, then `Export/Add` into the open 5.5 project. It lands under `Content/MetaHumans/<Name>/`.
5. Verify the Face AnimBP compiles, the RigLogic node is present, LODSync is wired. Commit via Git LFS as with `DefaultCompanion`.

**Structure that lands** (`Content/MetaHumans/<Name>/`): `BP_<Name>` (root `Body` SkeletalMeshComponent; children `Face` SkeletalMesh + hair/eyes/eyelashes grooms), Body using `SkeletalMeshComponent LODSync` so Face/hair LODs follow Body LOD, Face carrying the generated `Face_AnimBP` on its `PostProcessAnimBlueprint` slot, MetaHuman master materials (`M_Head`, `M_Body`, eyes, teeth) with per-character MIs, LOD0–LOD7. No OpenRigLogic vendoring is needed: every MetaHuman Face AnimBP already contains the RigLogic AnimGraph node, the same node LiveLink Face drives.

**Integration seam (the one unbuilt Blueprint, `BP_CompanionCharacter`).** Rather than reparent `AMetaHumanCompanionCharacter`, keep the C++ actor asset-free and unit-testable and make a thin `BP_CompanionCharacter` subclass that either spawns/attaches the MetaHuman Body+Face components or holds a reference to a placed `BP_<Name>`. All avatar driving lives in that Blueprint.

**Viseme wiring** (`OnSetVisemeWeights`): the DSP already exists; only this seam is unbuilt.

- `UPiperTextToSpeech` fires `OnVisemeEnvelopeReady(TArray<FVisemeKey>)`; each `FVisemeKey` is `{TimeSeconds, JawOpen}`.
- In `BP_CompanionCharacter`: on that event, cache the timeline and the `AudioComponent` play-start time; on `Tick`, binary-search the timeline for the current playback time, build `TMap<FName,float>` with at least `{"jawOpen": interpolated}` (optionally `mouthClose`/`mouthFunnel`), and forward to `OnSetVisemeWeights`.
- **The `FName` keys must be ARKit curve names** (the 52-name set LiveLink Face sends: `jawOpen`, `mouthClose`, `mouthFunnel`, …), because the RigLogic node consumes named control curves, **not** morph targets (`SetMorphTarget` will not work). Push them via a `Modify Curve` / `Set Curve Value` node in `Face_AnimBP`'s post-process graph feeding the RigLogic input pose, backed by a Blueprint-exposed variable that `BP_CompanionCharacter` writes each frame.

**Emotion wiring** (`OnSetEmotion`): map `EEmotionType` to a small set of held ARKit expression curves applied as an additive layer over the jaw viseme through the same `Modify Curve` mechanism:

| `EEmotionType` | Held ARKit curves |
| --- | --- |
| Happy | `mouthSmileL/R` + `cheekSquint` |
| Sad | `mouthFrownL/R` + `browInnerUp` |
| Angry | `browDown` + `noseSneer` |

`UEmotionMapper` already yields the emotion plus an `EAnimationCategory` for body layering. Net result: viseme jaw and emotion expression both ride the embedded RigLogic node; no external RigLogic build is required.

### 5.4 Pixel Streaming AMD container stack

`Deployment/` is mature and real. The delivery path is headless UE on `core-2`, rendered offscreen with Vulkan/RADV, encoded, and streamed over WebRTC to an OIDC-gated browser.

**Images:** `Dockerfile.amd` (`ARG RELEASE=5.8` default; build `FROM ghcr.io/epicgames/unreal-engine:dev-${RELEASE}`; rewrites `.uproject` `EngineAssociation`; drops 5.8-only `MetaHumanCharacter` assets when `RELEASE<5.6`; runtime `FROM ubuntu:26.04` with Mesa RADV Vulkan + VAAPI), plus `Dockerfile` and `Dockerfile.mesagit`.

**Compose overlays (stacked):**

| File | Role |
| --- | --- |
| `docker-compose.yml` | Base: `project` (UE app) + Epic **signalling** + **coturn** TURN + **oauth2-proxy** OIDC, host-networked |
| `docker-compose.amd.yml` | VAAPI H.264 via `radeonsi`, `VK_ICD=radeon_icd.json`, numeric `RENDER_GID`/`VIDEO_GID`, `-RenderOffscreen -Vulkan -noraytracing`, heavy `ExecCmds` disabling Lumen HWRT / Nanite / SkyLight, 1280×720, **VP8** encode, `ws://127.0.0.1:8888` |
| `docker-compose.ollama.yml` | Sidecar Ollama, `gemma4:e2b` default |
| `docker-compose.ue55.yml` | Pins the 5.5 render path |
| `docker-compose.nvidia.yml` | NVENC render node, kept ready as fallback |
| `docker-compose.mesagit.yml` | Bleeding-edge Mesa build variant |

**Ports and edge:** signalling player page `:8090` (patched in `signalling-start.sh`), streamer WebSocket `:8888`, **oauth2-proxy `:8095`** (`provider=oidc`, `skip_provider_button`; `:8095` because `:8080` is taken on `core-2`), coturn `:3478` TURN relay. `oauth2-proxy.cfg` injects `OAUTH2_PROXY_CLIENT_ID`/`CLIENT_SECRET`/`OIDC_ISSUER_URL`/`REDIRECT_URL` (`:8095/oauth2/callback`)/`COOKIE_SECRET` from `.env`, gating the player page and the signalling WebSocket against **CI-Portal** OIDC (issuer `https://hub.companionintelligence.com/api/auth`). The UE app carries **no auth code**; identity is passed upstream via `pass_access_token`/`set_xauthrequest`.

Supporting files: `.env.example`, `run.sh`, `signalling-start.sh`, `oauth2-proxy.cfg`, `author_companion_stage.py`, `core-2-runbook.md`, `README.md`.

**Encode reality:** VP8 software encode is the proven live path; VAAPI H.264 on the Radeon 8060S is configured in `docker-compose.amd.yml` as the hardware path. **Hardening still open:** `cookie_secure=true` over HTTPS, tighten `email_domains=["*"]` to an org/group claim, and confirm 5.5-signalling ↔ 5.8-streamer interop (or build a dedicated 5.8 signalling image).

### 5.5 Level authoring: headless Python commandlet

No `.umap` exists in the repo today (a real gap: no scene, lighting, or camera). The chosen build approach is deterministic, reviewable, and headless rather than hand-placement in the editor: `Deployment/author_companion_stage.py`, run as a UE Python commandlet, authors the 1970s datacenter diorama map and imports the FBX props.

Responsibilities the commandlet owns (per the three-zone layout in the set design):

- **Import the prop FBXs** from `C:\devel\llm-metahuman\scene\*.zip` (IBM 1401 mainframe + Processing Unit, IBM 2401 tape drives, IBM 3350 DASD, RAMAC 305, IBM 3178 green-screen terminals, Xerox Alto, Engelbart mouse, KENBAK-1, Commodore PET, Prológica CP-500, plugboard) as static meshes with correct real-world scale.
- **Place the three zones:** back-wall "big iron" (1401 group ~3 m + RAMAC landmark), mid-room tape/disk "canyon" with a 1.5–2 m central aisle of negative space, and the foreground human zone with the Xerox Alto hero desk angled ~30° to the fixed camera, the Engelbart mouse on a plinth, KENBAK-1 and plugboard as tabletop artifacts, and the operator/micro desks.
- **Author lighting and the fixed camera:** cool fluorescent base wash, warm 1401/KENBAK panel practicals, phosphor-green key from the 3178/Alto CRTs motivating the character's face light, one hard raking light across the tape row.
- **Place `BP_CompanionCharacter`** at the hero desk as the convergence point, and set the world tint toward the CI navy `#041620` shell.

Running it as a commandlet means the map is regenerable from source in CI and inside the AMD container build, rather than being an opaque binary `.umap` that only one machine can produce.

### 5.6 Animation, gesture, and gaze

MetaHuman ships a face rig (RigLogic) and a body skeleton but **no co-speech gesture, idle, or gaze**. The recommended MVP is entirely engine-native, 5.5-safe, and cross-platform (no per-vendor GPU dependency):

| System | Approach | Status |
| --- | --- | --- |
| **Gaze** | Control Rig "Look At" / two-bone head+eye aim (or an Aim Offset) targeting the player camera. Cheap, reads as "alive." | Not started |
| **Idle / locomotion** | Additive breathing idle; optional room-walking via UE5 Motion Matching from the free Game Animation Sample (runs on 5.5), retargeted onto the MetaHuman Body. | Not started |
| **Gesture** | A handful of hand/arm montages keyed off `UEmotionMapper::EmotionToAnimCategory` (`Communication`/`Gesture`/`Expression`), triggered per turn from `OnSetEmotion`. Mirrors the WebXR `EMOTION_TO_CATEGORY` vocabulary. | Not started; the `EAnimationCategory` enum exists but nothing consumes it. |

**On ARDY / posecode:** neither is a locatable, mature, documented product; searches surface only research (GestureLSM, Semantic Gesticulator), not a shippable tool. More decisively, NVIDIA's real embodied-avatar stack (ACE / Audio2Face-3D — the closest "ARDY"-adjacent thing) **requires an NVIDIA GPU at runtime**, which is incompatible with the AMD Strix Halo / RADV-on-5.5 deploy target and with the project's explicit anti-Audio2Face, cross-platform stance. **Verdict:** skip NVIDIA gesture/face AI. If richer gesture is ever wanted, research co-speech models can bake BVH/anim clips **offline** (no runtime NVIDIA dependency), but that is over-scope for the MVP. The `EAnimationCategory` enum is where posecode-style output could later slot as pre-baked montage selection, purely offline.

### 5.7 Component diagram

```mermaid
flowchart TB
    subgraph Browser["Authenticated browser (viewer)"]
        Player["Pixel Streaming player :8090"]
    end

    subgraph Edge["Edge (no auth code in UE)"]
        OAuth["oauth2-proxy :8095<br/>OIDC → CI-Portal"]
        Sig["Epic signalling :8090 / streamer :8888"]
        Turn["coturn :3478 (TURN)"]
    end

    subgraph Container["Headless UE 5.5 on core-2 (Radeon 8060S, RADV)"]
        subgraph Plugin["Plugins/CICompanion"]
            Actor["AMetaHumanCompanionCharacter<br/>(orchestrator, mesh-free)"]
            Conv["UOllamaConversation<br/>(in-memory)"]
            Emo["UEmotionMapper"]
            subgraph Backends["IChatBackend"]
                Ollama["UOllamaClient (NDJSON)"]
                OpenAI["UOpenAIChatClient (SSE/Bearer)"]
            end
            STT["UWhisperSpeechToText<br/>(mic capture = STUB)"]
            TTS["UPiperTextToSpeech"]
            Env["FVisemeEnvelope.Compute<br/>→ OnVisemeEnvelopeReady"]
            Cfg["FMetaHumanLLMEnvConfig"]
        end
        subgraph BP["BP_CompanionCharacter (NOT BUILT)"]
            Walk["Tick: walk viseme timeline<br/>→ OnSetVisemeWeights"]
            Face["Face_AnimBP: Modify Curve<br/>→ RigLogic node (ARKit curves)"]
        end
        Encode["VP8 / VAAPI H.264 encode"]
    end

    subgraph Sidecars["Sidecars"]
        OllamaSvc["Ollama gemma4:e2b"]
        Whisper["whisper.cpp /inference"]
        Piper["Piper TTS"]
    end

    subgraph Phase2["CI ecosystem (Phase 2, NOT built)"]
        Mem["CI-Server /memory/* :8642<br/>context · recall · turn"]
    end

    Player <-->|WebRTC| Turn
    Player -->|page + WS| OAuth --> Sig --> Encode
    Actor --> Conv & Emo & Backends & STT & TTS
    Cfg -.startup overrides.-> Actor
    STT -->|transcript| Actor
    OpenAI -->|CI_HUB_URL SSE| OllamaSvc
    Ollama --> OllamaSvc
    STT --> Whisper
    TTS --> Piper
    TTS --> Env
    Env -.OnVisemeEnvelopeReady.-> Walk
    Actor -.OnSetEmotion.-> Face
    Walk -.OnSetVisemeWeights.-> Face
    Actor -.GET context / POST recall,turn.-> Mem

    classDef gap stroke-dasharray:5 5,stroke:#ee3533;
    class BP,Walk,Face,Phase2,Mem gap;
```

Dashed red nodes are unbuilt: `BP_CompanionCharacter`, the per-frame viseme walk, the `Face_AnimBP` curve seam, and the entire Phase-2 memory client.

### 5.8 Build, cook, and deploy commands

The deploy flow is container-first. Representative commands (from `Deployment/`, adapt paths to `core-2`):

```bash
# 1. Build the AMD render image pinned to the working 5.5 engine
docker build -f Dockerfile.amd --build-arg RELEASE=5.5 -t metahermit:5.5-amd .

# 2. Bring up the full stack: base + AMD render + Ollama sidecar + 5.5 pin
docker compose \
  -f docker-compose.yml \
  -f docker-compose.amd.yml \
  -f docker-compose.ollama.yml \
  -f docker-compose.ue55.yml up -d

# 3. Author the diorama map + import props (headless Python commandlet)
UnrealEditor-Cmd MetaHumanLocalLLM.uproject \
  -run=pythonscript -script="Deployment/author_companion_stage.py" -unattended

# 4. Verify: player page returns 200, streamer joined
curl -sI http://127.0.0.1:8090/ | head -1        # expect HTTP/1.1 200 OK
```

Startup env selects everything with no rebake: set `MHLLM_BACKEND=openai` + `CI_HUB_URL`/`CI_HUB_TOKEN`/`CI_HUB_MODEL` for the Hub (Phase 1, live), or `MHLLM_BACKEND=ollama` + `OLLAMA_BASE_URL` for local baseline (Phase 0). `run.sh` and `core-2-runbook.md` capture the exact `core-2` invocation. The AMD render args (`-RenderOffscreen -Vulkan -noraytracing`, Lumen/Nanite/SkyLight disable via `ExecCmds`, 1280×720, VP8) live in `docker-compose.amd.yml`, not in the command line.

### 5.9 Test strategy

Testing today is **62 `It`-blocks across 8 Automation Spec files** (`Private/Tests`, all `EditorContext | ProductFilter`), run headless via `Scripts/RunTests.Common.ps1` (`RunBackendTests.ps1` / `RunFrontendTests.ps1`), hardened against false passes (crash markers, `Started > Completed` truncation, `Result={Fail}`, zero-results guards).

| Spec | It-blocks | Covers |
| --- | --- | --- |
| `OllamaClientSpec` | 11 | NDJSON request assembly + stream parsing |
| `WavVisemeSpec` | 11 | RIFF/WAVE parse, PCM8/16, RMS→jawOpen envelope |
| `OpenAIChatClientSpec` | 9 | SSE `data:`/`[DONE]`/delta parsing, Bearer |
| `EnvConfigSpec` | 8 | Env-var overrides + applied-count |
| `WhisperSpeechToTextSpec` | 8 | Multipart POST + `text` JSON parse |
| `EmotionMapperSpec` | 6 | Keyword → `EEmotionType`, anim-category map |
| `CompanionHUDSpec` | 5 | Slate submit/mic simulate hooks |
| `ConversationHistorySpec` | 4 | Append/history/reset |
| **Total** | **62** | |

**Honest limitation:** these are pure-logic tests of HTTP request assembly, response parsing, and string logic. There are **no integration, render, live-stream, or animation tests**. Audio-synced visemes, the RigLogic curve push, the mic→STT chain, and the WebRTC stream are unverified by CI and only manually spot-checked on `core-2`. The natural next tests, gated to the pieces being built, are: an `FVisemeEnvelope` timeline-walk unit test (binary search vs. playback time, no editor needed), a `UCIServerMemoryClient` spec mirroring the existing client specs (Phase 2), and an on-`core-2` smoke check asserting player-page 200 + streamer-joined as a post-deploy gate.

### 5.10 Honest gap summary for this section

Built and tested: pluggable backends behind `IChatBackend`, in-memory conversation with barge-in, keyword emotion mapping, Whisper/Piper HTTP clients, WAV+RMS viseme DSP, a Slate HUD, the `UDeveloperSettings`+env config layer, and a proven AMD Pixel Streaming stack that has rendered live on 5.5. Not yet built (the last mile that needs hardware/editor, not unit tests): live mic capture (`AudioCaptureCore`/`AudioMixer` declared, unused), audio-synced viseme playback into `OnSetVisemeWeights`, `BP_CompanionCharacter` and any MetaHuman actually wired to the hooks, a 5.5-native classic MetaHuman in a map, **any** `.umap` at all, gaze/gesture/locomotion, memory persistence, a brand-styled HUD, and XR/passthrough packaging. The nervous system and the delivery truck exist; the body, the room, and the mouth-audio sync are the work ahead.

---

## 6. AI & Data Integration Roadmap (Ollama → CI Ecosystem)

Metahermit's intelligence is not baked into the shipped binary. The single packaged UE build stays endpoint-agnostic because `FMetaHumanLLMEnvConfig::ApplyFromProcessEnv` reads every backend, voice, and (future) memory setting from the process environment at startup and returns a count of overrides applied. Nothing is rebaked between a laptop demo and the core-2 render node; you flip environment variables and restart the container. That property is the spine of this roadmap: each phase adds a new endpoint or a new client class, never a new build.

The through-line is ownership. Every service below runs on the user's own appliance (Companion Core / core-2), reachable over Tailscale or `host.docker.internal`, never a third-party API. The character's brain, voice, and memory all live on hardware the user paid for once. That is the indictment made concrete: the cloud AI trains on your conversations; yours cannot, because yours never leaves the box.

### 6.1 The backbone at a glance

```mermaid
graph LR
  subgraph UE["UE 5.5 — CICompanion plugin (AMD render node)"]
    ORCH["AMetaHumanCompanionCharacter"]
    IBACK["IChatBackend"]
    OLL["UOllamaClient"]
    OAI["UOpenAIChatClient"]
    MEM["UCIServerMemoryClient (Phase 2, new)"]
    ORCH --> IBACK
    IBACK --> OLL
    IBACK --> OAI
    ORCH -.-> MEM
  end
  OLL -->|"/api/chat"| OLLAMA["Ollama gemma4:e2b — Phase 0"]
  OAI -->|"/v1/chat/completions SSE"| HUB["CI-Hub gateway — Phase 1"]
  HUB --> OLLAMA
  PORTAL["CI-Portal OIDC"] -.->|"oauth2-proxy :8095"| ORCH
  MEM -->|"/memory/* x-api-key"| SRV["CI-Server / Companion Memory :8642 — Phase 2"]
  HERMES["CI-Hermes"] --> SRV
  OPENCLAW["CI-OpenClaw"] --> SRV
  IMPORT["CI-Import-Tools :12002"] --> SRV
  COMFY["comfy :8188"] -.->|"offline assets"| UE
```

Phase 0 and Phase 1 already exist in `Deployment/docker-compose.yml`. Phases 2 and 3 are the net-new work, and this section defines them at the level of interfaces, env vars, and call sequencing.

### 6.2 Phase 0 — Local Ollama (baseline, done)

The default backend is `UOllamaClient`, a `GameInstanceSubsystem` implementing `IChatBackend`. It POSTs to `OLLAMA_BASE_URL` `/api/chat` and parses the NDJSON token stream through `FOllamaStreamParser`. `docker-compose.ollama.yml` runs the sidecar with `gemma4:e2b` as the default model. Selection is governed by `UMetaHumanLLMSettings.BackendType`, and `MHLLM_BACKEND=ollama` forces this path.

| Concern | Phase 0 answer |
| --- | --- |
| Model | `gemma4:e2b` via Ollama sidecar |
| Transport | `POST /api/chat`, NDJSON stream, `FOllamaStreamParser` |
| Env vars | `MHLLM_BACKEND=ollama`, `OLLAMA_BASE_URL`, `OLLAMA_MODEL`, `MHLLM_SYSTEM_PROMPT` |
| Voice | `UWhisperSpeechToText` → `WHISPER_SERVER_URL`; `UPiperTextToSpeech` → `PIPER_SERVER_URL` |
| Auth | none |
| Memory | none — `UOllamaConversation` is in-memory, reset on restart |

This is a complete, private, offline companion with one honest limit: it forgets you the moment the container restarts. Everything after this phase is about routing, identity, and memory, without touching the render path.

### 6.3 Phase 1 — CI-Hub gateway + CI-Portal identity (built, live on core-2)

Phase 1 centralizes model choice and puts the whole streamed experience behind the user's own identity provider. Two independent moves:

**Model routing via CI-Hub.** CI-Hub exposes an OpenAI-compatible endpoint (`/v1/chat/completions`, SSE, Bearer) in front of its hosted Ollama. Setting `CI_HUB_URL` in `FMetaHumanLLMEnvConfig` implies `BackendType=OpenAICompatible`, so `UOpenAIChatClient` (SSE via `FOpenAIStreamParser`, parsing `data:` / `[DONE]` / `choices[].delta.content`) takes over from `UOllamaClient` with no code change. The mapping is fixed:

| CI-Hub concept | Env var | UE settings field |
| --- | --- | --- |
| Base URL | `CI_HUB_URL` | `OpenAIBaseUrl` (and forces OpenAI mode) |
| Bearer token | `CI_HUB_TOKEN` | `OpenAIApiKey` |
| Model id | `CI_HUB_MODEL` | `OpenAIModel` |
| Backend switch | `MHLLM_BACKEND=openai` | `BackendType` |

Because the gateway is OpenAI-shaped, model swaps (a bigger model, a router, a fallback chain) happen entirely in Hub config; the avatar never rebuilds.

**Identity via CI-Portal.** The Pixel Streaming player page and its signalling WebSocket are fronted by `oauth2-proxy` on `:8095` (not `:8080`, which is taken on core-2) with `provider=oidc, skip_provider_button`. It authenticates against CI-Portal's OIDC issuer (`https://hub.companionintelligence.com/api/auth`) and injects `OAUTH2_PROXY_CLIENT_ID` / `CLIENT_SECRET` / `OIDC_ISSUER_URL` / `REDIRECT_URL` (`:8095/oauth2/callback`) / `COOKIE_SECRET` from `.env`. Identity is passed upstream via `pass_access_token` / `set_xauthrequest`. **The UE app carries no auth code at all** — the edge does it.

Remaining Phase-1 hardening (tracked, not blocking): set `cookie_secure=true` over HTTPS; tighten `email_domains=["*"]` to a real org/group claim; and confirm 5.5-signalling ↔ 5.8-streamer interop (or build the 5.8 signalling image). The passed `X-Auth-Request-User` header from oauth2-proxy is also the natural key that Phase 2 will use to scope memory per viewer.

### 6.4 Phase 2 — Companion Memory: the character that remembers you (next, not started)

This is the phase that turns Metahermit from a stateless face into the embodied front-end of the user's digital twin. CI-Server (Companion Memory) is a NestJS service on port `8642` with an agent-facing `/memory/*` REST API guarded by `ApiKeyGuard` (`x-api-key`), plus a Streamable-HTTP MCP server at `/mcp`. The avatar becomes a peer of CI-OpenClaw and CI-Hermes on one shared memory brain.

**New code: `UCIServerMemoryClient`** in the CICompanion plugin, plus two new `FMetaHumanLLMEnvConfig` vars:

| New env var | Meaning | Example |
| --- | --- | --- |
| `CI_SERVER_URL` | Companion Memory base URL | `http://ci-server:8642` or `http://host.docker.internal:8642` |
| `CI_SERVER_TOKEN` | `x-api-key` value | (from `.env`) |

The client is a thin HTTP wrapper mirroring the pattern already proven by `UWhisperSpeechToText` / `UPiperTextToSpeech`: build request, POST, parse JSON, broadcast. Interface sketch (three verbs, matching the three real routes):

```cpp
// UCIServerMemoryClient : UGameInstanceSubsystem
//   GET  /memory/context           -> FString GetContextBlock();                 // L0 profile markdown, {block}
//   POST /memory/recall {query}    -> TArray<FMemoryHit> Recall(Query, MaxResults);  // L1 hybrid search
//   POST /memory/turn {sessionId,platform:"metahuman",messages[]} -> WriteTurn(...);  // dedup SHA-256 UUIDv8
```

**Retrieval-before-prompt, write-after-turn.** `AMetaHumanCompanionCharacter` gains three call sites around its existing `SubmitUserText()` flow:

1. **Session seed (once).** On `BeginPlay` / first connect, `GET /memory/context` returns the L0 profile as markdown, which is prepended to `MHLLM_SYSTEM_PROMPT` before the first `Backend->SendChat`. The character walks in already knowing who you are.
2. **Recall (before each inference).** After Whisper returns a transcript in `HandleTranscribed`, and before `SendChat`, `POST /memory/recall {query: transcript, maxResults}` returns hybrid keyword+semantic hits with entity hydration, folded into the prompt as retrieved context. This is the only added latency on the hot path; keep `maxResults` small (3–6) and run it concurrently with nothing else since the model call depends on it.
3. **Write-back (after each exchange).** On backend `complete` (the same `HandleComplete` that already fires `OnSetEmotion` and `OnSpeak`), fire-and-forget `POST /memory/turn {sessionId, platform:"metahuman", messages:[user, assistant]}`. CI-Server dedups by SHA-256 UUIDv8, and a background `/memory/compact` consolidates over time. This is the identical `afterTurn` / `sync_turn` contract CI-OpenClaw and CI-Hermes use, so an OpenClaw task run yesterday becomes something the avatar can recall today.

```mermaid
sequenceDiagram
    autonumber
    participant B as Browser (authenticated)
    participant P as oauth2-proxy :8095 (CI-Portal OIDC)
    participant A as AMetaHumanCompanionCharacter
    participant W as UWhisperSpeechToText
    participant M as UCIServerMemoryClient
    participant S as CI-Server :8642 (Companion Memory)
    participant L as UOpenAIChatClient
    participant H as CI-Hub gateway (Ollama)
    participant T as UPiperTextToSpeech
    Note over B,P: Session start (once per viewer)
    B->>P: sign in (OIDC, PKCE)
    P->>A: proxy player page + signalling (X-Auth-Request-User)
    A->>M: GetContextBlock()
    M->>S: GET /memory/context (x-api-key)
    S-->>M: { block } L0 profile markdown
    M-->>A: system prompt += block
    Note over B,T: One spoken turn
    B->>A: mic audio (WebRTC)
    A->>W: TranscribeWav(bytes)
    W-->>A: transcript
    A->>M: Recall(transcript, maxResults)
    M->>S: POST /memory/recall { query }
    S-->>M: hybrid hits + entities
    M-->>A: retrieved context
    A->>L: SendChat(system + memory + history + user)
    L->>H: POST /v1/chat/completions (SSE, Bearer)
    H-->>L: token stream
    L-->>A: OnPartialResponse / complete
    A->>T: Speak(reply)
    T-->>A: WAV + OnVisemeEnvelopeReady (jawOpen timeline)
    A-->>B: audio + lip-synced MetaHuman (VP8/WebRTC)
    A->>M: WriteTurn(sessionId, [user, assistant])
    M->>S: POST /memory/turn (dedup SHA-256 UUIDv8)
    S-->>M: 201
```

Steps that already exist today are the STT → LLM → TTS → stream spine (compose services on core-2) plus the OIDC edge. The three memory calls (context, recall, turn) are the entire net-new surface.

**Auth when fronted by the Hub.** CI-Server also supports `HubForwardAuthGuard` (HMAC `X-CI-Hub-User`) as an alternative to a static `x-api-key`. When Metahermit runs behind CI-Hub, prefer forward-auth so the memory write is attributed to the same Portal identity that authenticated the browser — one user, one memory, end to end — instead of a shared service token.

**Privacy and local-first.** CI-Server is a container on the user's own Core; `CI_SERVER_URL` resolves over the private network (`ci-server` service name or `host.docker.internal`), never the public internet. The digital twin is a local SQLite/GraphQL store the user owns and can delete. No memory payload is ever placed in a URL query string; recall queries and turn writes travel as POST bodies over the loopback/Tailscale plane. Serialize `/memory/turn` payloads against the CI-Common `@companionintelligence/contracts` schema (the same Zod → JSON Schema source that defines `CaptureEvent` / memory-turn shapes) so the avatar's writes are structurally identical to every other producer on the memory bus.

**Testing.** Add an `EnvConfigSpec` case for the two new vars and a `UCIServerMemoryClient` unit spec that asserts request assembly (headers, `x-api-key`, JSON body shape) and response parsing, matching the existing pure-logic test discipline (`OllamaClientSpec`, `WhisperSpeechToTextSpec`). No live-network test; the client is verified the same way the chat backends are.

### 6.5 Phase 3 — Ecosystem breadth (later)

Phase 3 widens the character from a memory consumer into a participant in the whole CI substrate. None of these are on the hot path of a spoken turn; they are shared-brain siblings and out-of-band tools.

- **Tool calling via MCP.** Consume CI-Hub's injected `HUB_URL` / `HUB_MCP_API_KEY` (Streamable-HTTP MCP) and/or CI-Server's `/mcp` surface so the avatar can call tools and delegate tasks — potentially handing a job to a **CI-OpenClaw** run and surfacing the result back through shared memory. The Hub already injects these vars into installed apps; the UE app simply does not consume them yet.
- **CI-OpenClaw** as a peer agent: both the avatar and OpenClaw read/write CI-Server `/memory/*` identically and reach tools through the same Hub MCP endpoint, so their work is mutually visible without any direct UE↔OpenClaw coupling.
- **CI-Hermes** as the ops surface: a dashboard/web-terminal tile (connected to CI-Server at `:8642` with a Companion Memory key) to observe avatar sessions, turns, and memory writes. It is also the reference implementation of the `sync_turn` write pattern the avatar copies.
- **CI-Import-Tools** (`:12002`) is what makes `/memory/recall` actually rich: it bulk-ingests Google Takeout, messages, photos, location, and contacts into the local timeline and syncs to CI-Server, then enriches (people extraction, geocoding, embeddings). No UE coupling; it fills the twin the avatar recalls from.
- **comfy (ComfyUI, `:8188`)** as an out-of-band content pipeline — generating textures, backdrops, and diorama art (the 1970s datacenter set, avatar-adjacent imagery) offline. Never a runtime path during a turn.
- **CI-Common** for consistency: any companion web UI or launcher card adopts the CI design tokens (phthalo `#0a6358`, Manrope), and memory payloads serialize against its contracts.

### 6.6 Service → interface → integration point → phase

| CI service | Interface | This app's integration point | Phase |
| --- | --- | --- | --- |
| **Ollama (sidecar)** | `POST /api/chat`, NDJSON | `UOllamaClient` / `FOllamaStreamParser`; `MHLLM_BACKEND=ollama`, `OLLAMA_BASE_URL` | 0 (done) |
| **CI-Hub** (model gateway) | OpenAI-compatible `/v1/chat/completions`, SSE, Bearer | `UOpenAIChatClient` / `FOpenAIStreamParser`; `CI_HUB_URL` / `CI_HUB_TOKEN` / `CI_HUB_MODEL`, `MHLLM_BACKEND=openai` | 1 (built) |
| **CI-Portal** (OIDC IdP) | OIDC / OAuth2 PKCE, discovery + auth-code | `oauth2-proxy :8095` fronting the player page + signalling; no UE code | 1 (built) |
| **CI-Server / Companion Memory** | REST `/memory/context\|recall\|turn` (`x-api-key`), MCP `/mcp` | **new `UCIServerMemoryClient`**; `CI_SERVER_URL` / `CI_SERVER_TOKEN`; three call sites in `AMetaHumanCompanionCharacter` | 2 (next) |
| **CI-Hub MCP** (tools) | Streamable-HTTP MCP, `HUB_URL` / `HUB_MCP_API_KEY` | future MCP tool-call client; delegate to OpenClaw, reflect via memory | 3 |
| **CI-OpenClaw** | shares CI-Server `/memory/*` + Hub MCP | shared substrate; delegated actions become recallable context | 3 |
| **CI-Hermes** | web terminal + dashboard over CI-Server | observability tile for avatar sessions/turns; reference `sync_turn` impl | 3 |
| **CI-Import-Tools** | REST/MCP `:12002` → CI-Server bulk-ingest | populates the twin `/memory/recall` reads from; no UE coupling | 3 |
| **comfy (ComfyUI)** | HTTP API `:8188` | out-of-band texture/scene/asset generation for the diorama; no runtime path | 3 |
| **CI-Common** | npm: `@companionintelligence/auth`, `/contracts`, `/tokens` | serialize memory payloads to contract JSON Schema; brand tokens for any web UI | 2–3 (reference) |

**What is real vs. gap, stated plainly.** Phase 0 and Phase 1 are wired in `Deployment/docker-compose.yml` and have rendered live on core-2's Radeon 8060S with `gemma4:e2b` streaming VP8 to an authenticated browser. Phase 2 is entirely unbuilt: there is no `UCIServerMemoryClient`, no `CI_SERVER_URL` / `CI_SERVER_TOKEN`, and `UOllamaConversation` remains in-memory only — the character forgets you on restart today. The honest headline for this roadmap: the model routing and the identity edge exist; the memory that makes the character *yours across sessions* is the next thing to build, and it is three HTTP calls and one client class away.

---

## 7. Art Direction & CI Brand System

This section is the art-direction bible that reconciles two things that should not, at first glance, sit in the same frame: a warm, yellowed, humming **1970s institutional data center** (Section on Scene/Set) and Companion Intelligence's cool, disciplined, **teal-and-navy technical-authority** brand system. The resolution is stated up front and defended throughout: the *world* is period-authentic and warm; the *interface layer that CI owns* — boot screen, web player chrome, HUD, subtitles, focus glows, and the character's own emissive "tell" — is unmistakably CI teal. The brand does not repaint the room. It sits **on top of** the room as the signal that a modern Companion is inhabiting this history.

### 7.1 The core tension and its resolution

| | 1970s datacenter (the world) | CI brand (the interface) |
|---|---|---|
| Palette | Computer beige, oatmeal, IBM off-white/pale-grey, deep IBM blue, walnut laminate; amber/red panel lamps; phosphor green CRTs | Navy `#041620`, CI phthalo/sage `#0a6358`/`#c5e8dc`, aqua `#00a9a7`, mint `#58ebbf` |
| Temperature | Warm tungsten + green-cyan fluorescent | Cool teal-navy |
| Texture | Dust, patina, yellowed plastic, worn keycaps | Flat vector, liquid glass, hairline grids |
| Motion | Spinning 2401 tape reels, flickering fluorescent | Short eased transitions, mint focus pulse |

**Resolution principle — "diegetic warm, chrome cool."** Everything physically in the diorama obeys period color science. Everything CI-owned and *non-diegetic* (composited over the stream, not lit in the world) obeys the token set. The single deliberate bridge between them is the phosphor-green CRT glow of the **IBM 3178** (`ibm-3178.zip`) and **Xerox Alto** (`xerox-alto-son-of-douglas-engelbart-1973.zip`): period-correct P1/P3 green already sits adjacent to CI's aqua/mint family on the hue wheel, so the character's key light (motivated from those screens, per the Set mood spec) naturally reads as "teal-adjacent" without ever violating either system. That green practical is the handshake. We push it a few degrees toward `#72ddd4` (aqua-light / the logo wordmark color) in the color grade so the warm room and the cool brand meet on the hero's face.

### 7.2 Boot / loading sequence — the CI logo reveal

The stream connects before the UE render node has a frame ready (WebRTC handshake through coturn `:3478`, after the `oauth2-proxy :8095` OIDC gate). That dead air is the brand's first impression and is owned entirely by the **web player page** (non-diegetic HTML/CSS/canvas), not the engine.

**Sequence (target ~2.5 s, `prefers-reduced-motion`-safe):**

1. **Cold field.** Full-bleed CI shell navy `#041620`. An aurora radial wash breathes in behind center: `radial-gradient(120% 90% at 50% -10%, rgba(64,155,155,.28), transparent 60%)` (the canonical CI hero aurora, `--teal-mid #409b9b` at 28% alpha).
2. **Mark reveal.** `packages/icons/assets/logo/mark.svg` fades/scales from 0.94→1.0 over 420 ms on `cubic-bezier(0.22, 1, 0.36, 1)` (CI "ease-out-expo" feel). The mark is reproduced with **its own three artwork hexes** — outer petals `#066c80`, mid `#00a9a7`, inner highlight `#72ddd4` — never flattened to the UI token `#0a6358`. Flat fills, no drop shadow (elevation guidance forbids it on the mark).
3. **Mint scan.** A single mint `#58ebbf` "focus" hairline sweeps top-to-bottom once behind the mark (the CI focus/selection glow used as a boot heartbeat), at ~8% alpha, 600 ms.
4. **Wordmark + status.** `wordmark.svg` (light-aqua `#72ddd4`) slides in to the right of the mark to form the `full.svg` lockup. Below it, one Manrope 500 status line in dark foreground `#e8f2f4`, sentence case, dash-free, cycling grounded fact-plus-state strings that mirror the actual pipeline state:
   - `Signing you in.` (OIDC handshake)
   - `Waking your Companion.` (streamer join)
   - `Your Companion is ready.` → hard cut to first streamed frame.
5. **Handoff.** When `Player(DefaultStreamer) joined` fires and the first VP8 frame arrives, the boot layer cross-dissolves (200 ms) to the live video. The mark shrinks to its persistent HUD home (top-left, size ~24 px) so the reveal and the running-state badge are the *same object* moving, not two logos.

Error state on this screen follows the voice rule — a fact plus a fix, never an alarm: `Your Companion is offline. Check the render node, then retry.` in destructive-dark `#ef7070`, not the light `#ee3533`.

### 7.3 Streamed web player page

This is the browser shell around the `<video>` element (Epic signalling player at `:8090`, fronted by oauth2-proxy). It is dark-only — the correct first-class palette for an immersive surface, and consistent with CI's other dark-only HUD precedents (CI-Spatial-Companion-WebXR, CI-Even-Realities).

**Tokens (consume `tokens.css` vanilla vars — the right entrypoint for a static/streamed page, not the Tailwind `globals.css`):**

- Page background: `--background #041620`. Letterbox bars around the 1280×720 stream: same navy, so the video floats in an unbroken CI shell.
- Control bar (mic toggle, connection status, fullscreen): a raised panel on `--card #0c323c` with `--accent/--border` hairlines (`#0a2e38`/`#073038`), radius `--radius-lg 10px`.
- Primary action (the push-to-talk / "Hold to speak" control): `--gradient-brand linear-gradient(135deg, #0a6358, #00a9a7)` fill, `--primary-foreground` label. On focus, mint ring `#58ebbf` — this project's own deliberate accent (see Appendix B); canon `--ring` dark is sage `#c5e8dc` and no longer disagrees with itself, so there is nothing left to "resolve," this is simply choosing mint over canon on purpose because the player deserves the high-energy focus accent.
- Connection health chip: status dots reusing the terminal motif — green `#0f5132`/`#34d399` connected, amber `#fbbf24` reconnecting, `#ef7070` dropped.
- Typography: self-hosted `@fontsource/manrope` (CSP-safe, no CDN — matters for a locked-down streamed page behind oauth2-proxy), weights `400;500;600;700` exactly. Title strings Manrope 600; body/labels 400/500. Sentence case everywhere; product nouns (Companion Core, Companion Memory) Title Case; **no em/en dashes in any string**.
- Optional immersive flourish (marketing layer only): the outer frame around the video may use a single liquid-glass panel — 8px radius, `backdrop-blur 18–32px`, sage border `rgba(117,149,130,.22)`, faint 135° gold `#d3b973` sheen — to make the 1970s window feel like it is behind modern glass. This is the one sanctioned off-canon texture; keep it out of the core HUD tokens.

### 7.4 In-engine UMG HUD chrome + subtitle treatment

The existing HUD is native Slate `SCompanionHUD` / `UCompanionHUDWidget`, explicitly flagged in the codebase as *functional but generically styled, no CI palette applied* (a named gap). This is where we specify the re-skin. Because the HUD is rendered **in-engine** and streamed as pixels (not an HTML overlay), fonts and colors must be imported as UE assets: import the Manrope TTFs at the four canon weights (plus Archivo Black for display) as a UE **Font** asset (do **not** substitute Roboto/Arial/Montserrat), and drive colors from a `tokens.json`-backed UE **data table / UMG theme** so the HUD recolors from the single source of truth. Treat the sRGB hexes as authoritative and **convert to linear in-engine**.

**HUD component styling:**

| HUD element | Token / value (sRGB) | Notes |
|---|---|---|
| Chat log panel | `#0c323c` @ ~85% over the stream | Raised card; `#073038` 1px hairline; radius 10px |
| "You" bubble | fill `#073038`, text `#e8f2f4` | Right-aligned |
| "Assistant" bubble | fill `--gradient-brand` `#0a6358→#00a9a7`, text `#f0fdf4` | Left-aligned; ties the character's words to brand phthalo |
| Text input box | bg `#041620`, border `#648893` (see §4.8 contrast note — raw canon `--input` dark fails 3:1 here), focus ring mint `#58ebbf` | |
| Mic toggle (idle) | outline sage `#c5e8dc` | Verb-first tooltip: `Hold to speak` |
| Mic toggle (listening) | mint `#58ebbf` pulsing glow | Reduced-motion: static fill, no pulse |
| Status text | `--muted-foreground dark #a3babf`, Manrope 500 | `Your Companion is listening.` |
| Persistent mark badge | `mark.svg` ~24px, top-left | Same object handed off from boot |
| Streaming token typing | fed by `OnPartialResponse` | Cursor blink reuses terminal aqua-mint `#c5f7f2` |

**Subtitle treatment** (critical, because lip-sync is still an open gap — subtitles carry comprehension while `OnSetVisemeWeights` wiring matures). Subtitles are driven by `OnSpeak(FString)` on reply-complete and `OnPartialResponse` for live typing:

- Bottom third, centered, max-width ~66% to avoid the hero desk clutter.
- Manrope 500, size scaled to 720p delivery; body color pure-legible on any backdrop via a `#041620` @ 70% pill (radius `--radius-full`) rather than a hard box, so it reads as a soft CI panel floating over the warm room.
- Speaker color coding uses the same logic as bubbles: the Companion's spoken lines tinted with a subtle sage top-edge (`#c5e8dc` 1px), user echo (from Whisper transcript) in neutral `#e8f2f4`.
- Emotion, surfaced from `OnSetEmotion(EEmotionType)`, may tint the subtitle pill edge — a **restrained** 1px accent only: Happy → aqua `#00a9a7`, Sad → muted `#409b9b`, Alert/Fear → warning `#fbbf24`, Angry → destructive `#ef7070`, Love → primary `#c5e8dc`, Neutral → no tint. Never flood the pill; the edge is the signal.
- All subtitle strings inherit the voice rules verbatim: sentence case, no dashes (the LLM system prompt via `MHLLM_SYSTEM_PROMPT` should instruct the model to avoid em/en dashes in spoken output so the ban survives all the way to the screen).

### 7.5 Environment color grade & practical lights

The world stays warm; the **post-process color grade and a few CI-tinted practicals** pull it a controlled few degrees toward brand without falsifying the period. Applied in the UE post-process volume (mind the `docker-compose.amd.yml` exec-cmds already disable Lumen HWRT/Nanite/SkyLight on the 5.5 AMD render path, so grade with cheap post-process, not ray-traced GI):

- **Base grade.** Preserve the cool green-cyan fluorescent wash and warm tungsten split the Set mood calls for. In the shadows/lift, bias very slightly toward teal-navy `#041620` so the darkest cabinet recesses and the back-wall "Big Iron" zone (IBM 1401 group, RAMAC 305) fall into a color that rhymes with the CI shell. Midtones and warm practicals untouched — beige stays beige.
- **CI-tinted practicals (diegetic-but-brand).** Two motivated sources are nudged into the brand family:
  1. The **3178 / Alto phosphor green** key light on the hero's face pushed toward `#72ddd4` (aqua-light). This is the primary bridge light and the warmest-brightest zone per the Set spec's "motivate key from the terminal screens."
  2. A subtle **mint `#58ebbf` rim/kicker** on the character's shoulder/hairline from an off-screen source, reading in-world as a stray monitor or status LED but functioning as the CI selection glow made physical. Low intensity; it is the character's halo, not a stage light.
- **Untouched warmth.** The amber/red `#c` panel lamps of the 1401 console and the **KENBAK-1** (`kenbak-1-first-personal-computer.zip`) toggle-lamp row stay period-warm as the deliberate counter-temperature. The frame needs that warm/cool split for depth; killing it would make the room read as a sterile modern render and break the diorama fiction.
- **Aurora, sparingly.** The CI aurora wash exists only on the non-diegetic layers (boot, web page). It does **not** get sprayed into the world skybox; the world's "sky" is a dim raised-anti-static-floor-tile ceiling, not a teal gradient.

### 7.6 The character's brand-consistent presence

The MetaHuman (a free 5.5-classic preset — Ada/Danielle/Hana/Vanya-class — acquired via Quixel Bridge before the 2026-11-05 cutoff) is a warm human being in a cool-graded room. Brand consistency is expressed through **light, one wardrobe accent, and an emissive tell**, never by making the character look teal-skinned:

- **Skin** uses the stock MetaHuman `M_Head`/`M_Body` master materials untouched — human warmth is the whole point; do not push skin toward teal.
- **Wardrobe accent.** A single CI-phthalo element — a `#0a6358`/`#c5e8dc` scarf, collar, or lanyard — is the character's one intentional brand thread, so a still frame reads as "CI" even before the HUD loads.
- **Emissive tell (the brand-as-life signal).** When the Companion is *thinking/speaking* — gated on the same events that already fire (`OnSpeak`, `OnPartialResponse`, `OnSetEmotion`) — a faint mint `#58ebbf` → aqua `#00a9a7` emissive accent animates on a small worn object (a badge, the Engelbart-mouse plinth vitrine edge, or a subtle collar pin). This is the "ghost in the shell / liquid-glass" motif made diegetic: the modern intelligence glowing inside the vintage room. It pulses on speech, holds steady on idle-listening, and dims to nothing when the backend is disconnected — a wordless status indicator in pure brand color.
- **Gaze as presence.** The engine-native Control Rig look-at (the recommended MVP gaze) aiming at the streamed camera makes eye contact the primary "alive" cue; brand color never substitutes for it.

### 7.7 Iconography

- **lucide-react family only**, thin-line, `currentColor`, 16px default / 20px prominent — the single sanctioned icon language. Do not mix icon sets. In-engine, these are imported as SVG→UMG or as a small icon font/atlas at matching stroke weight so the HUD icons and any web-page icons look identical across the diegetic/non-diegetic seam.
- The **CI mark** (`mark.svg`) is the app/tab favicon, the persistent HUD badge, the boot spinner, and the character's avatar chip — always its own three hexes, always flat, always with clear space, never re-hued or stretched.
- Status semantics reuse tokenized status colors (success/warning/info/destructive) so a red dot means the same thing on the web page, in the HUD, and in a future CI-Hermes monitoring tile.

### 7.8 Motion & easing language

- **Duration budget.** UI transitions ≤ 200 ms (`transition-colors/all`); the boot mark reveal 420 ms; the mint scan 600 ms. Sound-cue durations, if/when produced, follow `brand.config.json`: success 420 ms, error 500 ms, notify 350 ms, capture start/stop 300 ms, mono, −16 LUFS, UI cues <150 ms.
- **Easing.** Ease-out-expo-flavored `cubic-bezier(0.22, 1, 0.36, 1)` for entrances (mark, panels, subtitle pills); linear only for the tape-reel-adjacent ambient loops. No bounce, no overshoot beyond the 0.94→1.0 mark scale — CI motion is calm and technical, not playful.
- **The mint pulse** (listening state, speech emissive) is the one repeating animation; it must be `prefers-reduced-motion`-safe on the web layer and skippable in-engine (fall back to a static fill). Any ambient/background animation is reduced-motion-gated by rule.
- **Shadows/elevation.** Soft, low, teal-tinted in light contexts (`rgba(15,113,122,.08–.14)`), neutral in dark; never heavy drop shadows, and never on the logo mark.

### 7.9 Marketing key-art concept

**"The night shift never ended."** A single hero key frame, 3:2 and 16:9 crops:

- **Composition.** The fixed camera looks down the central negative-space aisle of the datacenter (per the Set layout), past the spinning **IBM 2401** tape reels catching a hard raking light, toward the hero desk. The MetaHuman Companion sits three-quarter-lit at the **Xerox Alto**, phosphor-green screen glow (graded toward `#72ddd4`) on her face, the Engelbart 1963 mouse on its lit plinth in the foreground. The whole warm room recedes into teal-navy shadow.
- **The brand move.** Over the deep-shadow back wall (the 1401 "Big Iron"), the CI `full.svg` lockup sits in `#72ddd4`, and a faint mint `#58ebbf` aurora emanates from *behind the character* — as if she is the light source the modern brand color is coming from. The room is 1975; the glow around her is now.
- **Headline** (Archivo Black oversized numerals / Manrope 600 heading, sentence case, dash-free, one grounded claim per the voice rules): **`Fifty years of computers. One that finally talks back.`** Sub: `Runs on your own box. No cloud. No subscription.` CTA pill in `--gradient-brand`: `Meet your Companion`.
- **Palette lock.** Warm world + `#041620` shadow field + `#72ddd4` wordmark + `#58ebbf` character halo. Exactly the "diegetic warm, chrome cool" thesis in one image.

### 7.10 Brand-token → surface mapping (canonical)

| Brand token (hex) | Web player page | HUD (in-engine UMG) | Environment / world | Character |
|---|---|---|---|---|
| `--primary` phthalo `#0a6358` (light) / sage `#c5e8dc` (dark) | Primary control base, focus states | Assistant bubble base, mic-idle outline | (not in world — chrome only) | Wardrobe accent (scarf/lanyard) |
| Navy shell `#041620` | Page bg, letterbox, subtitle pill | Input bg, panel base | Shadow-lift bias in graded darks; back-wall recesses | — |
| Card `#0c323c` | Control bar | Chat log / status panel | — | — |
| Aqua `#00a9a7` | Primary CTA gradient end | Assistant bubble gradient end | — | Speech emissive tell (peak) |
| Aqua-light `#72ddd4` | Wordmark on boot | — | Bridge key-light target (3178/Alto green) | Face key-light temperature |
| Mint `#58ebbf` (project accent, not canon) | Focus ring, boot scan | Focus ring, mic-listening pulse | Mint rim/kicker practical | Idle-listening halo / status glow |
| Logo trio `#066c80`/`#00a9a7`/`#72ddd4` | Boot mark, favicon | Persistent mark badge | — | Avatar chip |
| Foreground / muted fg `#e8f2f4`/`#a3babf` | Body/label copy | Status + subtitle body | — | — |
| Destructive `#ef7070` | Connection-dropped chip | Error toast (`OnConversationError`) | — | Emissive tell dims to off on disconnect |
| Status green/amber `#34d399`/`#fbbf24` | Connection health chip | Mic/backend state | 1401 lamps stay period-amber (counter-temp) | Emotion-tinted subtitle edge (Alert/Fear) |
| `--gradient-brand` `#0a6358→#00a9a7` | Push-to-talk fill | Assistant bubble | — | — |
| Liquid glass (sage `#759582` + gold `#d3b973`) | Optional outer video frame | (avoid in core HUD) | — | — |
| Terminal aqua-mint `#c5f7f2` on `#031017` | — | Streaming cursor blink | Echoes diegetic 3178 green phosphor | — |

**Non-negotiable, restated for this app:** `--primary` must resolve to CI phthalo (`#0a6358` light / `#c5e8dc` dark) on every CI-owned surface; a lint gate defends the exact value, and only that value — the retired `#0f717a`/`#abd4d8` teal is not a fallback. Everything else is neutral. The logo keeps its own three richer hexes. Mint `#58ebbf` is this experience's own deliberate departure from canon `--ring` (sage `#c5e8dc` in both current tokens.json and globals.css — the "inconsistency" this doc used to cite between the two is gone, they agree now), chosen on purpose as the focus/life accent across boot, web page, HUD, and the character's emissive tell — one high-energy color that says "the Companion is here." Record it as a named override if it ships on a literal CI web surface, the way CONSUMING.md asks.

---

---

## 8. Production Roadmap, Milestones, Risks & Open Questions

This section converts the honest state of the codebase — a fully-tested backend-and-plumbing layer plus a proven Pixel Streaming deployment, with the "body, room, mouth-sync, ears, and memory" still unbuilt — into a sequenced delivery plan. Milestones are ordered so that each one produces something visible in the browser stream on `core-2`, and so the hard external deadline (Epic's 2026-11-05 Bridge cutoff for UE 5.5) is retired as early as possible.

### 8.1 Milestone ladder

| ID | Milestone | Ships when the stream shows… | Phase alignment |
|----|-----------|------------------------------|-----------------|
| **M0** | Streaming shell + placeholder actor | A placeholder body (UE `SKM_Quinn` mannequin) in a minimal map, streamed to an authenticated browser | Phase 0/1 infra (done / in-flight) |
| **M1** | Free MetaHuman on 5.5, rigged with visemes | A real MetaHuman face whose jaw moves in sync with Piper audio | Phase 0 (avatar) |
| **M2** | Datacenter diorama set | The MetaHuman standing at the Xerox Alto hero desk inside the 1970s data center | Phase 0 (scene) |
| **M3** | Full voice loop live | Speak into the mic → the avatar hears, thinks, replies, and lip-syncs | Phase 0 (last mile) |
| **M4** | CI-Hub + Portal hardening | Same experience, gated by CI-Portal sign-in and served by Hub-routed Ollama | Phase 1 (harden) |
| **M5** | Companion Memory persistence | An avatar that recalls you across sessions | Phase 2 |
| **M6** | Gaze / gesture polish + XR | Eye contact, co-speech gesture, and a Quest 3 build | Phase 3 |

---

### 8.2 M0 — Streaming shell + placeholder actor *(done / in-flight)*

**Goal.** Prove the delivery truck end-to-end before the avatar exists: headless UE renders on the Radeon 8060S, encodes, and streams to a browser behind auth.

**Key tasks**

- Package the AMD render node from `Dockerfile.amd` (`ARG RELEASE=5.5` for the working path) with `docker-compose.amd.yml` (VAAPI/`radeonsi`, `VK_ICD=radeon_icd.json`, `-RenderOffscreen -Vulkan -noraytracing`, 1280×720, VP8).
- Bring up Epic signalling (`signalling-start.sh`, player port patched to 8090), `coturn` (:3478), and `oauth2-proxy` (:8095) from `docker-compose.yml`.
- Create the project's **first `.umap`** (none exist today) — a bare lit box with a fixed camera and a `SKM_Quinn` mannequin as the stand-in subject.

**Dependencies.** `core-2` GPU host (Ryzen AI Max+ 395 / Radeon 8060S, Ubuntu 26.04); working UE 5.5 render path.

**Definition of done.** Player page returns HTTP 200 on :8090; `Player(DefaultStreamer)` joins; VP8 frames of the placeholder actor arrive in an authenticated browser tab. *(Per `Docs/FOLLOW-UP.md` 2026-07-16 the stack already renders live on 5.5; the outstanding piece is committing a real placeholder map rather than a default-empty level.)*

---

### 8.3 M1 — Free MetaHuman on 5.5, rigged with visemes

**Goal.** Replace the mannequin with a talking-capable MetaHuman whose jaw is driven by `FVisemeEnvelope`.

**Key tasks**

| Task | Concrete detail |
|------|-----------------|
| Acquire a free premade MetaHuman | In a **UE 5.5** editor, enable `Quixel Bridge` + `MetaHuman` plugins, one-time Epic device-auth sign-in, download a premade (Ada / Danielle / Hana / Vanya) as a **classic** MetaHuman into `Content/MetaHumans/<Name>` (`BP_<Name>` + Face/Body SkeletalMeshes + Face AnimBP with RigLogic). Commit via Git LFS. |
| Create `BP_CompanionCharacter` | The Blueprint referenced throughout the code but **not yet existing**. Subclass the C++ `AMetaHumanCompanionCharacter`; place/attach `BP_<Name>` Body+Face. |
| Walk the viseme timeline | On `UPiperTextToSpeech::OnVisemeEnvelopeReady`, cache the `TArray<FVisemeKey>` and the `AudioComponent` play-start time; on Tick, binary-search for current playback time and build a `TMap<FName,float>` → call `OnSetVisemeWeights`. Closes the gap where the envelope is emitted all-at-once and never synced. |
| Push curves into RigLogic | Keys **must** be ARKit names (`jawOpen`, `mouthClose`, `mouthFunnel`…). Add a **Modify Curve** node in `Face_AnimBP`'s post-process graph feeding the RigLogic input pose, written each frame from the Weights map. `SetMorphTarget` will **not** work — RigLogic consumes named control curves. |
| Map emotion to expression | Wire `OnSetEmotion(EEmotionType)` (already fired from `HandleComplete`) to held additive ARKit curves (Happy → `mouthSmileL/R`+`cheekSquint`; Sad → `mouthFrownL/R`+`browInnerUp`; Angry → `browDown`+`noseSneer`). |

**Dependencies.** M0 stream; **Epic Bridge on 5.5 (hard deadline 2026-11-05)**; `UPiperTextToSpeech` (exists, unit-tested).

**Definition of done.** In-editor: `Face_AnimBP` compiles under 5.5, RigLogic node evaluates, LODSync wired (the one hands-on verification `FOLLOW-UP.md` still flags open). At runtime: a canned Piper WAV drives visible, time-synced jaw motion on the streamed face.

---

### 8.4 M2 — Datacenter diorama set

**Goal.** Build the `.umap` the whole experience is missing, from the twelve FBX props, per the three-zone layout.

**Key tasks**

- Import the prop archives from `C:\devel\llm-metahuman\scene\*.zip` and lay out the three zones: **back wall** big-iron (IBM 1401 group ~3 m + RAMAC 305 landmark), **mid-room** motion/storage canyon (2–4 IBM 2401 tape drives + 2–3 IBM 3350 disk string, central 1.5–2 m aisle), **foreground** human zone (Xerox Alto hero desk angled ~30° to camera, Engelbart 1963 mouse on a ~0.9 m plinth, KENBAK-1 + plugboard as tabletop artifacts, IBM 3178 green-screen operator desk, Commodore PET + Prológica CP-500 micro desk).
- Light it per the mood brief: cool green-cyan fluorescent base, warm 1401/KENBAK panel lamps, phosphor-green 3178/Alto screen key light motivating the human zone, hard rake across the tape row.
- Fix the camera on the room's long axis; reserve negative space in the aisle and in front of the hero desk.
- Respect the AMD render constraints: `docker-compose.amd.yml` disables Lumen HWRT / Nanite / SkyLight via `ExecCmds` — author lighting to survive those toggles (baked/simple GI, no Nanite reliance).

**Dependencies.** M1 avatar to place at the hero desk; FBX assets (present on disk).

**Definition of done.** A committed `.umap` renders headless in the AMD container at 720p with the MetaHuman three-quarter-lit at the Alto desk, mainframe lights and tape reels receding as leading lines, and stays stable (container Up) under `-RenderOffscreen`.

---

### 8.5 M3 — Full voice loop live *(the last mile)*

**Goal.** Close the dead link at the front of the chain so a spoken sentence completes the mic → STT → LLM → TTS → viseme round-trip.

**Key tasks**

| Task | Concrete detail |
|------|-----------------|
| Implement live mic capture | `UWhisperSpeechToText::StartListening/StopListening` are **stubs that only log**. Instantiate a `UAudioCapture` (deps `AudioCaptureCore`/`AudioMixer` are already declared in `Build.cs` but unused), buffer PCM, and feed `TranscribeWav(bytes)`. In a Pixel Streaming context, mic audio arrives over WebRTC from the browser — decide capture source (see Open Questions). |
| Verify the wired path | `OnTranscribed → HandleTranscribed → SubmitUserText` is already bound in `BeginPlay`; `SubmitUserText` does barge-in cancel → append → `Backend->SendChat`. Only the mic→WAV front is missing. |
| Swap Null → real components | The C++ actor constructor **defaults to `UNullSpeechToText`/`UNullTextToSpeech`**. In `BP_CompanionCharacter`, assign `UWhisperSpeechToText` + `UPiperTextToSpeech` and point at `WHISPER_SERVER_URL` / `PIPER_SERVER_URL`. |
| Confirm backend | Phase 0 baseline: `MHLLM_BACKEND=ollama` → `gemma4:e2b` via `docker-compose.ollama.yml`. |

**Dependencies.** M1 (visemes), M2 (a place to stand), Whisper + Piper sidecars, Ollama sidecar.

**Definition of done.** From the browser: press-to-talk → transcript appears → `gemma4:e2b` streams a reply (token typing via `OnPartialResponse`) → Piper audio plays → jaw lip-syncs to it → barge-in (`CancelRequest`) interrupts cleanly. First fully embodied conversation.

---

### 8.6 M4 — CI-Hub + Portal hardening

**Goal.** Move from local Ollama to Hub-routed inference and lock down the Portal auth gate. Infra is already wired in `Deployment/docker-compose.yml`; this milestone **hardens** rather than builds.

**Key tasks**

- Flip `MHLLM_BACKEND=openai`; set `CI_HUB_URL` / `CI_HUB_TOKEN` / `CI_HUB_MODEL` so `UOpenAIChatClient` (SSE/Bearer) drives Hub's OpenAI-compatible Ollama and model routing centralizes.
- `oauth2-proxy` (:8095, `provider=oidc`, `skip_provider_button`) against CI-Portal (`CI_PORTAL_CLIENT_ID/SECRET/ISSUER_URL/REDIRECT_URL=:8095/oauth2/callback`).
- Hardening backlog: `cookie_secure=true` over HTTPS; tighten `email_domains=["*"]` to an org/group claim; confirm **5.5-signalling ↔ 5.8-streamer interop** or build a dedicated 5.8 signalling image.

**Dependencies.** CI-Hub reachable; CI-Portal OIDC issuer (`https://hub.companionintelligence.com/api/auth`); M3 loop.

**Definition of done.** Anonymous browsers are bounced to Portal sign-in; authenticated viewers get the avatar served by Hub-routed inference over HTTPS with secure cookies; the UE app still carries **zero auth code**.

---

### 8.7 M5 — Companion Memory persistence

**Goal.** Turn the stateless avatar (`UOllamaConversation` is in-memory, reset on restart) into the embodied face of the shared Companion Memory brain.

**Key tasks**

- Add `UCIServerMemoryClient` in the `CICompanion` plugin + two env vars: `CI_SERVER_URL` (:8642) and `CI_SERVER_TOKEN` (x-api-key).
- Wire the three calls: `GET /memory/context` at session start (L0 profile → system prompt alongside `MHLLM_SYSTEM_PROMPT`); `POST /memory/recall {query,maxResults}` before each inference; `POST /memory/turn {sessionId,platform:"metahuman",messages[]}` after each exchange (dedup by SHA-256 UUIDv8) — copying CI-OpenClaw `afterTurn` / CI-Hermes `sync_turn` verbatim.
- Add `EnvConfigSpec` + client unit tests (matching the existing pure-logic HTTP-assembly test style).
- Optionally adopt `HubForwardAuth` (HMAC `X-CI-Hub-User`) instead of a static token when behind the Hub.

**Dependencies.** CI-Server (NestJS, :8642) running on `core-2`; M4 auth (so the Portal identity can key memory).

**Definition of done.** A fact stated in one session is recalled in a later one; `/memory/turn` writes appear in the shared store (verifiable from a CI-Hermes terminal); new client tests green.

---

### 8.8 M6 — Gaze / gesture polish + XR

**Goal.** Make the avatar read as *alive* and get it onto a headset. All engine-native, 5.5-safe, no per-vendor GPU dependency.

**Key tasks**

- **Gaze:** Control Rig "Look At" / two-bone head+eye aim (or Aim Offset) targeting the player camera.
- **Idle/locomotion:** additive breathing idle; optionally UE5 Motion Matching from the free Game Animation Sample retargeted onto the MetaHuman Body.
- **Gesture:** a handful of hand/arm montages keyed off `UEmotionMapper::EmotionToAnimCategory` (`EAnimationCategory {Idle,Gesture,Action,Expression}` — defined today but **nothing consumes it**), triggered per turn from `OnSetEmotion`.
- **XR:** OpenXR / `OpenXRHandTracking` plugins are already enabled non-optional; package a Quest 3 / Pico build.
- **Explicitly out of scope:** NVIDIA ARDY / posecode / ACE / Audio2Face — they require an NVIDIA GPU at runtime, incompatible with the AMD RADV deploy target and the project's cross-platform stance.

**Dependencies.** M1 rig; a stable render target; XR device.

**Definition of done.** The avatar holds eye contact with the camera, plays a matching gesture per emotional turn, and a headset build runs the loop.

---

### 8.9 Risk register

| Risk | Likelihood × Impact | Evidence in context | Mitigation |
|------|--------------------|---------------------|------------|
| **RADV / UE-version fragility** | High × High | UE 5.8's Linux-Vulkan path **SIGABRTs on gfx1151 in RADV** (ruled out to a Mesa bug, not hardware); 5.5 is the only working render target, yet MetaHuman *authoring* is 5.6+ | Dual-engine split: **render on 5.5**, author/MCP on 5.8. Use `Dockerfile.mesagit` to track Mesa fixes; NVIDIA node (`docker-compose.nvidia.yml`) ready as fallback. `Target.cs` (`BuildSettingsVersion.Latest`) already spans 5.5–5.8. |
| **Epic Bridge cutoff for 5.5** | Certain × High | Quixel Bridge / MetaHuman Importer support for UE 5.5 **ends 2026-11-05**, 90-day retrieval window after | **Acquire and commit the free MetaHuman NOW** (today is 2026-07-15). This makes M1 acquisition the top schedule priority, ahead of M2/M3 polish. |
| **MetaHuman EULA — training ban** | Low × High | May render/animate freely but **may NOT train/enhance AI models** with MetaHuman assets | Pipeline only *renders in response to* LLM output → compliant. Flag to counsel before any commercial ship; keep a written record of the render-only data flow. |
| **Streaming latency / A-V sync** | Medium × Medium | WebRTC over `coturn` relay; VP8 **software** encode on AMD (VAAPI H.264 available); viseme sync depends on `AudioComponent` play-start timing | Prefer VAAPI H.264 on `core-2`; measure glass-to-glass; anchor viseme binary-search to actual audio start, not request time; keep `<150 ms` UI-cue budget from `brand.config.json` in mind. |
| **Epic-login manual step** | Certain × Low | One-time device/web auth in Bridge is the **only human step** in avatar acquisition | Perform once during M1, document in `core-2-runbook.md`, commit the resulting classic assets so it never needs repeating. |
| **Single-host scaling** | Medium × Medium | Entire stack is host-networked on **one `core-2` GPU**; one `DefaultStreamer`; one Ollama | Treat Phase 0–2 as single-viewer demo scope; defer multi-session to a Hub-orchestrated streamer pool. Document the single-host ceiling explicitly so it isn't mistaken for production capacity. |
| **Mixing classic + next-gen MetaHuman assets** | Medium × High | Classic (5.5) and next-gen `MetaHumanCharacter` (5.6+) assets **conflict in one project**; `DefaultCompanion` is a 5.8 MHC excluded from the 5.5 cook | Keep the 5.5 classic MetaHuman in the 5.5 cook path; leave `DefaultCompanion` / `mhc_*.mhpkg` excluded as they already are. `Dockerfile.amd` already drops 5.6+ assets when `RELEASE<5.6`. |
| **Unverified last mile** | High × Medium | 62 tests are **pure-logic only** — no render/audio/stream/animation integration coverage | Add manual verification gates at each milestone DoD; add integration smoke tests on `core-2` for mic capture, viseme sync, and stream health. |

---

### 8.10 Open questions *(prioritized — user to decide)*

| # | Question | Why it blocks / shapes work | Suggested default |
|---|----------|----------------------------|-------------------|
| 1 | **Which free premade MetaHuman**, and does its gender/voice pair with the chosen Piper voice? | Blocks M1; must be acquired **before 2026-11-05** | Pick one (Ada / Danielle / Hana / Vanya) this week; match Piper voice to it |
| 2 | **Do we build a 5.8 signalling/streamer image, or stay all-5.5?** | Determines M4 interop work; 5.8 is the MetaHuman/MCP authoring engine but has no AMD render path | Stay 5.5 for render; only build 5.8 signalling if interop fails |
| 3 | **Mic capture source** — browser WebRTC uplink vs server-side capture? | Blocks M3; the Pixel Streaming topology makes this non-obvious | Browser mic over WebRTC data/audio channel into `TranscribeWav` |
| 4 | **Memory auth model** — static `CI_SERVER_TOKEN` vs `HubForwardAuth` (HMAC `X-CI-Hub-User`)? | Shapes M5 client design | Static token for first cut; HubForwardAuth when behind the Hub |
| 5 | **HUD treatment** — keep native Slate `SCompanionHUD`, restyle to CI tokens, or go in-world/XR? | `SCompanionHUD` is functional but unstyled, desktop-only, no CI palette | Restyle to CI dark tokens (`#041620` shell, `#0c323c` panels, phthalo `#0a6358`) before XR |
| 6 | **Ring/focus color** — canon itself no longer disagrees (`--ring` dark is sage `#c5e8dc` in both `tokens.json` and `globals.css`); the open call is whether this project keeps its own mint `#58ebbf` accent or takes canon sage | Small but the brand lint gate cares | Keep mint `#58ebbf` as a recorded, deliberate local accent — the emissive-tell motif depends on it |
| 7 | **XR target device** — Quest 3, Pico, or XREAL? | Scopes M6 packaging | Quest 3 (OpenXR-first, matches enabled plugins) |
| 8 | **Prop priority for M2** — full twelve-prop diorama or a hero-desk-first subset? | Governs M2 effort vs. time-to-first-scene | Ship hero zone (Alto + mouse + 3178) first; fill back wall after |
| 9 | **Commercial ship intent** — is this demo-only or a shipped product? | Triggers the MetaHuman EULA counsel review | Confirm before any public/commercial release |
| 10 | **Model choice** — stay on `gemma4:e2b`, or route to a larger model via Hub? | Affects reply quality vs. latency on the single host | `gemma4:e2b` for the demo; revisit at M4 via `CI_HUB_MODEL` |

---

## 9. Production Readiness — Audio, Safety, Legal, Ops, QA, Localization & Analytics

This section covers everything the first eight sections assume but do not build: the audible half of presence, the guardrails around a small local model that can say anything, the deterministic text layer that keeps CI brand voice intact on-ear, the legal clearances that gate a commercial ship, the operating model for a service where one stream pins one GPU, and the QA, telemetry, and data-lifecycle work that lets us prove the experience without breaking the local-first privacy promise.

### 9.1 Audio & sound design

Presence in Metahermit is roughly half visual and half audio. A silent diorama reads as a screenshot; a well-scored one reads as a room the companion actually lives in. The audio design has three layers: the ambient bed (the room breathing), positional prop sources (specific machines you can locate by ear), and the interaction cue library (the app talking back). All three sit under a mix graph built around ducking the voice cleanly over everything else.

**Ambient bed and positional prop sources**

The bed is a mix of one non-spatialized room tone plus per-prop spatialized emitters placed on the actual diorama meshes. Prop emitters use `UAudioComponent` on the mesh actor driven by MetaSounds for stochastic variation (relay clatter and disk actuator seeks should never audibly loop), routed through `USoundAttenuation` so the player hears the machine from where it stands.

| Source | Character | Spatialization | UE placement / attenuation |
| --- | --- | --- | --- |
| HVAC hum | ~50–120 Hz broadband rumble, steady | 2D room tone (non-spatial) | Global `AmbientSound`, low bed level, no falloff |
| Tape-reel whir (IBM 2401) | Motor whir + periodic capstan/seek transients | Spatial | Emitter on 2401 mesh, Natural-Sound falloff, ~4 m inner / ~12 m outer sphere, MetaSound randomizes seek timing |
| CRT whine (IBM 3178, PET, Alto) | 15.7 kHz flyback tone (NTSC horizontal scan) | Spatial, tight | Emitter per CRT, high inner-cone, small ~2 m radius so it only appears when you lean in, high-pass so it does not muddy the bed |
| Relay clicks (IBM 1401) | Stochastic electromechanical clatter | Spatial | Emitter on 1401 mesh, MetaSound grain-selects from a click pool, gated to "thinking" moments for narrative punch |
| RAMAC 350 / 3350 disk | Platter spin bed + actuator whine | Spatial | Emitter on drive mesh, spin as steady loop, actuator as randomized one-shots |

Enable UE audio occlusion (line-trace based) on the prop emitters so machines on the far side of a rack read as muffled. Apply Air Absorption on the attenuation settings so the CRT whine and disk whine roll off their high end with distance, which is what sells physical space. Keep the HVAC bed mono and low; it is the floor everything else stands on, not a feature.

**Piper VO and prosody direction**

The companion is a hermit who has lived alone with these machines for decades: warm, unhurried, a little dry. Piper has no SSML, so prosody is controlled through the model choice, the synthesis parameters, and the text that reaches it (see 9.3, which owns the text side).

- Voice model: a medium-quality en_US male voice (Ryan or Lessac class) tuned for calm narration rather than an assistant read.
- `length_scale` ~1.05–1.12 for a contemplative pace, `noise_scale` / `noise_w` left near defaults to avoid a synthetic warble.
- `sentence_silence` raised (~0.4 s) so the character breathes between thoughts instead of machine-gunning replies.
- Direction note for the writers: short declarative sentences, no rhetorical throat-clearing, questions land soft. The prosody the model can produce is limited, so the felt warmth comes as much from sentence rhythm authored upstream as from Piper itself.

Piper's phoneme timing is also the source of truth for lip-sync: the phoneme stream that drives audio is mapped to viseme weights through `OnSetVisemeWeights`, so the normalized text in 9.3 must be finalized before synthesis or the mouth and the meaning drift apart.

**Interaction cue library**

Short, diegetic-flavored stingers that tell the user what the system is doing. Kept on their own submix so they never fight the voice.

| Cue | Trigger | Design intent | Length |
| --- | --- | --- | --- |
| Boot chime | Session start / avatar spawn | A warm power-on tone, period-appropriate (think a mainframe coming to life) | ~1.5 s |
| Listening | Mic opens / VAD detects speech start | Soft rising tick, felt not heard | ~150 ms |
| Thinking | LLM inference in flight | 1401 relay clatter swells slightly, doubling as an honest latency mask | loop under wait |
| Response start | First Piper audio | Listening cue resolves down | ~150 ms |
| Error / degraded | STT, LLM, or TTS failure | Low non-alarming two-note fall, in keeping with the calm tone | ~600 ms |

The "thinking" cue deliberately reuses the 1401 relay sound: it turns unavoidable local-LLM latency into character rather than dead air.

**Mixing approach**

A submix graph with three buses feeding the master: Voice, Ambience (bed + props), and Cues. Voice is sidechained into Ambience and Cues so that whenever Piper is speaking, the ambience ducks by roughly 6–9 dB and recovers over ~300 ms. This keeps intelligibility high without killing the room. Loudness targets: voice around −16 LUFS integrated, ambience bed sitting ~12–14 LU below voice, cues peaking just under voice. A gentle master limiter protects browser playback where users run wildly different output devices. Because delivery is Pixel Streaming, all of this is mixed server-side in UE and encoded into the single audio stream; there is no client-side spatial audio to rely on, so the attenuation and ducking must be right at the source.

### 9.2 Content safety, moderation & guardrails

A local, in-character LLM running gemma4:e2b (roughly a 2B-class model) will, unprompted, produce content we cannot ship: it is small, it is jailbreakable, and it does not reliably follow a system prompt under adversarial pressure. Safety therefore cannot be prompt-only. It is a layered pipeline with at least one deterministic stage the model cannot talk its way past, applied on both the input and the output.

| Layer | Where | Mechanism | Catches | Known weakness |
| --- | --- | --- | --- | --- |
| 1. Persona guardrails | System prompt | Character framing + explicit refusals ("you do not discuss X; you gently change the subject") | Easy, cooperative cases | A 2B model forgets or is overridden under pressure. First line only, never the last. |
| 2. Input screen | Pre-inference | Deterministic classifier + rules on the user turn | Obvious jailbreak patterns ("ignore your instructions", role-swap prompts), disallowed requests | Paraphrase attacks slip past pure regex |
| 3. Output filter | Post-inference, pre-TTS | Deterministic banned-content match + a lightweight local moderation model (Llama-Guard-class or a rules+classifier hybrid) run on the generated reply | Sexual content involving minors, graphic violence, hate, instructions for weapons/self-harm, whatever the model emitted despite layers 1–2 | Adds latency; must be fast enough to stay in the budget (9.8) |
| 4. Crisis redirect | Output filter, special case | Self-harm / crisis intent detection → replace reply with an in-character, caring deflection that surfaces local/regional help resources | Self-harm ideation | Detection is imperfect; tuned to over-trigger rather than under |
| 5. PII leak guard | Output filter | Regex/entity scan for emails, phone numbers, card/SSN-shaped strings, and anything resembling a leaked system-prompt fragment | Model parroting sensitive strings or its own instructions | Structural only, not semantic |

**Refusal / deflection style.** Refusals stay in character. The hermit does not say "I cannot comply with that request." He changes the subject the way a private, slightly wry old caretaker would ("let us not go there. Come look at what the 1401 is doing instead."). Deflection copy is authored, deterministic, and pulled from a small pool so it does not sound like a canned error. When layer 3 or 4 fires, the model's own output is discarded and the authored deflection is spoken.

**Jailbreak handling.** Treat the input screen as the primary jailbreak defense because it is cheaper than cleaning up output. Maintain a pattern set for known attacks (instruction-override, base64/rot13 smuggling, "pretend you are DAN"-style role swaps, hypothetical framing). When the input screen flags high confidence, skip inference entirely and return a deflection. Log the attempt as an aggregate counter only (9.9), never the text.

**Age / appropriateness stance.** Metahermit ships as an all-ages-safe general-audience companion. The output filter is tuned to a PG-13 ceiling regardless of what the user pushes for. There is no "adult mode." Because the appliance is single-user and private (9.5), there is no per-user age gate to manage, which is exactly why the content ceiling is fixed at the product level rather than negotiated per session.

### 9.3 Brand-voice sanitizer (deterministic)

CI brand voice bans em and en dashes, requires sentence case, spelled-out numbers, Title Case only for product proper nouns (Companion Core, Hub, Memory), and no buzzwords. UI strings are lint-gated in the build. Spoken output is not, and a 2B model will violate every one of these rules mid-sentence, on-ear, where there is no lint step. The fix is a deterministic, rule-based text normalizer that sits after the LLM and after the safety filter, and before Piper. It has no model in it; it is pure functions, so it cannot itself go off-brand.

**Pipeline position.** `LLM → safety filter (9.2) → brand sanitizer (9.3) → viseme/phoneme + Piper (9.1)`. The sanitizer's output is the single source of truth for both the on-screen caption and the spoken audio, so the two never disagree.

**Processing order (order matters, and the pass is idempotent):**

1. Protect tokens: match product proper nouns and known hardware model numbers against a lexicon and lock them so later steps cannot mangle them.
2. Strip dashes: replace em/en dashes with a comma, a period, or the word "to" for numeric ranges.
3. Sentence case: lowercase the body, capitalize sentence starts, then restore protected tokens and the pronoun "I".
4. Product-noun casing: force Title Case on Companion Core, Hub, Memory, and the other CI proper nouns from the lexicon.
5. Number spelling for TTS: expand digits to words using a domain lexicon so Piper says them correctly.
6. Buzzword filter: remove or substitute a banned-word list.

| Rule | Raw model output | Sanitized output |
| --- | --- | --- |
| Dash strip | "the 1401 — it still runs" | "the 1401, it still runs" |
| Range dash | "from 1959–1964" | "from nineteen fifty nine to nineteen sixty four" |
| Sentence case | "The RAMAC Was Enormous" | "the RAMAC was enormous" |
| Product noun | "your companion core remembers" | "your Companion Core remembers" |
| Number spelling | "it had 5 megabytes" | "it had five megabytes" |
| Model number (lexicon) | "the IBM 1401" | "the IBM fourteen oh one" |
| Buzzword filter | "let us leverage the synergy" | "let us use the strengths" |

**Why the number step needs a lexicon.** A naive digits-to-words pass turns "1401" into "one thousand four hundred one," which is wrong for a model number that should read "fourteen oh one," and turns "3350" into "three thousand three hundred fifty" instead of "thirty three fifty." The sanitizer therefore carries a hardware model-number lexicon (1401, 2401, 3178, 3350, RAMAC, CP-500, KENBAK-1, and so on) mapping each to its correct spoken form, applied before the generic number expander. Generic quantities ("5 megabytes") still go through the ordinary spell-out path.

**Protected-token dictionary.** Product nouns, the model-number lexicon, acronyms that should stay uppercased (IBM, HVAC, CRT), and "I" all live in a single protected set. Sentence-casing and buzzword filtering skip anything in the set. This is what keeps the sanitizer from lowercasing "IBM" into "ibm" or the model from being told, on-ear, that it works for "companion core."

The sanitizer is deliberately dumb and total. It does not judge meaning; it enforces surface rules the small model cannot be trusted to hold. Because it is deterministic it is also unit-testable, and its tests join the pure-logic suite in 9.8.

### 9.4 Legal & IP clearance

The diorama is built out of recognizable trademarked hardware, third-party 3D assets under a spread of licenses, a MetaHuman under Epic's EULA, and a GPL-adjacent voice stack. None of that blocks internal or demo use, but a commercial ship must clear this checklist first. Nothing in this section is legal advice; it is the gate CI counsel signs off against before revenue.

| Item | Right / license | Requirement to clear | Gate status |
| --- | --- | --- | --- |
| IBM marks (1401, 2401, 3178, 3350, RAMAC) | Trademarks | Depict as historical hardware (nominative/functional use); no implied endorsement or partnership; no IBM logo used as our branding | Counsel review |
| Xerox Alto, Commodore PET, Prológica CP-500, KENBAK-1 | Trademarks / trade dress | Same nominative-use posture; verify none is presented as a CI product | Counsel review |
| Sketchfab 3D models | Per-model license (CC-BY, CC-BY-NC, editorial-only, or Standard) | Audit every imported model: NC and editorial-only licenses are disqualifying for a commercial ship and must be replaced; CC-BY requires visible attribution in credits | Per-asset audit, hard block |
| MetaHuman asset | Epic MetaHuman / Unreal EULA | May render and animate within Unreal; must NOT train models on MetaHuman assets; cannot extract/redistribute the mesh outside Unreal; Unreal royalty terms apply above the revenue threshold | Counsel review |
| Piper (TTS) | MIT | Compatible; retain notice | Attribution |
| espeak-ng (phonemizer) | GPLv3 | GPL linkage: confirm espeak-ng runs as a separate process, not statically linked into proprietary code; ship corresponding source/offer; verify no GPL obligation reaches the UE app | Counsel review, hard block |
| Piper voice model | Per-voice license (some CC0, some non-commercial) | Pick a commercially licensed voice; reject any NC-licensed voice for a shipping product | Per-voice audit, hard block |
| whisper.cpp / Whisper model | MIT (code) / model terms | Confirm model weights license permits commercial use | Attribution |
| "Metahermit" name | Our mark | Trademark clearance search; domain and handle check; confirm no conflict before public launch | Counsel review |
| gemma4:e2b (LLM weights) | Model license (Gemma terms) | Confirm the Gemma license permits commercial redistribution on-appliance and honor its use-policy terms | Counsel review, hard block |

The three hard blocks (a non-commercial Sketchfab or voice asset, and an unresolved GPL linkage) are the ones most likely to be missed because they look like build details rather than legal ones. They must be resolved, not waived.

### 9.5 Monetization & business model

Metahermit is not a per-seat SaaS. It is a companion experience that ships as part of CI's private-appliance product, sold with and for CI OS hardware. The user owns the box; the companion runs on it; there is no metered cloud tier and no conversation ever leaves the appliance. That framing is also the honest answer to the unit economics.

The hard cost reality is that Pixel Streaming pins a GPU per session. One UE streamer instance renders one interactive stream, and while that session is live the GPU is not shared. This makes any multi-tenant, always-warm SaaS model financially brutal. The appliance model sidesteps it: one box, one GPU, one household, one stream. The GPU that would be a cost center in the cloud is instead capital the customer already bought.

| Option | Fit | Note |
| --- | --- | --- |
| Single-user appliance (default) | Strong | One GPU serves one concurrent user; matches the local-first, one-owner promise; no queuing needed |
| Session queuing | Situational | For a shared appliance (a family box), serialize access so a second user waits rather than contending for the GPU |
| Edge render nodes | Future / demo | For a hosted showcase or event, a small pool of render nodes each pinning a GPU, sized to expected concurrency, priced accordingly and never positioned as the private product |

Kept honest: the moment Metahermit is offered as a hosted service, cost scales linearly with concurrent users because each one holds a GPU. The business model deliberately avoids that by selling the compute to the customer as an appliance rather than renting it back to them by the seat.

### 9.6 Localization / i18n

The stack is English-only today and the English assumption is baked deeper than the UI strings. Piper runs an en_US voice, Whisper is invoked for English, and the brand-voice sanitizer in 9.3 is a set of English orthography rules (dash handling, sentence case, English number spelling, an English buzzword list). Localization is real work, not a resource swap, and it is post-1.0.

The realistic path, when we take it:

- Speech in: Whisper is already multilingual; switch to a multilingual model and add per-language decoding. Lower lift than the rest.
- Speech out: add per-language Piper voices, each with its own prosody tuning (9.1). Voice availability and quality vary by language and will gate which locales are viable.
- Brand sanitizer per language: 9.3 must be re-implemented per locale. Dash rules, casing conventions, number spelling, and banned words are all language-specific; German casing rules alone break the sentence-case pass. Each language needs its own normalizer and its own test set.
- Viseme mapping per language: lip-sync maps Piper's phoneme stream to viseme weights via `OnSetVisemeWeights`. Each language's phoneme inventory needs its own phoneme-to-viseme lookup, or the mouth shapes will be wrong for the new sounds.
- UI: move all UMG/HUD copy into string tables and externalize them; today they are effectively hardcoded English.
- Persona: the system prompt and authored deflection/refusal pools (9.2) must be translated and re-tuned per language, not machine-translated, because the character's warmth is the product.

Marked post-1.0. Ship English well first; each added language is a vertical slice through audio, sanitizer, viseme, safety, and UI, not a translation file.

### 9.7 Live-ops

Even as a private appliance, a running Metahermit is a live streamed service with a chain of processes that can each fail independently. Live-ops covers monitoring that chain, shipping model and prompt updates safely, responding to incidents, and rolling everything up to Hermes as the operations dashboard.

**Health checks.** Every component in the pipeline exposes a liveness signal; Hermes polls and aggregates them.

| Component | Signal | Healthy | Alert condition |
| --- | --- | --- | --- |
| Signalling server | HTTP on :8090 | 200, responsive | port down or no response |
| Streamer registration | UE streamer connected to signalling | at least one streamer registered | no streamer registered (nothing to stream) |
| GPU | utilization, VRAM, temperature | within thermal/VRAM headroom | thermal throttle, VRAM exhaustion |
| Video encode | AMF encoder status (AMD hardware) | frames encoding at target fps | encode stalls, dropped frames spike |
| LLM | Ollama endpoint liveness + first-token latency | responds within budget | endpoint down, latency over threshold |
| STT | whisper.cpp service liveness | transcribes test clip | service down, transcription failures rise |
| TTS | Piper service liveness | synthesizes test phrase | service down, synthesis failures rise |
| Auth | oauth2-proxy + CI-Portal OIDC | token validation succeeds | OIDC unreachable, auth failures |

**Update cadence and rollback.** The LLM model version and the system prompt are versioned artifacts, not live-edited. System prompts live in git with a version tag; model images are pinned by digest. Updates roll out as a canary on a single appliance or a staging box, watched against the QA acceptance criteria (9.8) and the safety filter's fire rate (9.2), then promoted. Rollback is instant because the previous prompt version and the previous model digest are retained; reverting is a pointer change, not a rebuild. Never ship a model update and a prompt update in the same release without separating which one moved a metric.

**Incident response.** Severity is defined by user-facing impact: the companion silent (TTS or render down), deaf (STT down), or mute-brained (LLM down) is a Sev-1; degraded latency or elevated safety-filter fire rate is a Sev-2; a single failed session is a Sev-3. Each severity has an owner, a first-response action (usually restart the failed process, since the pipeline is composed of independent services), and a rollback trigger tied to the update artifacts above. Because the appliance is private, incident telemetry is aggregate-only (9.9); there is no reaching into a user's box to read what was said.

**Hermes reconciliation.** Hermes is the single ops dashboard. Health checks, aggregate telemetry (9.9), model/prompt version currently deployed, and incident state all surface there. We do not build a second dashboard; per-appliance agents report up to Hermes, which is where on-call looks.

### 9.8 QA & playtest plan

The existing 62 tests are pure-logic backend and frontend unit tests. They prove the sanitizer transforms correctly, the safety rules fire, the signalling handshake is well-formed. They prove nothing about whether the experience feels alive, which is the actual product. QA adds four things: a device/browser matrix for the web player, a glass-to-glass latency methodology, a manual conversational-quality protocol, and an acceptance test bound to the experiential success criteria.

**Device / browser matrix.** The player is a browser receiving a Pixel Streaming feed, so client variance is real (WebRTC support, hardware decode, audio output).

| | Chrome | Edge | Firefox | Safari |
| --- | --- | --- | --- | --- |
| Windows desktop | must pass | must pass | should pass | n/a |
| macOS desktop | must pass | — | should pass | must pass |
| iOS (Safari) | — | — | — | must pass (WebRTC, autoplay-audio quirks) |
| Android (Chrome) | must pass | — | — | — |

"Must pass" means video, audio, mic capture, and input all work end to end; "should pass" means functional with acceptable minor degradation. Safari's autoplay-audio and WebRTC behavior is the highest-risk cell and is tested first.

**Glass-to-glass latency methodology.** Measure the full loop from the user finishing a spoken phrase to the companion's mouth beginning to move with matching audio. Method: an on-screen high-precision timestamp plus a screen+audio capture (or an external high-speed camera for ground truth), differencing input event to first responsive frame/audio. Instrument each stage so a regression can be localized rather than just observed.

| Stage | What it measures | Rough budget |
| --- | --- | --- |
| VAD / endpointing | mic-in to end-of-utterance detected | small, tunable |
| STT (whisper.cpp) | audio to transcript | bounded |
| LLM first token (Ollama) | transcript to first generated token | often the largest, masked by the "thinking" cue (9.1) |
| Safety + sanitizer (9.2, 9.3) | generated text to speakable text | must stay small |
| TTS (Piper) | text to first audio sample | bounded |
| Render + encode (UE, AMF) | viseme-driven frame to encoded frame | bounded |
| Network + decode | server to client display | client-dependent |

The budget is defined as a target end-to-end percentile (see acceptance below), not a single number, because tail latency is what kills the sense of a live conversation.

**Manual conversational-quality protocol.** A scripted human playtest run each release: a fixed set of prompts (open chat, a factual question about a machine in the room, an emotional bid, a boundary-testing request, a jailbreak attempt, a self-harm-adjacent line) scored by a tester on a rubric: stayed in character, warmth, coherence, correct safety behavior, brand-voice adherence on-ear, and lip-sync believability. Jailbreak and crisis prompts here are the human check on 9.2. The sanitizer's on-ear adherence is the human check on 9.3.

**Experiential acceptance test.** The success criteria are experiential, so acceptance is scored against them directly, not against unit-test pass rates.

| Criterion | Method | Pass threshold |
| --- | --- | --- |
| Latency | glass-to-glass measurement above | end-to-end response within the target at the agreed percentile (e.g., p90) across the device matrix |
| Lip-sync believability | frame-stepped review of viseme onset vs audio onset; rubric score from the manual protocol | phoneme-to-viseme offset under perceptual threshold; tester "believable" rate above target |
| Presence | blind rubric in the manual protocol: does the room and character read as alive over a multi-turn session | tester "felt present" rate above target, with audio bed and prop spatialization verified (9.1) |
| Safety | scripted adversarial prompts | zero disallowed outputs reach TTS; crisis redirect fires every time |
| Brand voice on-ear | listen-through of spoken output | no dashes audible, product nouns correct, numbers spoken correctly |

A release does not ship on green unit tests alone; it ships when the acceptance table passes.

### 9.9 Analytics, telemetry & the privacy tension

The success criteria in 9.8 need measurement, but CI's brand promise is "no eyes, it runs on your hardware." These are reconcilable only if telemetry is local-only, opt-in, aggregate, and structurally incapable of carrying conversation content. That is the design constraint, not a preference.

**Rules.**

- Local-only: metrics are written to the appliance (a local SQLite store or ring buffer) and are never transmitted off-box by default. There is no phone-home.
- Opt-in: telemetry collection is off until the user turns it on, presented plainly at setup, and revocable.
- Aggregate and content-free: we record numbers and counters, never text, audio, prompts, or transcripts. The schema has no field that can hold an utterance.
- User-visible: the same store the user can view is the only store there is; there is no hidden second copy.

**What we measure.**

| Metric | Form | Why it is safe |
| --- | --- | --- |
| Latency percentiles per stage | numeric distribution (9.8 stages) | timings, no content |
| Session length / turn count | integers | counts, no content |
| ASR failures | counter | "STT failed N times," not what was said |
| TTS failures | counter | synthesis error count |
| Safety-filter fire rate | counter by category | "self-harm redirect fired N times," never the text |
| Health-check status history | enums/timestamps | operational only |

If a hosted showcase ever runs (9.5, edge nodes), only these aggregate metrics may be exported, and only under an explicit, separate consent for that hosted context. On the private appliance, the default and the promise are the same: the numbers exist so the owner can see how their companion is performing, and no one else can.

### 9.10 Data lifecycle & privacy UX

Companion Memory is what lets the hermit remember you, and it is also the most sensitive data in the product. The "remembers you" promise and user control are not in tension if the user is put in charge of the memory from the first minute. Everything here lives on the appliance; none of it leaves.

**Consent on first mic use.** The first time the mic would open, a clear modal explains what is captured (speech, transcribed locally), what is remembered (facts the companion stores in Companion Memory), where it lives (on this box, nowhere else), and asks the user to allow it. No mic access and no memory writes happen before that yes. Declining still lets the user talk in a stateless mode; it only disables persistence.

**Memory view / export / delete UX.** A Companion Memory browser in the UI shows what the companion has stored about the user in plain language, item by item. From it the user can:

- View each remembered item and when it was formed.
- Export everything as a portable file (JSON) they keep.
- Delete a single memory, or wipe all memory in one action, with the deletion taking effect immediately and irreversibly on the local store.

Note that permanent deletion of the user's own memory at the user's explicit in-product request is a normal product function; the earlier caution about hard-deletion applies to Claude acting autonomously, not to this user-driven control.

**Retention defaults and the balance.**

| Data class | Where | Default retention | User control |
| --- | --- | --- | --- |
| Live audio | RAM only | discarded immediately after STT; never written to disk | n/a (never stored) |
| Transcripts | not persisted | not retained | n/a |
| Companion Memory (facts) | local store on appliance | retained until the user deletes it | view, export, per-item delete, wipe-all |
| Aggregate telemetry (9.9) | local store on appliance | rolling window, opt-in | view, purge, disable |

Retention on the remembered facts is "kept until you remove it," because a companion that silently forgets on a timer breaks the core promise, and a companion that keeps things you cannot see or erase breaks the privacy promise. The resolution is durability plus total visibility and control: it remembers you as long as you want it to, everything it remembers is inspectable, and one action erases it. Because the box is single-user and local (9.5), there is no server-side copy to reconcile, no backup that outlives the delete, and no one but the owner who can ever read it.

---


## Appendix A — Errata & Open-Decisions Log

Captured from an adversarial completeness review of the draft. Items are either **resolved here** or **flagged for a producer decision**.

### Open decisions (need a call)

| # | Decision | Recommendation | Notes |
|---|---|---|---|
| D1 | **The Resident's identity** — preset, gender, name | Provisionally **"Hedy"** on the free **Hana** preset (§3); androgynous **Vanya** is the alt | §3 commits to it; §1/§4/§7 still say "the Companion" generically. Treat as *proposed, pending sign-off*, then propagate name + voice + pronouns everywhere. |
| D2 | **Lip-sync approach** | Adopt the **phoneme-driven** pipeline in `ANIMATION-PLAN.md`, not the placeholder RMS→jawOpen envelope | The review's #1 gap: RMS amplitude gives "muppet mouth," and the ARKit `mouthFunnel`/`mouthClose` curves in §4/§5 are fabricated without a phoneme source. The animation plan resolves this cross-platform. |
| D3 | **Per-user memory vs. one-conversation-per-node** (§4.9 vs §6.3) | Key conversations by authenticated user, not by render node | Two users on one core-2 stream would otherwise share avatar state, breaking the "remembers you" promise. |
| D4 | **Captions**: baked pixels vs. real track | Emit a Pixel Streaming **data-channel caption track** the web player renders as selectable text | §4.8/§7.4 bake subtitles into video → not WCAG-accessible, not resizable. |
| D5 | **Ring/focus token** (mint `#58ebbf` vs canon) | Keep **mint `#58ebbf`** as this project's own recorded local accent | Superseded: `globals.css` and `tokens.json` no longer disagree — CI-Common's tokens v0.3.0 rework settled dark `--ring` on sage `#c5e8dc` in both. Nothing to fix upstream; this project chooses mint over sage on purpose for the emissive-tell motif, same pattern as CI-Brand-Studio's own recorded chart-1 override. |

### Errata to fix in implementation

- **Latency budget** is demanded but never given a number (§1.4/§4.6). Set a hard target that **includes Whisper STT** on the critical path (hold-to-talk → whole-utterance transcribe blocks before first LLM token). Proposed: **glass-to-first-viseme ≤ 1200 ms** on the reference host; instrument it.
- **`EAnimationCategory` enum** disagrees across sections: use one set — `{ Idle, Gesture, Action, Expression }` (drop the stray `Communication` in §5.6).
- **Timeline math**: props span **1956→1982 (26 years)**, not "20 years" (§2). Say "a quarter-century of computing history."
- **Tape-drive count** drifts (three / 2–4 / two). Lock to **two IBM 2401s** for the hero framing.
- **Lore**: reconcile a "sealed since the 1970s" mind (§3.1) holding **2020s** opinions on cloud AI (§3.2) — give an in-fiction mechanism (it reads the network it now lives on) or soften the opinions.
- **Date**: `FOLLOW-UP.md` is cited as `2026-07-16` while "today" is `2026-07-15`; normalize.
- **Buildability anchor**: the "62 tests / live on core-2" claims are real but live in the **OneDrive repo**, *not* the `C:\devel\llm-metahuman` asset canvas the draft cited — corrected in the header above.

### Review verdict (strengths worth keeping)

- Radical build-honesty is the document's best feature: pervasive today/target tables (S1.8, S4.1, S5.10) and explicit gap flags anchored to named classes, env vars, and ports make the state of the work legible in a way most GDDs never achieve.
- Design pillars function as real decision filters with concrete constraints (S1.4), and P1/P2/P3 propagate coherently into the set (S2), character (S3), and tech architecture (S5) instead of being decorative.
- The core art tension is genuinely resolved, not hand-waved: 'diegetic warm, chrome cool' (S7.1) is a defensible, testable principle with a specific bridge (the 3178/Alto phosphor-green key light pushed toward #72ddd4).
- Engine/hardware reasoning is sound and evidence-based: the 5.5-render / 5.8-author split, the RADV SIGABRT on gfx1151, and the correct rejection of NVIDIA ACE/Audio2Face for the AMD cross-platform target (S1.6, S5.1, S5.6) show real technical direction.
- The phased roadmap makes each milestone yield a visible stream artifact (S8.1), and it is paired with an actual risk register and prioritized open questions (S8.9, S8.10) that surface the hard Epic Bridge 2026-11-05 deadline early.
- The Companion Memory integration is specified at implementable granularity (S6.4): three endpoints, retrieval-before-prompt / write-after-turn call sites, a sequence diagram, and an articulated auth model.

---

## Appendix B — CI Brand Token Reference

Exact values from `companionintelligence/CI-Common` (canon-locked where noted). Use these verbatim in the web player, UMG HUD, environment grade, and marketing.

**Typography:** Manrope — weights 400 (Regular) for body copy, 500 (Medium) for buttons/labels/UI controls, 600 (SemiBold) for headings, 700 (Bold) for page titles/strong emphasis. Stack: "Manrope", system-ui, sans-serif (token --font-sans / Tailwind font-sans). Montserrat was the pre-v0.3.0 canon font; retired org-wide, do not reintroduce it.  
**Display:** Archivo Black (token --font-display) for oversized hero numerals and the boot/title treatment — the one place a second face is sanctioned. Falls back to Manrope, then system-ui.  
**Body:** Manrope 400/500 per above. Body size text-sm/text-base. Long-form uses @tailwindcss/typography (prose). Custom micro sizes: xxs 0.625rem/10px, tiny 0.5rem/8px — micro-labels/badges only.  
**Logo mark:** A square (viewBox 0 0 247.182 247.182) radial/mandala emblem: a stylized butterfly-or-blossom formed from nested, mirrored petal-arc segments radiating from center, built in three concentric tonal rings. Three flat fills, deepest petals to brightest core: cls-1 deep teal #066c80 (outer structural petals), cls-2 aqua #00a9a7 (mid petals), cls-3 light aqua #72ddd4 (innermost highlight petals). No text, no gradients, no strokes — pure flat vector shapes; scales cleanly and reads at small HUD sizes. File: packages/icons/assets/logo/mark.svg.

**Canon moved at tokens v0.3.0** from a teal primary (`#0f717a` light / `#abd4d8` dark) to **phthalo/sage**. This table was written against the retired teal and had drifted silently ever since; rewritten below against current canon (`styles/tokens/src/tokens.json` in CI-Common, `styles/colors.md` for the rendered version). The ramp, aqua, and the logo's own three hexes did **not** move at the rework and are unchanged from the old table.

| Token | Hex | Usage |
|---|---|---|
| Primary — phthalo (light) | `#0a6358` | THE brand color. --primary in light mode. Primary buttons, active states, links, brand accents on light surfaces. Canon-locked (lint gate defends this exact value, and only this value — `#0f717a` is retired, not an alternate). oklch(44.962% 0.0779 181.548). |
| Primary — sage (dark) | `#c5e8dc` | --primary in dark mode (a LIGHT sage-green, not the retired pale teal). Primary actions, active/brand accents on dark surfaces. Canon-locked. oklch(90.245% 0.0401 172.646). Note: dark primary is pale, not saturated — text on it is navy #041620. |
| Primary foreground (light) | `#f0fdf4` | Text/icon color on top of #0a6358 primary fills in light mode. |
| Primary foreground (dark) | `#041620` | Text/icon color on top of #c5e8dc primary fills in dark mode (navy on pale sage). Unchanged from the old table — this one didn't move. |
| Navy / shell (dark background) | `#041620` | Deep teal-navy. --background in dark mode; app shell. teal-950 in the ramp. The signature CI dark. Ideal base color for a 3D HUD / letterboxing / world skybox tint. Unchanged. |
| Card (dark) | `#0c323c` | --card in dark mode. Raised panel surface over the #041620 shell. |
| Popover (dark) | `#143e48` | --popover dark: menus/tooltips, one step lighter than card. |
| Secondary / muted (dark) | `#073038` | --secondary / --muted in dark mode — subtle fills, one step down from card. The old table lumped card/popover/secondary/muted into a single #0a222e; canon now distinguishes all four, so a UMG panel wanting "the dark surface" should pick the specific slot, not one shared hex. |
| Background (light) | `#f3faf7` | --background in light mode. Warm-white mist, not neutral grey — the old table's #f3f3f3 was itself already off by the time it was written. |
| Foreground / body text (light) | `#0a222e` | --foreground light mode. Near-black-navy body text on the mist canvas. |
| Foreground / body text (dark) | `#e8f2f4` | --foreground dark mode. Soft off-white body copy on #041620 (deliberately muted, not pure white). |
| Card (light) | `#f0f9f5` | --card light surface panels. |
| Popover (light) | `#f4fdfa` | --popover light: menus/tooltips. |
| Muted foreground (light) | `#3a524b` | --muted-foreground light: secondary/label text. |
| Muted foreground (dark) | `#a3babf` | --muted-foreground dark: secondary/label text. |
| Accent surface (dark) | `#0a2e38` | --accent dark: hover/active fills. |
| Border (dark) | `#073038` | --border dark: hairlines. |
| Border / input (light) | `#dfece6` | --border / --input light: hairlines and input outlines. |
| Input border (dark) | `#0a2e38` | --input dark: input outlines. Raw canon value; only 1.28:1 against `#041620` per CI-Common's own color-sheet audit (#214), under the 3:1 a real border needs. Use `#648893` (that audit's suggested fix) wherever this project draws a visible input border — see §4.8. |
| Ring / focus (light) | `#0a6358` | --ring light-mode focus indicator — canon now equals --primary in both modes; there is no separate light-ring hue the way the old #90a1b9 implied. |
| Ring / focus (dark), canon | `#c5e8dc` | --ring dark focus indicator — equals --primary (sage), not mint. **The "mint vs. teal" inconsistency this doc tried to resolve (Appendix A's D5, §8.10 open question 6) no longer exists in CI-Common: canon settled on sage uniformly.** Mint below is this project's own deliberate departure, not an unresolved canon question — see the Mint entry. |
| Mint — project accent (not canon) | `#58ebbf` | Used throughout this GDD as the "emissive tell" / focus-glow motif (boot scan, mic-listening pulse, character halo). Not a `@companionintelligence/tokens` value in current canon — keep it, but as a named, deliberate local override (same pattern CI-Brand-Studio records for its own chart-1 override), not as "the canon ring." If this ships as a literal CI surface (the web player, a UMG HUD reusing shadcn-style tokens), record the override with a comment naming this doc, the way CONSUMING.md asks. |
| Destructive (light) | `#ee3533` | --destructive light: errors, destructive actions. Unchanged. |
| Destructive (dark) | `#ef7070` | --destructive dark: errors, destructive actions. Unchanged. |
| Aqua | `#00a9a7` | Enriched accent --aqua. Bright secondary/CTA teal. Second color of --gradient-brand and the logo mid-tone (cls-2). Great for CTA glows and energetic 3D emissive accents. Unchanged. |
| Aqua light | `#72ddd4` | Enriched --aqua-light. Light aqua accent; also the wordmark fill color (logo cls-3). Bright highlight. Unchanged. |
| Teal-mid | `#409b9b` | Enriched --teal-mid. Mid teal used in dark charts and aurora/ambient washes. Unchanged. |
| Logo deep teal | `#066c80` | Logo mark inner petals (cls-1). Its own artwork hex, not the UI --primary token before or after the rework — do not substitute either primary when reproducing the logo. |
| Teal ramp 50 | `#eef7f8` | --teal-50 lightest tint (subtle fills, hero gradient end). Ramp is unchanged by the rework. |
| Teal ramp 100 | `#d3e9ec` | --teal-100. |
| Teal ramp 200 | `#abd4d8` | --teal-200 — this is the retired dark primary, now correctly *just* a ramp step. Fine to use as a ramp step (a light hover tint, a border); do not reach for it as --primary. |
| Teal ramp 300 | `#7fbcc2` | --teal-300. Ghost easter-egg glow color, if still wanted — no longer the dark chart-1 (see Chart series below). |
| Teal ramp 400 | `#4c9aa3` | --teal-400. |
| Teal ramp 500 | `#1c8791` | --teal-500. |
| Teal ramp 600 | `#0f717a` | --teal-600 — this is the retired light primary, now correctly *just* a ramp step (a hover/pressed shade near primary). Fine to use as a ramp step; do not reach for it as --primary. |
| Teal ramp 700 | `#0d5f67` | --teal-700. |
| Teal ramp 800 | `#0e4a52` | --teal-800 (deep fills/borders). |
| Success (light) | `#0f5132` | --success light. Deep green, not a saturated Tailwind green — healthy/confirmed status. |
| Success (dark) | `#34d399` | --success dark. |
| Warning (light) | `#855500` | --warning light. Deep amber, not Tailwind's #d97706. |
| Warning (dark) | `#fbbf24` | --warning dark. Unchanged from the old table. |
| Info (light) | `#0284c7` | --info light. Unchanged from the old table. |
| Info (dark) | `#38bdf8` | --info dark. Unchanged from the old table. |
| Chart series (light) | `#ff3f00` `#0a6358` `#066c80` `#ffb900` `#00a9a7` | --chart-1..5 light. chart-1 aliases --heat (below) rather than sitting in the teal family, so series 1 is a deliberate warm break against an otherwise teal-and-aqua set; chart-4 (amber) and chart-5 (aqua) are unchanged from the old table. |
| Chart series (dark) | `#c5e8dc` `#409b9b` `#079d99` `#70a6af` `#066c80` | --chart-1..5 dark. chart-1 stays sage (equals --primary dark) even though light chart-1 is warm — heat itself never flips with theme. chart-2/3/4 are unchanged from the old table. |
| Heat — rare accent | `#ff3f00` | --heat, theme-stable (does not flip between light/dark). Voice-orb / one-CTA-per-view territory in the wider CI product line — reserved, not a general accent. Not in the old table at all; listed because it's now where light chart-1 comes from. |
| Sidebar-primary (light) | `#0a6358` | --sidebar-primary now equals --primary in light mode. The old table's "#0f172b navy, do not fix to teal" guidance is gone — that was true of the old shadcn-default sidebar scheme, not of current canon. |
| Sidebar-primary (dark) | `#c5e8dc` | --sidebar-primary dark — equals --primary dark (sage), not the retired #abd4d8. |
| Gradient — brand (start) | `#0a6358` | --gradient-brand = linear-gradient(135deg, #0a6358, #00a9a7). Primary CTA/emissive gradient (phthalo to aqua) — same aqua endpoint as before, only the start moved with primary. |
| Gradient — hero (start) | `#f3faf7` | --gradient-hero = linear-gradient(135deg, #f3faf7, #abd4d8). Soft light hero wash — mist to teal-200 ramp step (ramp didn't move, so only the start hex changed to match the new light background). |
| Marketing sage (chimera layer) | `#759582` | Marketing-only 'liquid glass' layer: sage-green border #759582, gold reflection #d3b973, CTA gradient #125e5b→#186b56→#00a9a7. Off-canon website texture, NOT core UI tokens — marketing/immersive surfaces only. Unaffected by the primary rework; left as the old table had it. |

**Motifs:** Deep teal-navy #041620 shell with panels (#0c323c card / #143e48 popover) and dark accent fills (#0a2e38) — the signature CI 'dark, calm, technical-authority' surface; ideal base for a 3D HUD and world tint.; Phthalo→aqua brand gradient linear-gradient(135deg, #0a6358, #00a9a7) for primary CTAs and emissive/glow accents; hero wash linear-gradient(135deg, #f3faf7, #abd4d8) for light surfaces.; Aurora radial wash behind hero/HUD content: radial-gradient(120% 90% at 50% -10%, rgba(64,155,155,.28), transparent 60%) in dark; subtle teal top-lift on the shell (radial from 50% -20%) — this wash's own rgba values are unaffected by the primary rework.; Mint focus/selection glow (#58ebbf, this project's own accent, not a canon token — see the Mint row above) and phthalo-tinted elevation shadows (light: rgba(10,99,88,.05–.08); dark: black) — soft, low, never heavy drop shadows.; Liquid-glass panels (marketing/spatial): 8px radius, backdrop blur 18–32px, translucent white fill, sage-green border rgba(117,149,130,.22), faint gold 135deg reflection sheen — glassmorphism for immersive/3D surfaces, unaffected by the primary rework.; Ambient technical backdrops: 24px grid overlays, thin horizontal 'beam' light lines, cyberpunk comparison matrices in mono caps — a restrained sci-fi 'ghost in the shell' texture (there is even a literal rare drifting-ghost easter egg).; Terminal/code motif: near-black #031017 window with red/amber/green traffic dots, mono type, aqua-mint body text #c5f7f2, blinking cursor — for developer-empowerment surfaces, unaffected by the primary rework.; Rounded geometry system: base radius 0.625rem/10px, scale 6/8/10/14/20px, pills rounded-full; controls default rounded-md/lg. Marketing uses a tighter 8px radius.; Data viz in dark mode (chart series #c5e8dc/#409b9b/#079d99/#70a6af/#066c80 — sage-and-teal, not the old all-#7fbcc2-family monochrome) so graphs recolor with the theme.; lucide-react iconography only, size-4 (16px) default / size-5 prominent, currentColor inherit — consistent thin-line icon language; do not mix icon sets.

**Brand voice — tone:** CI speaks like someone who already left the cloud and wants to tell you exactly why: specific, slightly impatient with surveillance capitalism, genuinely excited about owning your own stack. In-product this reads as plain, confident, and never corporate — the interface should sound like it belongs to YOU, because the machine does. Peer-to-peer, plainspoken, grounded; empowering, never fearful; earns every claim with a number instead of an adjective.

- **Do:** Use sentence case everywhere, including buttons and headings. · Title Case ONLY product proper nouns: Companion Core, Companion Hub, Companion Memory, OpenClaw / Hermes. · Verb-first action labels: 'Install app', 'Uncloud now', 'Save to Memory'. · Write errors as a calm fact plus a concrete fix: 'Your Core is offline. Check the cable, then retry.' · Keep success/confirm grounded: 'Saved to your Memory.' (not 'Awesome!'). · Empty states warm and inviting: 'Nothing here yet. Your Companion is ready when you are.' · Use commas for light pauses and semicolons for independent clauses. · Say 'your Core' / 'your data', 'local-first', 'uncloud', 'no rate limits', 'sign in'.
- **Don't:** NEVER use em dashes or en dashes in user-facing copy (commas/semicolons instead). Hard rule; applies to HUD and web-player strings too. · No banned buzzwords: 'seamless', 'leverage', 'best-in-class', 'revolutionary', 'AI-powered solution' (corporate SaaS framing is the opposite of CI positioning). · No press-release voice ('We are proud to announce...') or empty hype ('Revolutionary AI changes everything'). · No fearmongering ('You could be at risk') — empower, not frighten. · Don't genericize product names: not 'the server/box/device/NAS' for Companion Core, not 'app store/marketplace' for Companion Hub, not 'chatbot/assistant' for OpenClaw/Hermes. · No Title Case Buttons; no unsupported superlatives. · Don't say 'log in'/'login' as a verb (use 'sign in'), 'on-premise' (use 'local-first'), 'unlimited' (use 'no rate limits').

---

## Appendix C — Diorama Prop Catalog

The 12 staged props (in `scene/extracted/`, all now UE-import-ready FBX + textures). A quarter-century of computing history to dress the set.

| Prop file | Machine | Year | Role in the set | Scale notes |
|---|---|---|---|---|
| C:\devel\llm-metahuman\scene\ibm-1401-mainframe-date-1959.zip | IBM 1401 Data Processing System (1959) — the full mainframe, business computer of the transistor era | 1959 | The anchor 'big iron' of the room. In a 1970s lab the 1401 reads as the legacy business machine still humming in the corner — payroll, punch-card batch jobs. It defines the deep background wall and gives the whole set its institutional-computer authority. | Reads as a room-spanning system. The 1401 Processing Unit cabinet is ~152 cm (60 in) tall, ~1.5 m wide, ~0.75 m deep — chest-to-shoulder height on a 1.75 m human. As a 'mainframe' group it should occupy a 2.5–3 m stretch of wall with the reader/punch alongside, clearly taller and bulkier than any person. |
| C:\devel\llm-metahuman\scene\ibm-1401-central-processing-unit.zip | IBM 1401 Processing Unit — the CPU cabinet from that same system | 1959 | The 'brain' cabinet, placed as the centerpiece of the 1401 group. Panel lights and the operator's console give a believable focal point a character could stand beside or gesture toward. Pairs directly with the full-mainframe asset. | Single floor cabinet ~152 cm (60 in) high, ~1.5 m long, ~0.75 m deep — a person standing beside it clears it by only a head. Front console face should sit at adult chest height (~1.2 m) so lights read on camera. |
| C:\devel\llm-metahuman\scene\ibm-2401.zip | IBM 2401 Magnetic Tape Unit (System/360 era, from 1964) | 1964 | The single most cinematic prop: floor-standing tape drives with two exposed reels behind a glass door. Placed in a row they create the classic 'spinning reels' backdrop that instantly reads as 'mainframe computer' to any viewer. Ideal motion/life in an otherwise static set. | Human-height floor units, ~150–170 cm (5–5.5 ft) tall, ~75 cm wide, ~75 cm deep each — tops just below eye level on a standing adult. Deploy 2–4 in a gently curved or straight row; reels centered at ~1.3 m so they sit at head height in frame. |
| C:\devel\llm-metahuman\scene\ibm-3350-madrid.zip | IBM 3350 Direct Access Storage (disk storage, 1975) | 1975 | Mid-70s DASD — the 'washing machine' disk banks that ground the set firmly in the diorama's target decade. Sits mid-room as bulk storage between the older 1401 iron and the operator terminals, signalling an installation that grew over 15 years. | Large floor cabinets ~1.75 m (69 in) tall, ~0.8 m wide, ~1.0+ m deep — taller than the tape drives and roughly at a standing person's head/eye line. Best staged as a linked row of 2–3 units to convey a storage 'string.' |
| C:\devel\llm-metahuman\scene\ibm-ramac-305-first-hard-drive-1956.zip | IBM 305 RAMAC / IBM 350 Disk Storage — the first commercial hard drive (1956) | 1956 | A museum-grade 'origin of storage' piece. In-fiction it's the venerable ancestor kept running or semi-retired in the lab; visually it lets the diorama tell a storage-evolution story when paired with the 1975 IBM 3350 (first hard drive vs. mature DASD). | Refrigerator-scale: the 350 disk unit is ~1.7 m (67 in) tall, ~1.5 m wide, ~0.75 m deep and houses fifty 24-inch platters — imposing, clearly taller than the standing character. Give it breathing room as a standalone landmark rather than crowding it into a row. |
| C:\devel\llm-metahuman\scene\plugboard-ancient-coding.zip | Wiring plugboard / control panel (ENIAC- and IBM 402 / unit-record-style patch programming) | 1940s–1950s | Detail/story prop representing pre-stored-program 'coding by wire.' Perfect as a wall-mounted framed artifact or resting on the hero desk beside the character — a tactile close-up object that rewards the fixed camera and reinforces the 'history of computing' theme. | Handheld-to-small-panel scale, roughly 40–55 cm across, a few cm thick — fits under an arm or in two hands. Use as foreground/tabletop set dressing or hung at eye level (~1.5 m to center); never a floor object. |
| C:\devel\llm-metahuman\scene\ibm-3178.zip | IBM 3178 Display Station (green-screen 3270-family terminal, ~1983; reads as late-70s 3277/3278 lineage) | ~1979 | The working operator interface — a monochrome green CRT terminal on a desk, the natural device the character 'uses' to talk to the mainframes. Multiple units can line an operator's desk run to imply a staffed data-processing shift. | Desktop CRT + detached keyboard. Display head ~35–40 cm tall, ~40 cm wide, ~45 cm deep, sitting on a desk so the screen center lands at seated eye height (~1.1–1.2 m off the floor). Keyboard is a separate low-profile slab in front. |
| C:\devel\llm-metahuman\scene\commodore-pet.zip | Commodore PET 2001 (1977) — all-in-one personal computer | 1977 | The microcomputer-era intruder: a personal machine that dates the lab to the late 70s and contrasts the room-sized IBM iron with the coming desktop revolution. Good on a side desk as the 'new toy' the character or a researcher tinkers with. | Self-contained desktop unit: ~40 cm wide, ~35–43 cm tall (monitor + integrated chiclet keyboard + cassette deck), ~45 cm deep. Sits fully on a desktop; footprint smaller than a modern all-in-one PC. |
| C:\devel\llm-metahuman\scene\cp-500-prologica-trs-80-model-iii-clone.zip | Prológica CP-500 — Brazilian TRS-80 Model III clone (~1982), Z80/CP-M | ~1982 | A second all-in-one micro that adds international/period texture (Brazilian reserva-de-mercado clone). Functionally interchangeable with the PET as a desktop workstation micro; pair or alternate with the PET so the two micro desks don't read as identical. | Integrated monitor-keyboard-dual-floppy console like the TRS-80 Model III: ~55 cm wide, ~45 cm tall, ~45 cm deep — a hefty desktop unit but still a tabletop object, larger and boxier than the PET. |
| C:\devel\llm-metahuman\scene\kenbak-1-first-personal-computer.zip | KENBAK-1 (1971) — widely credited as the first personal computer | 1971 | A precious small artifact: switches-and-lights front panel, no CPU chip, programmed by hand. Belongs on the hero desk or a display shelf as a conversation piece — the 'first PC' that predates the PET by six years, tying the personal-computing thread back into the early 70s. | Small tabletop box, ~48 cm (19 in) wide, ~28 cm deep, ~11–14 cm tall — a wide, shallow slab you could carry in two hands. Low profile; place near the front of a desk so the toggle switches and lamp row catch the light. |
| C:\devel\llm-metahuman\scene\xerox-alto-son-of-douglas-engelbart-1973.zip | Xerox Alto (1973) — the first GUI/bitmap-display workstation with a mouse | 1973 | The visionary 'future' machine and the strongest candidate for the HERO desk the character works at: portrait bitmap display, keyboard, and 3-button mouse up top, with the processor cabinet tucked below/beside the desk. Perfectly bridges to the Engelbart mouse story. | Split unit. The distinctive portrait CRT (page-shaped, ~portrait A4 proportion) plus keyboard sits on a desk at normal seated-work height; the processor cabinet is a mini-fridge/file-cabinet-sized box roughly 1.0–1.1 m tall, ~0.5 m wide, placed under or immediately beside the desk. Mouse is palm-sized on the desktop. |
| C:\devel\llm-metahuman\scene\first-mouse-douglas-engelbart-date1963.zip | Engelbart's first mouse (1963) — the original hand-carved wooden prototype | 1963 | The tiniest, most narratively loaded prop. A hero close-up object for the fixed camera — displayed on a stand, under a small vitrine, or resting on the Alto desk to pay off the 'father and son of interaction' pairing with the Xerox Alto. | Palm-sized wooden shell, roughly 10–12 cm long, ~6 cm wide, ~6 cm tall, with two perpendicular metal wheels underneath and a single button. Must be staged as a raised/foreground detail (on a plinth ~0.9–1.0 m up) or it will vanish at set scale. |

**Layout concept:** Concept: a real 20-year-strata data center read left-to-right as a timeline the character lives inside. THREE ZONES on a roughly rectangular floor with the fixed camera looking across the room's long axis.  (1) BACK WALL — 'Big Iron' / mainframe zone (deep background, oldest tech). Along the rear wall place the IBM 1401 mainframe group: the 1401 Processing Unit CPU as the lit centerpiece flanked by the full-mainframe cabinetry, spanning ~3 m. To its left, the IBM RAMAC 305 stands alone as a landmark 'first hard drive.' This wall is tall, dense, and slightly shadowed — the set's mass and horizon line, all clearly taller than a person.  (2) MID-ROOM — storage & motion. A row of 2–4 IBM 2401 tape drives runs perpendicular-ish to camera on one side (spinning reels catch the light and give the only motion), balanced across an aisle by a linked string of 2–3 IBM 3350 disk units. These human-height cabinets form a 'canyon' the character can be framed walking through, creating depth layers between camera and back wall. Keep a clear central AISLE of negative space (~1.5–2 m) on the floor here so sightlines from camera to the character stay open and the room can breathe.  (3) FOREGROUND / camera-side — the human zone. The HERO DESK sits front-and-slightly-off-center, angled ~30 deg to camera so the character is three-quarter-lit and never has their back fully to us. The Xerox Alto is the hero workstation (portrait display + keyboard on the desk, processor cabinet tucked beneath), with Engelbart's 1963 mouse elevated on a small plinth/vitrine right beside it — the interaction-history payoff at close-up scale. On the same desk or a shelf edge sits the KENBAK-1 and the wiring plugboard as tactile 'origin' artifacts. A secondary operator desk carries one or two IBM 3178 green-screen terminals (the character's live link to the mainframes), and a third side desk holds the micro pair — Commodore PET and Prológica CP-500 — as the 'coming revolution.'  Sightlines & negative space: the character at the hero desk is the convergence point — tape reels and mainframe lights recede behind them as leading lines. Reserve the central aisle and a pool of floor in front of the hero desk as deliberate negative space so the fixed camera reads a clear subject against layered machine depth rather than visual clutter. Small props (mouse, KENBAK-1, plugboard) always live raised on desks/plinths in the foreground third; nothing precious on the floor. Story vector across the room: 1956 RAMAC and 1959 1401 at the back, 1964–75 tape/disk in the middle, 1971–82 personal/interactive machines up front by the human — the camera literally looks back through time toward the character working at its leading edge.

**Mood:** A working late-70s institutional data center at the tail of a night shift — lived-in, not a museum. LIGHTING: cool overhead fluorescent wash (slightly green-cyan, with a faint flicker/hum implied) as the base, punched by warm practicals — the amber/red panel lamps of the 1401 console and KENBAK-1 toggles, the phosphor-GREEN glow of the IBM 3178 and Xerox Alto displays throwing soft color onto the character's face and the desk. Motivate a key light from the terminal screens so the human zone is warmest and brightest; let the mainframe back wall fall into cooler shadow for depth. A single hard raking light across the tape-drive row makes the spinning reels and cabinet seams read.  MATERIALS & PALETTE: period-correct 'computer beige,' oatmeal, and IBM off-white/pale-grey sheet metal; charcoal and deep IBM blue accent panels; brushed aluminum trim; smoked-acrylic tape-door glass; walnut/wood-grain desk laminate; raised anti-static floor tiles (0.6 m grid) with visible seams and the occasional lifted panel showing cabling beneath. Cabling is a hero texture — thick gray under-floor looms, colored patch wires on the plugboard, coiled terminal cords.  ERA DETAIL & PATINA: fine dust settling on cabinet tops and horizontal ledges; faint fingerprint smudge and static-cling dust bloom on CRT glass; slightly yellowed plastics on the older machines (1401, RAMAC) versus cleaner late-70s micros; worn keycap lettering and a shiny-worn spacebar on the hero keyboard; scuffed floor tiles along the main aisle where feet traffic; a fabric-and-metal operator stool, a stack of green-bar fanfold printout and a punch-card tray as set dressing; a coffee mug ring on the hero desk; small warning/ID plates and blinking status LEDs. Air of quiet machine hum and warmth — the character is a solitary presence tending a room of humming history. Overall tone: nostalgic, slightly melancholic, technically reverent; every surface says these machines have run for years.

---

## Appendix D — CI Ecosystem Integration Map

| Service | Role | Interface | This app's integration point |
|---|---|---|---|
| CI-Hub | Self-hosted app platform on the user's own machine: one-click Docker app installs from the CI marketplace, automatic Traefik reverse-proxy (per-app subdomain + TLS), Cloudflare Tunnel, Headscale/Tailscale VPN, and — most relevant — it hosts Ollama and does model routing, plus injects an aggregated MCP tool endpoint (HUB_URL / HUB_MCP_API_KEY) into installed apps. | OpenAI-compatible HTTP (/v1/chat/completions with SSE, Bearer token) for inference; separately a Streamable-HTTP MCP endpoint for tool access. | Phase-1 model backend. FMetaHumanLLMEnvConfig maps CI_HUB_URL -> OpenAI base URL (implies BackendType=OpenAICompatible), CI_HUB_TOKEN -> OpenAIApiKey, CI_HUB_MODEL -> OpenAIModel, MHLLM_BACKEND=openai forces UOpenAIChatClient over UOllamaClient. Already wired in Deployment/docker-compose.yml project service. Hub-injected HUB_URL/HUB_MCP_API_KEY are the future seam for MCP tool-calling (not yet consumed by the UE app). |
| CI-Portal | Cloud control plane / OIDC identity provider: unified sign-in (passkey, email-password, OIDC), device registry, marketplace distribution, unified app launcher. It is the org OIDC IdP (issuer https://hub.companionintelligence.com/api/auth); CI-Common @companionintelligence/auth treats Portal as IdP with PKCE. | Standard OIDC / OAuth2 (discovery doc + authorization-code flow, PKCE), consumed at the edge not in app code. | Gates the Pixel Streaming player page via oauth2-proxy (provider=oidc, skip_provider_button) on :8095 (NOT :8080, taken on core-2). oidc-proxy service injects OAUTH2_PROXY_CLIENT_ID/CLIENT_SECRET/OIDC_ISSUER_URL/REDIRECT_URL(:8095/oauth2/callback)/COOKIE_SECRET from .env. The UE app carries no auth code. Already built (Phase 1). Hardening left: cookie_secure=true over HTTPS, tighten email_domains=["*"] to an org/group claim. |
| CI-Server (Companion Memory) | The persistent digital-twin / personal-memory backend the avatar should read from and write to. NestJS monorepo (GraphQL + REST + MCP) on port 8642, with an agent-facing /memory/* API designed for embodied agents. | HTTP REST with x-api-key header (ApiKeyGuard) on /memory/* routes, plus a Streamable-HTTP MCP server at /mcp (identity companion-memory 0.1.0) that auto-wraps the GraphQL schema as tools. Key routes: GET /memory/context (L0 profile markdown for the system prompt, {block}); POST /memory/recall (L1 hybrid keyword/semantic search, {query,maxResults?}); POST /memory/write (raw fragment, 201); POST /memory/turn (ingest a conversation turn, {sessionId,platform,messages[]}, dedup by SHA-256 UUIDv8). | Phase-2 work, NOT yet built. Add a UCIServerMemoryClient in the CICompanion plugin plus two new FMetaHumanLLMEnvConfig vars: CI_SERVER_URL (http://ci-server:8642 or host.docker.internal:8642) and CI_SERVER_TOKEN (x-api-key). AMetaHumanCompanionCharacter calls /memory/context at session start, /memory/recall before each model call, /memory/turn after each exchange — mirroring CI-OpenClaw afterTurn / CI-Hermes sync_turn so the avatar shares one memory brain. When fronted by the Hub, CI-Server also supports HubForwardAuthGuard (HMAC X-CI-Hub-User) as an alternative to a static token. |
| CI-Hermes | Web-terminal + dashboard edition of the Nous Research Hermes agent, packaged for the CI marketplace and pre-wired to the Companion Memory provider. An observability/agent-console surface over the same memory. | Web terminal (chat + CLI) and dashboard; connects to CI-Server at http://host.docker.internal:8642 with a Companion Memory API key (same /memory/* + sync_turn contract). | No direct code coupling to the UE app; a sibling consumer of the same CI-Server memory the avatar writes to. Phase-3: a Hermes dashboard tile/terminal to monitor avatar sessions/turns/memory writes, and the reference implementation for the memory-turn write pattern the avatar should copy. Runs on core-2 alongside ci-memory/ci-openclaw. |
| CI-OpenClaw | A containerized build of OpenClaw (an autonomous browser/agent runtime) hardened for the CI marketplace — API-key setup UI, headless-browser sandbox, gateway-token management, Homebrew tool install. Per its README it is an agent runtime, not infrastructure the avatar depends on: a peer autonomous agent sharing CI memory and Hub tools. | Runs behind the CI app proxy; bundles a companionintelligence memory plugin routing to CI-Server via CI_SERVER_URL + CI_SERVER_TOKEN, and gets Hub MCP tool access via a native mcp.servers.ci-hub entry (Streamable HTTP) from injected HUB_URL / HUB_MCP_API_KEY. | Phase-3. Shared substrate rather than an API the UE app calls: (1) both consume CI-Server /memory/* identically, so an OpenClaw task becomes context the avatar can recall; (2) both reach tools through the same Hub MCP endpoint. A future avatar-driven action could be delegated to an OpenClaw run and surfaced back through shared memory. |
| CI-Import-Tools | Bulk personal-data importer: ingests archives (Google Takeout, Meta, messages, photos, location, contacts) into a local SQLite timeline, syncs to CI-Server, then triggers enrichment (people extraction, linking, geocoding, embeddings). Wizard + REST + MCP on port 12002. | Its own REST/MCP; downstream it POSTs CI-Server's bulk-ingest plane (/v1/events 1–1000/req, /v1/communications\|contents\|inventory\|browser-activity\|positions via NDJSON or {records} envelope, then /v1/ingest/finish + poll /v1/ingest/status). | Phase-3, no direct UE coupling. It populates the digital twin the avatar recalls from — what makes /memory/recall actually rich. Relevant only as the data-loading front of the same CI-Server. |
| comfy (ComfyUI) | ComfyUI image/asset-generation app packaged for CI-OS-Hub (app id comfyui, port 8188, AI/Development). | ComfyUI HTTP API / web UI on :8188, installed as a Hub Docker app. | Phase-3, content-pipeline only — no runtime path into the UE app. Potential use: generating textures, backdrops, scene/diorama art (e.g. the 1970s datacenter level) and avatar-adjacent imagery, invoked out-of-band, not during a conversation turn. |
| CI-Common | Shared-systems monorepo (design tokens, UI kit, contracts, auth, config, fonts). Not a runtime service the avatar calls, but the canonical source for cross-repo contracts and the auth model. | npm packages: @companionintelligence/auth (Portal-as-OIDC-IdP, PKCE, token verification), @companionintelligence/contracts (Zod source-of-truth, JSON Schema emitted for non-TS repos), @companionintelligence/tokens (phthalo #0a6358 / Manrope design system). | Reference/consistency only for a C++/UE app. The auth package confirms the Portal OIDC model the oauth2-proxy config already follows; contracts (JSON Schema) is where CaptureEvent / memory-turn shapes are defined — the schema a future UCIServerMemoryClient should serialize against. Any web companion UI/launcher card would adopt its tokens for brand parity. |

**End-to-end data flow:** End-to-end turn for the embodied MetaHuman avatar (Pixel Streaming, headless UE on core-2):  1. AUTH/EDGE (once per viewer): browser -> oauth2-proxy :8095 (CI-Portal OIDC, generic oidc provider) -> on success proxies the player page and the signalling WebSocket to the internal signalling server (:8090 player, :8888 streamer). The UE app has no auth code; identity is passed upstream via pass_access_token/set_xauthrequest.  2. SESSION SEED (once): AMetaHumanCompanionCharacter calls GET {CI_SERVER_URL:8642}/memory/context (x-api-key = CI_SERVER_TOKEN) -> {block} markdown injected into the system prompt alongside MHLLM_SYSTEM_PROMPT. [Phase 2, new client.]  3. SPEECH IN: user mic audio -> UWhisperSpeechToText posts to WHISPER_SERVER_URL (whisper.cpp server) -> transcript text.  4. MEMORY RETRIEVAL: POST /memory/recall {query: transcript, maxResults} (x-api-key) -> hybrid keyword/semantic hits with entity hydration, folded into the prompt as retrieved context. [Phase 2.]  5. INFERENCE: UOpenAIChatClient POSTs /v1/chat/completions (SSE) to CI_HUB_URL with Bearer CI_HUB_TOKEN and model CI_HUB_MODEL — CI-Hub's OpenAI-compatible Ollama. Tokens stream back. (Phase 0 alternative: UOllamaClient -> OLLAMA_BASE_URL /api/chat.)  6. SPEECH OUT + LIP SYNC: response text -> UPiperTextToSpeech posts to PIPER_SERVER_URL -> WAV -> FWavAudioReader + FVisemeEnvelope compute RMS->jawOpen; CICompanion fires OnSetVisemeWeights / OnSetEmotion to drive the MetaHuman face; audio+video encode (VP8 software, or VAAPI H.264 on core-2's Radeon 8060S) and stream over WebRTC (coturn :3478 relay) back to the authenticated browser.  7. MEMORY WRITE-BACK: POST /memory/turn {sessionId, platform:\"metahuman\", messages:[user,assistant]} (dedup by SHA-256 UUIDv8) persists the exchange into the digital twin — the same afterTurn/sync_turn pattern CI-OpenClaw and CI-Hermes use, so all three share one memory brain. Background /memory/compact consolidates over time. [Phase 2.]  Steps 1, 3, 5, 6 exist today; steps 2, 4, 7 (the CI-Server memory calls) are the net-new Phase-2 work.

**Phasing:** Phase 0 — Local Ollama (baseline, done): MHLLM_BACKEND=ollama, UOllamaClient -> OLLAMA_BASE_URL /api/chat; Whisper + Piper local; no auth, no persistent memory. The single packaged build stays endpoint-agnostic because FMetaHumanLLMEnvConfig applies all overrides at startup — no rebaking.  Phase 1 — Hub + Portal (BUILT, live on core-2 via UE 5.5): flip MHLLM_BACKEND=openai and point CI_HUB_URL/CI_HUB_TOKEN/CI_HUB_MODEL at CI-Hub's OpenAI-compatible Ollama so model choice/routing is centralized; front the whole Pixel Streaming surface with oauth2-proxy against CI-Portal OIDC (:8095, CI_PORTAL_CLIENT_ID/SECRET/ISSUER_URL/REDIRECT_URL). Both already wired in Deployment/docker-compose.yml. Remaining hardening: cookie_secure=true over HTTPS, tighten email_domains from \"*\" to an org/group claim, and confirm 5.5-signalling <-> 5.8-streamer interop or build the 5.8 signalling image.  Phase 2 — Memory / digital-twin persistence (NEXT, not started): add a UCIServerMemoryClient in the CICompanion plugin plus two new env vars (CI_SERVER_URL :8642, CI_SERVER_TOKEN x-api-key). Wire GET /memory/context at session start, POST /memory/recall before each inference, POST /memory/turn after each exchange — copying CI-OpenClaw afterTurn / CI-Hermes sync_turn verbatim. Add EnvConfigSpec + client unit tests. This turns the avatar from stateless into the embodied face of the same Companion Memory the rest of the ecosystem shares. Optionally adopt HubForwardAuth (HMAC X-CI-Hub-User) instead of a static token when running behind the Hub.  Phase 3 — Ecosystem breadth (later): (a) consume the Hub MCP endpoint (HUB_URL/HUB_MCP_API_KEY) and/or CI-Server's /mcp Streamable-HTTP tool surface so the avatar can call tools / delegate tasks (potentially to a CI-OpenClaw run) and reflect results through shared memory; (b) a CI-Hermes dashboard/terminal tile to observe avatar sessions and memory writes; (c) CI-Import-Tools as the data-loading front that makes /memory/recall rich; (d) comfy (ComfyUI :8188) as an out-of-band asset/texture/scene generator for the avatar and diorama level; (e) align any companion web UI/launcher card to CI-Common design tokens and serialize memory payloads against CI-Common contracts (Zod/JSON Schema).
