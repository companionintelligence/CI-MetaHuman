# NOTICE

This file records attribution that the licences of our dependencies and
upstream projects legally require us to reproduce. It is not a full dependency
list — see [THIRD_PARTY.md](THIRD_PARTY.md) for that, and [CREDITS.md](CREDITS.md)
for acknowledgements.

<!-- hand-authored: do not regenerate this section -->
## Copyleft components and the source-offer duty

**The repository itself** contains two copyleft or reciprocal-licence components.

- **Four Blender scripts, GPL-3.0-or-later.** `Deployment/blender/make_period_props.py`,
  `Deployment/blender/make_ibm729.py`, `Deployment/blend_fix_mats.py` and
  `Deployment/ue_prop_fbx.py` run inside Blender and import its Python API (`bpy`, with `bmesh`
  and `mathutils`). Blender is GPL-3.0-or-later (`license/license.md` in the Blender
  installation), and the `bpy` package carries `SPDX-License-Identifier: GPL-2.0-or-later`. The
  FSF holds that a program which reaches GPL-covered facilities through an interpreter's
  bindings is linked to them and must be released in a GPL-compatible way
  ([GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html#IfInterpreterIsGPL)). Blender's
  [FAQ](https://www.blender.org/support/faq/) and [licence page](https://www.blender.org/about/license/)
  apply this to scripts: published scripts that use the Blender Python API must be shared under
  the GPL. The Blender manual requires GPL-3.0-or-later for add-ons
  ([Extension Licenses](https://docs.blender.org/manual/en/latest/advanced/extensions/licenses.html)).
  PolyForm Noncommercial is not GPL-compatible. These four files are therefore licensed under the
  GNU General Public License v3.0 or later, not PolyForm Noncommercial, as their
  `SPDX-License-Identifier` headers state. The full text is in
  [`LICENSES/GPL-3.0-or-later.txt`](LICENSES/GPL-3.0-or-later.txt). Anyone may use, change and
  redistribute these files under the GPL, commercially or not. They are source code, so
  publishing them here already provides their source. Anyone who redistributes them must keep
  their notices and pass the GPL on. The packaged app and the runtime images do not contain them.
  The GPL does not extend to the GLBs the scripts write. GPL-3.0 section 2 covers the output of a
  covered work only when the output is itself a covered work, and the Blender manual states that
  the GPL does not apply to artwork made with Blender
  ([About Free Software and the GPL](https://docs.blender.org/manual/en/latest/getting_started/about/license.html)).
  The props remain CI-authored works ([CREDITS.md](CREDITS.md)).
- **Montserrat, SIL Open Font License 1.1**, in `Content/Fonts`, and a copy of
  `Montserrat-SemiBold.ttf` in `Deployment/pixelstreaming/fonts/`, which the signalling image
  serves to the player page instead of Google Fonts. Modified versions of the fonts must stay
  under the OFL, and each copy of the fonts must carry their copyright notice and licence, so ship
  `Content/Fonts/OFL-Montserrat.txt` with any packaged build; the signalling image carries
  `Deployment/pixelstreaming/fonts/OFL-Montserrat.txt` next to the font it serves. Using the fonts
  in software is unrestricted.

No other file in the repository is under a GPL-family licence.

**Images built from `Deployment/kokoro-align` contain copyleft software:**

- **espeak-ng, GPL-3.0-or-later**, twice: once as the Debian `espeak-ng` package, and once as
  the compiled `libespeak-ng.so` and `espeak-ng-data` that the `espeakng-loader` wheel bundles.
  The wheel does not carry espeak-ng's licence text.
- **phonemizer-fork, GPL-3.0-or-later**, installed as Python source.
- **num2words, LGPL-2.1**, installed as Python source.
- The GPL and LGPL packages of the Debian base image. The same holds for every image built
  from the Debian- and Ubuntu-based Dockerfiles in `Deployment/`.

Building these images and running them yourself creates no obligation. Neither does offering
the speech sidecar as a network service, because GPL-3.0 and LGPL-2.1 have no network clause.
**The duty arises when you convey an image or its binaries to anyone else**, for example by
pushing it to a public registry, shipping it to a customer, or handing over an image archive.
GPL-3.0 section 6 then requires you to provide the Corresponding Source of the GPL components,
under the GPL, in one of the ways it allows:

- ship the source together with the image;
- include a written offer, valid for at least three years, to supply the source; or
- for network distribution, offer equivalent access to the source through the same place. The
  source may sit on another server, including a third party's such as Debian's source archive,
  if clear directions next to the image say where it is. You remain responsible for keeping it
  available for as long as you distribute the image.

Pass on the licence texts and copyright notices as well. The Debian packages carry theirs in
`/usr/share/doc/*/copyright`, but for the espeakng-loader wheel you must add espeak-ng's
`COPYING`. num2words ships as source, which already provides the source that LGPL-2.1 requires;
its licence text must still go with it. To meet the duty for a published image, pin the base
image digest and package versions, record `dpkg -l` and `pip freeze` for the image, and archive
or publish the matching Debian source packages (`apt-get source espeak-ng`) and the source
distributions of phonemizer-fork, num2words and espeakng-loader next to the image.

**Before publishing the kokoro-align image, get counsel's view on one more point.**
`Deployment/kokoro-align/app.py` is first-party code under PolyForm Noncommercial. At run time it
loads the GPL components: misaki's espeak fallback calls phonemizer-fork, which loads
libespeak-ng. The FSF holds that a program which imports a GPL library forms a combined work.
Distributed together, that work would have to be under GPL-compatible terms, and PolyForm
Noncommercial is not GPL-compatible. The compose files build this image locally
(`ci/kokoro-align:local`) and do not publish it, so nothing is conveyed today.

## Vendored third-party code

### VibeUE and VibeMetaHumans (MIT)

`Plugins/VibeUE` and `Plugins/VibeMetaHumans` (upstream: <https://github.com/kevinpbuckley/VibeUE>):

```
MIT License

Copyright (c) 2025 Kevin Buckley / Buckley Builds LLC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Both plugins also carry this text in their own `LICENSE` files.

### Epic Games Pixel Streaming Infrastructure (MIT)

`Deployment/pixelstreaming/player.html` is Epic's stock Pixel Streaming player page with one
added script, and with its Google Fonts links replaced by a self-hosted Montserrat.
`Deployment/Dockerfile.signalling` builds Epic's Pixel Streaming Infrastructure from source, and
`Deployment/patch_pixelstreaming_player.py` patches its player page the same two ways. Upstream:
<https://github.com/EpicGames/PixelStreamingInfrastructure>.

```
Copyright Epic Games, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

This MIT grant covers the Pixel Streaming Infrastructure project only. The Pixel Streaming
plugin inside Unreal Engine, and Epic's container images, are under the Unreal Engine EULA (see
below).

### Montserrat (SIL Open Font License 1.1)

`Content/Fonts/Montserrat-Light.ttf`, `-Regular.ttf` and `-SemiBold.ttf`, the Unreal font
assets imported from them, and the byte-identical copy of `Montserrat-SemiBold.ttf` in
`Deployment/pixelstreaming/fonts/` (with its own copy of the licence) that the signalling image
serves to the player page: Copyright 2024 The Montserrat.Git Project Authors
(<https://github.com/JulietaUla/Montserrat.git>). The fonts are licensed under the SIL Open Font
License, Version 1.1, whose full text is in
[`Content/Fonts/OFL-Montserrat.txt`](Content/Fonts/OFL-Montserrat.txt) and at
<https://openfontlicense.org>.

## 3D models under CC BY 4.0

The packaged app's machine-room set contains eight models from Sketchfab, each © its author and
licensed under CC BY 4.0 (<https://creativecommons.org/licenses/by/4.0/>). All eight were
modified: re-exported to GLB, rescaled, given project materials, and placed and lit in the scene.
[CREDITS.md](CREDITS.md) has the full attribution and the modification details.

- "Commodore PET" by museudocomputador: <https://sketchfab.com/3d-models/commodore-pet-fb1e5a04ef4644f98219e1d5d52ab073>
- "First Mouse - Douglas Engelbart - Date:1963" by museudocomputador: <https://sketchfab.com/3d-models/first-mouse-douglas-engelbart-date1963-8f4a2b1d7fb6420191a77042bb275d15>
- "IBM 1401 - MAINFRAME - Date: 1959" by Breno Valli, Museu do Computador collection (museudocomputador): <https://sketchfab.com/3d-models/ibm-1401-mainframe-date-1959-0d5ead7d38c84a5c916b214c50ca0a77>
- "IBM RAMAC 305 - FIRST HARD DRIVE - 1956" by museudocomputador: <https://sketchfab.com/3d-models/ibm-ramac-305-first-hard-drive-1956-03f81005695944cca90ab1aecc11975a>
- "Kenbak-1 // First Personal Computer" by museudocomputador: <https://sketchfab.com/3d-models/kenbak-1-first-personal-computer-bd931bcaf2094846a7708a5106ffd294>
- "Plugboard - "Ancient" Coding" by museudocomputador: <https://sketchfab.com/3d-models/plugboard-ancient-coding-b483e91712804a7bb387969c6d054870>
- "CP-500 - Prologica - TRS 80 Model III Clone" by museudocomputador: <https://sketchfab.com/3d-models/cp-500-prologica-trs-80-model-iii-clone-63ac1d69330b4d0a9629b62b30a14c9b>
- "Xerox Alto - Son of Douglas Engelbart - 1973" by museudocomputador: <https://sketchfab.com/3d-models/xerox-alto-son-of-douglas-engelbart-1973-fc49263ee9b94ea98caa11c65e663a90>

CC BY 4.0 defines "Share" to include public display and communication (Section 1), so a public
stream of the set shares these models. Section 3(a)(2) allows the attribution "in any reasonable
manner based on the medium", so it must be reachable from the product as well as from this
repository, for example through a credits link on the streaming page.

## Epic Games Unreal Engine, MetaHuman and Fab content (not covered)

The PolyForm Noncommercial License 1.0.0 applies to Companion Intelligence's own code and content
only, apart from the four GPL-3.0-or-later Blender scripts listed above. It does **not** cover
Epic Games' Unreal Engine, Unreal Engine source or tools, MetaHuman characters, or any other Epic
Games or Fab/Marketplace assets used or referenced by this project.
Those remain under the Epic Games End User License Agreement and the applicable Unreal Engine,
MetaHuman and Fab terms, and you must obtain and comply with them yourself. Nothing here grants
any right in Epic's or any third party's materials.

This repository no longer carries MetaHuman or outfit source assets. `Content/MetaHumans/DefaultCompanion`,
`Content/TestMetaHumanCharacter` and the KhaosMDF492 outfit (`Content/KhaosMDF492/`,
`Content/OA_KhaosMDF492`, `Content/WI_KhaosMDF492`) were removed from the tree in November 2026.
Nothing in the project referenced them, and a public repository should not redistribute them as
uncooked `.uasset` files. **Earlier commits still contain them in Git LFS.** Removing them from
the published history would need a history rewrite and an LFS purge, which is the owner's call.
Do not commit MetaHuman, Fab or other Epic source assets to this repository; keep them in
private storage.

## The resident avatar: Epic's mannequin by default; "Nia" and "Seo" are opt-in only

The companion's on-screen character is chosen when the map is authored: `MHLLM_RESIDENT` for
`Deployment/author_diorama.py` and `Deployment/core2/run_author_m2.sh`, `-Resident` for
`Scripts/BuildNative.ps1`. The default is the only choice whose licence clears an AI companion.
Facts below were read on 2026-11-12; the root `README.md` ("Licensing") quotes the sources.

- **Default: Epic's UE mannequin.** `SKM_Manny` and its `MM_Idle` loop, from the engine's
  MoverTests plugin. They are Epic-provided content installed with Unreal Engine, so they are
  Licensed Technology under the Unreal Engine EULA (section 1), and a cooked build or runtime image
  carries them in object code as section 4 requires. The current EULA has no AI clause (EULA
  update 22, October 30, 2026, removed section 6(e)'s restrictions). Nothing outside the engine is
  staged for it. It is a placeholder: it has no facial rig, so there is no lip-sync or expression.
- **Opt-in: "Character Creator: Nia" by Reallusion Inc** (`-Resident Nia`, `MHLLM_RESIDENT=nia`),
  free on Fab under its Standard License, in the Personal and Professional tiers:
  <https://www.fab.com/listings/366cfc84-6d6a-44ff-89ad-4bffb7347c15>.
  - **No AI use.** The listing page shows "Allows usage with AI: No", and Fab's listing API
    (`https://www.fab.com/i/listings/366cfc84-6d6a-44ff-89ad-4bffb7347c15`) returns
    `"isAiForbidden": true`. Fab does not document that label on its own; read it as Fab's "NoAI"
    marking. Epic's Fab documentation says a NoAI asset "must not be used for generative AI data
    collection"
    ([Licenses and Pricing in Fab](https://dev.epicgames.com/documentation/en-us/fab/licenses-and-pricing-in-fab)).
    As Fab staff quoted the Fab EULA (section 16(l)) on 2026-10-13, NoAI content may not be used
    in datasets used by generative AI programs, in the development of generative AI programs, or
    as training inputs to them. That thread also notes the Fab Terms of Service (section 3.3(i))
    say "inputs" without "training"
    (<https://forums.unrealengine.com/t/noai-content-inputs-or-training-inputs/2838630>).
  - **A non-commercial note.** The listing description ends "*Non-Commercial Use", yet Fab's
    summary of the Standard License it is offered under says buyers may use assets commercially
    (<https://www.fab.com/eula>). The listing contradicts itself, and a commercial release falls
    inside that conflict.
  - **How she enters a build, and only when asked for.** Her content is not in this repository or
    its history: `/Content/CC_ControlRig_Nia/` is in `.gitignore`, and
    `Docs/reference/nia-morph-targets.txt` holds only the names of her morph targets. Only
    `-Resident Nia` makes `Scripts/BuildNative.ps1` copy about 900 MB of it into
    `Content/CC_ControlRig_Nia`, and only `MHLLM_RESIDENT=nia` makes `run_author_m2.sh` mount
    `/home/ci/nia-content`. `author_diorama.py` does not even load `SK_Nia` for another resident,
    and `Deployment/core2/m2_clean.sh` refuses a mannequin or MetaHuman cook tree that still
    contains her (`UNEXPECTED_NIA_CONTENT`).
- **Opt-in: the MetaHuman "Seo"** (`-Resident MetaHuman`, `MHLLM_RESIDENT=metahuman`).
  `mhc_seo.mhpkg` is the Fab listing "Seo - Editable MetaHuman Character" by Groomchenko
  (<https://www.fab.com/listings/3693be83-9054-4fd3-9b2e-0b6d5b45fe22>): Standard License, and
  "Allows usage with AI: No" (`isAiForbidden: true`). Epic's MetaHuman terms do not override the
  seller's: the Unreal Engine EULA (section 1) says assets downloaded from an Epic-operated
  marketplace, Fab included, are not Licensed Technology. Seo therefore stands exactly where Nia
  does. Her FBX export is not in this repository; it is read from `MHLLM_MH_DIR` /
  `-MetaHumanDir` only for this resident (and as Nia's fall-back), and a mannequin build creates
  nothing under `/Game/Seo`.

This product is an AI companion whose generated replies drive the resident's face. Whether that
counts as developing, or giving inputs to, a generative AI program is unclear, and an unclear
licence is treated as a licence problem. Neither Fab character is staged, loaded or cooked unless
asked for by name, and building or distributing with either is a decision for counsel, not for the
build script. The three NoAI-tagged Sketchfab props were replaced on the same ground
([CREDITS.md](CREDITS.md)).

**Still open:**

1. **A resident with a face** that the project may use with AI: a MetaHuman created in-house with
   MetaHuman Creator (Licensed Technology, so the EULA governs it), or written terms from
   Reallusion or Groomchenko that cover use as the avatar of an AI companion (for Nia, commercial
   use too), recorded here.
2. **Images made with Nia.** `Assets/store-screenshots/01-scene.png`, `02-conversation.png` and
   `Docs/verification-2026-08-17/measurement-regions.png` show her, and the two screenshots also
   show a replaced NoAI prop ([CREDITS.md](CREDITS.md)). Recapture them from a mannequin build, or
   get counsel's view, before they appear in a store listing or other commercial material.
3. **What is on record.** Earlier versions of `README.md` and `Docs/ROADMAP.md` said the owner had
   stated that licensing is handled. No licence, permission or correspondence from Reallusion or
   Groomchenko is recorded in this repository, so this file cannot confirm it.

Do not use either character's mesh, rig or renders as data for training any model.
<!-- end hand-authored -->

## Copyleft and reciprocal-licence dependencies

The package manifests declare none: this repository has no package manifest. The repository and
the images built from it still contain copyleft and reciprocal-licence components. They are
listed above, under "Copyleft components and the source-offer duty", together with the duty to
provide source.

## First-party code

Except as noted above, the contents of this repository are
Copyright © LifeScope INC, DBA Companion Intelligence (<https://ci.computer>),
licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE).

Required Notice: Copyright LifeScope INC, DBA Companion Intelligence (https://ci.computer)

---

_Generated by [`scripts/generate-attribution.mjs`](https://github.com/companionintelligence/CI-Engineering/blob/main/scripts/generate-attribution.mjs) for `CI-MetaHuman`. Regenerate rather than editing generated sections by hand._
