# Third-party dependencies

This repository declares no third-party package dependencies — it has no
`package.json`, `Cargo.toml`, `requirements.txt`, `pyproject.toml` or
`pubspec.yaml` with dependency entries.

Anything this repository vendors, bundles or derives from is recorded in
[NOTICE.md](NOTICE.md) instead.

<!-- hand-authored: do not regenerate this section -->
## Not in a package manifest, but still third-party

The generated paragraph above covers package manifests only. The repository still vendors
third-party code and fonts, and its Dockerfiles pull in more at build time. Licences were
checked against each project's `LICENSE` file or its registry metadata on 2026-11-12. The
notices those licences require are in [NOTICE.md](NOTICE.md).

### Vendored in this repository

| Component | Path | Version | Licence | Copyright |
|---|---|---|---|---|
| VibeUE | `Plugins/VibeUE` | 5.0 | MIT ([`LICENSE`](Plugins/VibeUE/LICENSE)) | Copyright (c) 2025 Kevin Buckley / Buckley Builds LLC |
| VibeMetaHumans | `Plugins/VibeMetaHumans` | 0.1 | MIT ([`LICENSE`](Plugins/VibeMetaHumans/LICENSE)) | Copyright (c) 2025 Kevin Buckley / Buckley Builds LLC |
| Montserrat Light, Regular, SemiBold (`.ttf` and the imported `.uasset` fonts) | `Content/Fonts` | — | SIL Open Font License 1.1 ([`OFL-Montserrat.txt`](Content/Fonts/OFL-Montserrat.txt)) | Copyright 2024 The Montserrat.Git Project Authors |
| Pixel Streaming player page (Epic's stock page plus one inline script) | `Deployment/pixelstreaming/player.html` | from `pixel-streaming-signalling-server:5.5` | MIT | Copyright Epic Games, Inc. |

Upstream projects: [VibeUE](https://github.com/kevinpbuckley/VibeUE) (VibeMetaHumans is its
sibling, by the same author), [Montserrat](https://github.com/JulietaUla/Montserrat) and
[Pixel Streaming Infrastructure](https://github.com/EpicGames/PixelStreamingInfrastructure).

### Epic Games: Unreal Engine, MetaHuman and Pixel Streaming

- **Unreal Engine** (5.5 and 5.8), including the engine's Pixel Streaming and MetaHuman plugins,
  is licensed under the Unreal Engine EULA, not open source. `Deployment/Dockerfile*` build
  from Epic's container images (`ghcr.io/epicgames/unreal-engine:dev-*`,
  `:runtime-pixel-streaming`). Epic's container documentation states that "Distribution of
  development images is governed by the terms of the Unreal Engine EULA", and that runtime
  images, which contain no editor or build tools, are less restricted. Do not publish an image
  that contains the editor or engine tools. That rules out the `dev-*` images, the builder
  stages, and the image from `Dockerfile.editorpreview`, which copies the whole engine in.
  MetaHuman characters are under Epic's MetaHuman licence terms.
- **Pixel Streaming Infrastructure** (signalling server, web server and TypeScript frontend).
  `Deployment/Dockerfile.signalling` builds it from public source at a pinned commit
  (`PIXEL_STREAMING_INFRASTRUCTURE_REF`), and `Deployment/patch_pixelstreaming_player.py`
  patches its player page. The project is MIT-licensed, Copyright Epic Games, Inc. The image
  also contains the project's npm dependencies, each under its own licence, on a `node:lts`
  (Debian) base.

### kokoro-align sidecar image (`Deployment/kokoro-align`)

| Component | Installed by | Licence |
|---|---|---|
| **espeak-ng** | `apt-get` (Debian package) | **GPL-3.0-or-later** |
| **espeakng-loader** | `pip` | MIT for the loader, but its wheel **bundles a compiled `libespeak-ng.so` and `espeak-ng-data`, which are GPL-3.0-or-later** |
| **phonemizer-fork** | `pip` | **GPL-3.0-or-later** |
| **num2words** | `pip` | **LGPL-2.1** |
| misaki | `pip` | Apache-2.0 |
| onnxruntime | `pip` | MIT |
| numpy | `pip` | BSD-3-Clause (bundled parts: 0BSD, MIT, Zlib, CC0-1.0) |
| huggingface_hub | `pip` | Apache-2.0 |
| spacy | `pip` | MIT |
| fastapi | `pip` | MIT |
| uvicorn | `pip` | BSD-3-Clause |
| pydantic | `pip` | MIT |
| Python 3.12 on Debian (`python:3.12-slim`) | base image | PSF-2.0 for CPython; Debian packages under their own licences, many GPL or LGPL |
| Kokoro-82M ONNX weights (`onnx-community/Kokoro-82M-v1.0-ONNX-timestamped`) | downloaded from Hugging Face on the first synthesis request into a volume (`kokoro-align-models`), not stored in the image | Apache-2.0 |

The bold rows are copyleft. The obligations that come with distributing this image are set out
in [NOTICE.md](NOTICE.md).

### Other images and sources the deployment uses

| Component | Where | Licence |
|---|---|---|
| coturn 4.6.2 | `coturn/coturn` image, `Deployment/docker-compose.yml` | BSD-3-Clause |
| OAuth2 Proxy 7.6.0 | `quay.io/oauth2-proxy/oauth2-proxy` image, `Deployment/docker-compose.yml` | MIT |
| Mesa and libdrm (built from source) | `Deployment/Dockerfile.mesagit` | MIT for the core libraries; other components carry their own licences (see Mesa's licence documentation) |
| Ubuntu 26.04 base | `Deployment/Dockerfile.amd`, `Dockerfile.editorpreview`, `Dockerfile.mesagit` | Ubuntu packages under their own licences, many GPL or LGPL |

### Build tools that set licence terms

| Tool | Used for | Licence | Effect on this repository |
|---|---|---|---|
| Blender 5.1 | runs `Deployment/blender/make_period_props.py`, `Deployment/blender/make_ibm729.py`, `Deployment/blend_fix_mats.py` and `Deployment/ue_prop_fbx.py` on a workstation to make the prop GLBs; not shipped | GPL-3.0-or-later | The four scripts import Blender's Python API, so they are GPL-3.0-or-later rather than PolyForm Noncommercial. See [NOTICE.md](NOTICE.md) |

### 3D assets

The machine-room props are listed with their licences and attributions in
[CREDITS.md](CREDITS.md): eight Sketchfab models under CC BY 4.0 and four CI-authored props.
<!-- end hand-authored -->

---

_Generated by [`scripts/generate-attribution.mjs`](https://github.com/companionintelligence/CI-Engineering/blob/main/scripts/generate-attribution.mjs) for `CI-MetaHuman`. Regenerate rather than editing generated sections by hand._
