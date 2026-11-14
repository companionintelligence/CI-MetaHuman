# Credits

This repository declares no third-party package dependencies.

Companion Intelligence is nonetheless built throughout on open-source work —
see the sibling repositories' `CREDITS.md` for the projects the platform
depends on, and [NOTICE.md](NOTICE.md) for anything vendored here.

<!-- hand-authored: do not regenerate this section -->
## 3D assets in the machine-room set

`Deployment/author_diorama.py` builds the 1979 machine room from eight prop GLBs, placed 14 times
as `Deployment/stage_layout.json` lists them and loaded by file name from `MHLLM_GLB_DIR` (see
[Docs/SCENE-AUTHORING.md](Docs/SCENE-AUTHORING.md)): three CI-authored files (`ibm-1401-cpu.glb`
once, `ibm-3350-disk.glb` four times as one disk string, `ibm-3178-terminal.glb` four times as IBM
3277 display stations) and the five CC BY 4.0 models in the table below. The GLBs
are not committed to this repository, but they are cooked into the packaged app and shown in the
streamed scene. Any build that ships them must carry the attributions below, and every package
does: this file, `NOTICE.md` and `LICENSE` are staged in `MetaHumanLocalLLM/` beside its paks. The
stream page links the same attributions ([Deployment/pixelstreaming/credits.html](Deployment/pixelstreaming/credits.html),
served next to the player). Change that page with this table: CI fails if they differ.

### Sketchfab models used under CC BY 4.0

Each model below is © its author and licensed under the
[Creative Commons Attribution 4.0 International licence (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
Titles, authors and licences were read from the public Sketchfab API
(`https://api.sketchfab.com/v3/models/<uid>`) on 2026-11-12. None of these pages carries
Sketchfab's NoAI tag, and none of the models has `noai` among its tags in the API.

| Set file | Title | Author | Source |
|---|---|---|---|
| `engelbart-mouse.glb` | "First Mouse - Douglas Engelbart - Date:1963" | [museudocomputador](https://sketchfab.com/museudocomputador) (Museu do Computador) | <https://sketchfab.com/3d-models/first-mouse-douglas-engelbart-date1963-8f4a2b1d7fb6420191a77042bb275d15> |
| `ibm-ramac-305.glb` | "IBM RAMAC 305 - FIRST HARD DRIVE - 1956" | [museudocomputador](https://sketchfab.com/museudocomputador) | <https://sketchfab.com/3d-models/ibm-ramac-305-first-hard-drive-1956-03f81005695944cca90ab1aecc11975a> |
| `kenbak-1.glb` | "Kenbak-1 // First Personal Computer" | [museudocomputador](https://sketchfab.com/museudocomputador) | <https://sketchfab.com/3d-models/kenbak-1-first-personal-computer-bd931bcaf2094846a7708a5106ffd294> |
| `plugboard.glb` | "Plugboard - "Ancient" Coding" | [museudocomputador](https://sketchfab.com/museudocomputador) | <https://sketchfab.com/3d-models/plugboard-ancient-coding-b483e91712804a7bb387969c6d054870> |
| `xerox-alto.glb` | "Xerox Alto - Son of Douglas Engelbart - 1973" | [museudocomputador](https://sketchfab.com/museudocomputador) | <https://sketchfab.com/3d-models/xerox-alto-son-of-douglas-engelbart-1973-fc49263ee9b94ea98caa11c65e663a90> |

**Modifications.** These are adaptations, not the original files. Each was downloaded from
Sketchfab and re-exported as GLB with Blender's glTF exporter, which also strips Sketchfab's
embedded author and licence metadata. Several were rescaled, recentred or merged in that step.
`author_diorama.py` then scales each model uniformly, re-parents its materials onto a project
material that keeps base colour, metallic and roughness but drops emission and specular, and
places and lights it in the scene. For the stage it also edits three of them before import, as
`Deployment/stage_layout.json` asks (`clipLocal`, `rotateLocal`): it removes the RAMAC 305's loose
disk platter; it removes the Kenbak-1's power cord, stands the model upright (the file lies face
down) and merges its 79 parts into one mesh; and it merges the mouse's 16 parts into one mesh.

**How the files were matched to these pages.** Each local GLB has exactly the triangle count
that the Sketchfab API reports for its model. Texture counts match, and material counts are the
same or one lower, except for the merged PET. Each file also looks the same as the Sketchfab
thumbnail in a side-by-side render. `plugboard.glb` had no recorded source. It matches
"Plugboard - "Ancient" Coding" exactly (4,160 triangles, 5 materials, 2 textures), and its
texture names are Portuguese like the rest of the Museu do Computador set.

**In the prop set, not placed.** Three more CC BY 4.0 models from the same museum account are in
the licence-clean prop directory, but the stage does not place them, so no build ships them:
"Commodore PET" (`commodore-pet.glb`, which carries one textured material where the original has
seven and renders with that texture scrambled; a true-size stand-in takes its place),
"IBM 1401 - MAINFRAME - Date: 1959" by Breno Valli (`ibm-1401-mainframe.glb`, unassembled) and
"CP-500 - Prologica - TRS 80 Model III Clone" (`prologica-cp500.glb`, made in 1982, after the room's
date). A layout that places one of them must add it to the table above, NOTICE.md and credits.html.

Model names describe historical machines. IBM, Commodore, Xerox, Prológica and Kenbak are
trademarks of their respective owners, and their use here implies no endorsement.

### Props authored by Companion Intelligence

These props are original works of LifeScope INC, DBA Companion Intelligence, and are owned by
CI. They are generated by scripts in this repository and contain no third-party geometry or
textures. The scripts cite the published dimensions that each model follows. Because they import
Blender's Python API, the generator scripts are licensed GPL-3.0-or-later (see
[NOTICE.md](NOTICE.md)); the GPL does not extend to the GLBs they produce.

| Set file | Machine | Generator |
|---|---|---|
| `ibm-729-tape.glb` | IBM 729 magnetic tape unit (cabinet plus two reels) | `Deployment/blender/make_ibm729.py` |
| `ibm-1401-cpu.glb` | IBM 1401 Processing Unit, 4-cube | `Deployment/blender/make_period_props.py` |
| `ibm-3178-terminal.glb` | IBM 3277 Display Station Model 2 (the slot keeps its old file name) | `Deployment/blender/make_period_props.py` |
| `ibm-3350-disk.glb` | IBM 3350 dual-drive disk storage unit | `Deployment/blender/make_period_props.py` |

The stage places the 1401 processing unit once and the 3350 and the 3277 four times each. It does not
place `ibm-729-tape.glb`: the six 729s in the room are true-size stand-ins built from primitives,
which are the spec for that model's re-author ([Docs/STAGE-DESIGN.md](Docs/STAGE-DESIGN.md)).

The other set dressing is CI-generated as well: `Deployment/audio/machine_room.wav` and
`Deployment/generated/S_RoomTone.wav` come from `Scripts/MakeAmbience.py`, and
`Deployment/generated/T_CRT_Vector.png` comes from `Scripts/MakeCRTTexture.py`.

### Replaced models (do not ship)

These Sketchfab models used to fill set slots. They are CC BY 4.0 by
[maxdragon](https://sketchfab.com/maxdragon), but each page carries Sketchfab's NoAI tag
(`<meta name="robots" content="index, noAI">`), and the API lists `noai` among each model's tags.
Sketchfab's Terms of Use prohibit using
NoAI-tagged content "in datasets for, in the development of, or as inputs to generative AI
programs" ([Sketchfab Help Center](https://help.sketchfab.com/en/articles/16152133)). For an AI
companion, that was treated as a licence issue, and CI-authored props replaced the models.

| Former set file | Sketchfab model | Status |
|---|---|---|
| `ibm-1401-cpu.glb` | ["IBM 1401 Central Processing Unit"](https://sketchfab.com/3d-models/ibm-1401-central-processing-unit-62a34be60e0548bf938b52989f86136d) | replaced by `make_period_props.py` |
| `ibm-3178-terminal.glb` | ["IBM 3178"](https://sketchfab.com/3d-models/ibm-3178-891ae8149d8547a5971381237d13e2e6) | replaced by `make_period_props.py` |
| `ibm-3350-disk.glb` | ["IBM 3350 "Madrid""](https://sketchfab.com/3d-models/ibm-3350-madrid-6d1b6c37642a44388c06992e0923f990) | replaced by `make_period_props.py` |
| `ibm-2401-tape.glb` | ["IBM 2401"](https://sketchfab.com/3d-models/ibm-2401-61f8ce78c76b46dfade5ccc7ad020a1b) | the set no longer loads it (the 729 replaced it); delete it from prop directories |

`Deployment/author_diorama.py` enforces this. It reads the glTF `asset.copyright` of
`ibm-1401-cpu.glb`, `ibm-3178-terminal.glb` and `ibm-3350-disk.glb` before importing them and
accepts a file only if that field names Companion Intelligence, which `make_period_props.py`
writes into every replacement. Any other file in those slots, such as a stale maxdragon download,
is never imported: the authoring result logs `prop REFUSED …` and the slot stays empty.

`Assets/store-screenshots/01-scene.png` and `02-conversation.png` were captured before the
replacement. They show maxdragon's "IBM 1401 Central Processing Unit" in the background,
alongside the CI-authored 729s. Until they are recaptured from a build with the CI-authored
props, wherever they are published they need the credit "IBM 1401 Central Processing Unit" by
maxdragon, CC BY 4.0, linked to the model page above. Both screenshots also show Nia, an opt-in
resident whose licence does not clear this use (next section).

## The resident avatar

Eliza, the resident, was made by Companion Intelligence. Her current version was built with MPFB 2
(MakeHuman Community) from CC0 1.0 assets: the MakeHuman system assets (the MakeHuman team),
Faceunits 01 and Visemes 02 (Mika Suominen), and the fisherman sweater, wool slacks, flats and
curled-under bob by Margaret Toigo, recoloured and fitted to her. Her idle is Epic's MM_Idle,
retarget-adapted. CC0 needs no credit; it is given as a courtesy. A MetaHuman version is planned under
the same name: it is to be created in MetaHuman Creator in Unreal Engine 5.8 from Epic-provided
presets and grooms (Unreal Engine EULA), wearing clothing adapted from the same CC0 items, and this
credit will name it once it exists. She is authored with `-Resident Eliza` (`MHLLM_RESIDENT=eliza`);
her content is private and not in this repository.

By default the companion appears as **Epic's UE mannequin** (`SKM_Manny` with its `MM_Idle`
loop, from the engine's MoverTests plugin): Unreal Engine content, licensed with the engine under
the Unreal Engine EULA. It is a faceless placeholder.

Two third-party characters can still be authored, but only when asked for by name, because both
are Fab content whose listings read "Allows usage with AI: No":

- "Character Creator: Nia" by [Reallusion Inc](https://www.fab.com/sellers/Reallusion%20Inc), from
  Fab (<https://www.fab.com/listings/366cfc84-6d6a-44ff-89ad-4bffb7347c15>), with Reallusion's own
  materials, shaders and idle motions (`-Resident Nia`, `MHLLM_RESIDENT=nia`). Her listing's
  description also ends "*Non-Commercial Use".
- "Seo - Editable MetaHuman Character" by Groomchenko, from Fab
  (<https://www.fab.com/listings/3693be83-9054-4fd3-9b2e-0b6d5b45fe22>), which is what
  `mhc_seo.mhpkg` is (`-Resident MetaHuman`, `MHLLM_RESIDENT=metahuman`).

Neither one's content is in this repository, and a default build neither stages nor cooks it
([THIRD_PARTY.md](THIRD_PARTY.md)). Nia is shown in the store screenshots above and in
`Docs/verification-2026-08-17/measurement-regions.png`, which were made before the mannequin
became the default. [NOTICE.md](NOTICE.md) sets out why neither licence clears this use and what
would: a MetaHuman created in-house, or written terms from the seller.

## Unreal Engine

The app is built with Epic Games' Unreal Engine. Section 7(a) of the
[Unreal Engine EULA](https://www.unrealengine.com/eula/unreal) requires a product that has credits to
place these two notices in them, with the current year:

CI Metahuman uses Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Unreal® Engine, Copyright 1998 – 2026, Epic Games, Inc. All rights reserved.

The stream's credits page carries the same two lines, and CI fails unless both carry them word for
word. Change the year each January. "CI Metahuman" is the product's name as this repository's README
gives it; if the product is renamed, change the name in both places.

## Software and fonts

The Unreal editor tooling in `Plugins/VibeUE` and `Plugins/VibeMetaHumans` is by Kevin Buckley /
Buckley Builds LLC ([VibeUE](https://github.com/kevinpbuckley/VibeUE), MIT). The interface font is
[Montserrat](https://github.com/JulietaUla/Montserrat) by the Montserrat Project Authors (SIL Open
Font License 1.1), which the signalling image also serves to the stream page. The streaming page builds on Epic Games'
[Pixel Streaming Infrastructure](https://github.com/EpicGames/PixelStreamingInfrastructure) (MIT).
The CI-authored props are built with [Blender](https://www.blender.org) (GPL-3.0-or-later).
[THIRD_PARTY.md](THIRD_PARTY.md) lists everything vendored here or pulled in at build time, and
[NOTICE.md](NOTICE.md) carries the notices those licences require.
<!-- end hand-authored -->

---

_Generated by [`scripts/generate-attribution.mjs`](https://github.com/companionintelligence/CI-Engineering/blob/main/scripts/generate-attribution.mjs) for `CI-MetaHuman`. Regenerate rather than editing generated sections by hand._
