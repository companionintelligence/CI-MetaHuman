# Plan — multiple avatars

> Written 2026-07-31. Covers what avatars are actually available, and how to go from one hard-coded
> resident to N configurable ones. Grounded in what this codebase already does, not a greenfield
> design.

---

## 1. Inventory — what we can actually cast

Checked, not assumed: every `.mhpkg` manifest was opened and read.

| Asset | Type | Grooms | Clothing | Verdict |
|---|---|---|---|---|
| `MHC_Seo` (0.15 GB) | Editable MetaHuman | ✗ | ✗ | Converted; renders flat grey without a hand-built shader |
| `MHC_Crowd80` (0.43 GB) | Editable MetaHuman | ✗ | ✗ | Unconverted |
| `MHC_Crowd84` (0.43 GB) | Editable MetaHuman | ✗ | ✗ | Unconverted |
| `khaosmdf492` | Outfit (`OA_`/`WI_`) | — | — | Wardrobe, not a person |
| **Nia** (Character Creator) | CC character | ✓ | ✓ | **In use as Eliza.** Real PBR, hair, dress, motions |

**"Crowd80/84" are single characters, not crowds** — the name comes from a MetaHuman Creator preset
set. So the real count is three MetaHumans, all bald and naked, plus one outfit.

### The casting recommendation

**Do not convert more MetaHumans for this product.** Three findings, each paid for:

1. **The bake is neutral grey.** `T_Head_BC` is (94,94,94) by construction — MetaHuman expects its
   own skin shader to tint it. Without that shader a plain texture sample is grey, which is why Seo
   could only ever be flat constant colour. Rebuilding that shader is a project, not a task.
2. **No grooms, no clothing.** Every MetaHuman here needs hair and a garment sourced separately and
   fitted. Nia shipped with both.
3. **Assemble + export needs the interactive 5.8 editor.** It deadlocks headless (the game thread
   waits on async render work that needs the game thread to tick), so every new MetaHuman costs
   manual GUI time and cannot be automated.

Character Creator assets have none of these problems: Nia's 23 authored PBR slots loaded straight
into the 5.5 cook with no FBX round-trip. **Cast additional avatars from CC/Fab, not MetaHuman.**

The MetaHumans stay useful as *fallbacks* and as ARKit-profile test subjects — the `FFaceProfile`
split only earns its keep if something actually exercises the ARKit path.

## 2. What already generalises

The architecture is closer to multi-avatar than it looks, because the rig-specific parts were
pushed behind seams early:

| Component | Multi-avatar readiness |
|---|---|
| `EViseme` + `FFaceProfile` | ✅ Already vendor-neutral. CC and ARKit profiles ship today. |
| `UCompanionFaceComponent` | ✅ Per-actor component. Nothing static; N instances work as-is. |
| `FVisemePlanner` | ✅ Pure functions. No per-character state. |
| Kokoro voice | ✅ Voice is a **per-request** field, so per-avatar voices cost nothing. |
| `UCompanionFaceSubsystem` | ⚠️ Finds **one** resident by tag, holds one `Face`. |
| `UCompanionRuntimeSubsystem` | ⚠️ Spawns **one** brain, one TTS, one HUD. |
| `UCompanionStageDirector` | ⚠️ One subject, one settle pose. |
| Config | ❌ Env vars are singular (`MHLLM_SYSTEM_PROMPT`, `KOKORO_VOICE`). |

So the work is mostly **pluralising three subsystems and replacing scalar env config with a
registry** — not rewriting the stack.

## 3. Design

### 3.1 The registry

One JSON file, read at `BeginPlay`, staged like the compose:

```json
{
  "avatars": [
    { "id": "eliza",  "tag": "CompanionFace",
      "faceProfile": "CC", "voice": "af_sky",
      "persona": "You are Eliza, the resident intelligence...",
      "spawn": { "pos": [330, 20, 0], "yaw": 90 },
      "openingLine": "Welcome to Companion Intelligence, I'm Eliza. How can I help you?" },
    { "id": "ada", "tag": "CompanionFace_Ada",
      "faceProfile": "CC", "voice": "af_bella",
      "persona": "You are Ada, the systems engineer...",
      "spawn": { "pos": [330, -180, 0], "yaw": 70 } }
  ],
  "primary": "eliza"
}
```

JSON rather than more env vars: per-avatar config is a *list of records*, and the env-var namespace
already has 20 keys for one character. It also means adding an avatar is a data change, not a
redeploy of new variables.

**The `tag` is the join.** The map is authored headlessly and cannot reference plugin C++, so actor
tags remain the only identifier that survives the cook (labels are editor-only — learned the hard
way). `author_diorama.py` stamps one tag per avatar; the registry maps tag → behaviour.

### 3.2 Subsystem changes

**`UCompanionFaceSubsystem`** — `TMap<FName /*tag*/, UCompanionFaceComponent*>` instead of a single
`Face`. Keep `GetFace()` returning the primary so existing callers don't break; add
`GetFace(FName Tag)`. The morph-target-resolution logging becomes per-avatar, which matters: a
second character with a different rig will silently no-op exactly like the first one did, and the
`16/16 curves resolved` line is the only thing that catches it.

**`UCompanionRuntimeSubsystem`** — one `AMetaHumanCompanionCharacter` *per avatar*, each with its
own `UOllamaConversation` (history is already per-character) and its own `UKokoroTextToSpeech`
constructed with that avatar's voice. Route `OnCompanionSpoke` through a small struct carrying the
speaker id so the right face moves.

> The trap: `OnCompanionSpoke` is a `DYNAMIC_MULTICAST` delegate with one `FString` param. Binding N
> avatars to one handler gives no way to tell who spoke. Either bind per-avatar lambdas via a
> `TMap<UObject*, FName>` reverse lookup, or add the speaker id to the delegate signature. The
> second is cleaner and touches one header.

**`UCompanionStageDirector`** — takes the subject from the registry's `primary` rather than
"whichever actor has the most morph targets". Later: cut to whoever is speaking.

### 3.3 Turn arbitration — who answers?

The genuinely new problem. Three options, cheapest first:

1. **Addressed-by-name** (recommended first). If the message begins with an avatar's name, route to
   them; otherwise the primary answers. Zero extra latency, completely predictable, and it teaches
   the user the mental model in one turn.
2. **Round-robin / explicit turn-taking.** Good for a scripted two-hander demo, useless for Q&A.
3. **Router model call.** A fast small model (`qwen3:8b`, 262 ms measured) picks the responder.
   Adds a round trip to *every* turn and can pick wrong, which is worse than a dumb rule that the
   user can predict.

Start with (1). It is a `StartsWith` check, and the failure mode is "the wrong one answers", not
"nobody does".

### 3.4 Audio with more than one speaker

Currently `SpawnSound2D` — non-spatialized, correct for a single character filling frame. With two
avatars it becomes wrong: both voices arrive dead centre and the viewer cannot tell who spoke
without watching mouths.

Switch to `SpawnSoundAttached` on the speaking avatar. Pixel Streaming captures the final submix,
so stereo placement survives to the browser *provided* the stream is not downmixed to mono — which
is **unverified**, like everything else about that audio path.

The ambience stays 2D and non-spatialized. It is a bed.

## 4. Phasing

| Phase | Work | Proves |
|---|---|---|
| **0. Prerequisite** | Verify audio reaches a browser at all | Everything below assumes it does |
| **1. Registry** | JSON + parser + `FAvatarConfig`; single avatar reads from it | No behaviour change — pure refactor, so a regression is obvious |
| **2. Pluralise faces** | `TMap<tag, face>`; author a second tagged actor | Two faces animate independently (self-test both) |
| **3. Pluralise brains** | Per-avatar character + conversation + voice; speaker id on the delegate | Two personas, two voices, right mouth moves |
| **4. Arbitration** | Name-addressed routing + HUD shows who is answering | A visitor can talk to either |
| **5. Staging** | Camera cuts to the speaker; non-speakers idle/attend | Reads as a scene, not two kiosks |

Phases 1–2 are safe refactors. Phase 3 is where the real complexity sits.

## 5. Costs and risks

**GPU.** A second CC character is ~2× skeletal mesh + morph updates. core-2's iGPU renders one
comfortably at 720p; two is untested and is the first thing to measure, because the render budget —
not the AI — is the likely ceiling.

**VRAM on beta-1.** Two conversations against one 27B model share weights; only KV cache grows. 48 GB
across two Navi 31 cards is ample. No second model needed unless personas want different ones.

**TTS serialisation.** One Kokoro instance synthesises sequentially. Two avatars replying at once
queue — fine, since they should not talk over each other anyway.

**The real risk is dramaturgical, not technical.** Two AI characters in shot invites the viewer to
watch them talk to *each other*, which is a different product from an assistant answering *you*.
Decide whether the second avatar is a participant or a colleague-in-background before building
Phase 5.

## 6. Recommendation

Do Phase 0 first — audio is still unproven and four phases here depend on it.

Then Phases 1–2, which are cheap and make the system honest about being multi-avatar even while one
character is cast.

Before Phase 3, **cast the second avatar from Character Creator**, not from `MHC_Crowd80/84`. The
grey-bake and no-clothing findings above mean a MetaHuman costs days of shader and wardrobe work to
reach where Nia started.

---

*See `Docs/GDD-WEB-EXPERIENCE.md` (the experience), `Docs/GAP-ANALYSIS.md` (what is real),
`metahuman-mhpkg-to-55.md` in memory (the conversion pipeline and its deadlocks).*
