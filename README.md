<h1 align="center">CI Metahuman — a locally-hosted embodied companion</h1>

<p align="center"><strong>A locally hosted embodied companion built with Unreal Engine.</strong></p>

[![release](https://img.shields.io/badge/release-not%20published-757575?style=flat-square)](https://github.com/companionintelligence/CI-MetaHuman/releases)
[![platform](https://img.shields.io/badge/platform-Windows%20%C2%B7%20Linux%20Pixel%20Streaming-0a6358?style=flat-square)](https://github.com/companionintelligence/CI-MetaHuman)
[![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-0a6358?style=flat-square)](LICENSE)
[![Discord](https://img.shields.io/badge/chat-Discord-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.com/invite/yQp9hwpzAa)

<p align="center"><a href="docs/README.md">Documentation</a> · <a href="https://docs.ci.computer/docs/reference/open-source-systems">Systems map</a></p>

<p align="center">
  <a href="#build-and-test-windows"><img src="https://img.shields.io/badge/Build%20and%20run-Build%20and%20run-0a6358?style=for-the-badge&logo=windows&logoColor=white" alt="Build and run" height="30"></a>
</p>


## How it fits together

Unreal C++ subsystems connect the chat backend to cognition, speech, emotion and face animation; the same authored project can run natively or stream from a Linux host.

```mermaid
flowchart LR
  Browser[Text or microphone input] --> Backend[Chat and cognition backend]
  Backend --> Voice[STT and TTS]
  Backend --> Emotion[Emotion and response state]
  Voice --> Face[Phoneme-timed facial animation]
  Emotion --> Face
  Face --> Unreal[Unreal avatar]
  Unreal --> Delivery[Windows app or Pixel Streaming]
```


## Features

| Feature | What it provides |
|---|---|
| **Conversation** | Use an interchangeable chat backend for local or OpenAI-compatible inference. |
| **Voice** | Connect speech recognition and synthesis to phoneme-timed lip-sync. |
| **Embodied behavior** | Coordinate cognition, emotion and facial animation in Unreal C++ subsystems. |
| **Delivery** | Run natively on Windows or stream a Linux container with Pixel Streaming. |

> [`Docs/README.md`](Docs/README.md). This file is the orientation; that one is the spine.
>
> Rewritten 2026-08-02. The previous version described an earlier design (UE 5.8, a MetaHuman
> `DefaultCompanion`, `Source/MetaHumanLocalLLM/`, amplitude lip-sync, an NVIDIA requirement) and
> had carried a "this is out of date" banner for some time. Everything below was checked against
> the source or a recorded measurement.

A photoreal avatar standing in a 1970s mainframe room, holding a conversation with a visitor.
**Every model runs on hardware we own; nothing leaves the tailnet.**

| | |
|---|---|
| **Engine** | UE **5.8** for editor authoring, MCP work and the current Pixel Streaming cook |
| **Avatar** | **MetaHuman "Seo"** by default (persona: Eliza), face driven through the `ARKit` profile. The Reallusion Character Creator "Nia" (**CC-named** `V_*` morphs, 286 morphs, all 15 CC visemes) is **opt-in only** (`-Resident Nia` / `MHLLM_RESIDENT=nia`) because of her licence — see [Licensing](#licensing) |
| **Cognition** | Ollama, thinking disabled — **424–914 ms** warm for `qwen3.8:27b` on the reference host |
| **Voice out** | Kokoro via AMD Lemonade, OpenAI-shaped HTTP — 1166 ms median, **audible in the browser, measured by energy** |
| **Voice in** | Whisper via the same endpoint — 66 ms median |
| **Lip-sync** | Phoneme-timed from Kokoro's own duration predictor, with two documented fallbacks |
| **Delivery** | Linux container → Pixel Streaming (WebRTC, software VP8) at `:8090`, **or** a native Windows executable from the same source |
| **Behaviour** | C++ `UWorldSubsystem`s. **No Blueprints anywhere** — and that constraint explains most of the architecture |

The deployment target is an **AMD/ROCm** host. It does **not** require an NVIDIA GPU.

## What she does

- **Talks and listens.** Type in the browser or click the mic; replies are spoken aloud with the
  mouth timed from the synthesizer's own phoneme durations rather than guessed from spelling.
- **Looks at you.** Ballistic saccades, ~70% eye contact while listening and ~40% while speaking,
  and an upward planning aversion while the model is thinking — so latency reads as thought.
- **Has a face.** Reply text drives a sustained brow/cheek/eye expression composited over the
  visemes, plus involuntary blinking.
- **Can remember you**, opt-in via `MHLLM_MEMORY=1`.
- **Runs against anything.** `IChatBackend` covers native Ollama `/api/chat` and any
  OpenAI-compatible `/v1/chat/completions` (LM Studio, Lemonade, vLLM, a gateway) by config alone.

## Layout

| Path | What |
|---|---|
| `Plugins/CICompanion/` | **All the companion code.** A reusable plugin — drop it into another UE project |
| `Plugins/CICompanion/Source/CICompanion/Private/Tests/` | The Automation Spec suite |
| `Source/MetaHumanLocalLLM/` | A near-empty host module. The code is *not* here |
| `Content/` | The diorama and the resident. Most of it is authored at build time by `Deployment/author_diorama.py`; the resident is chosen there (see [`Docs/SCENE-AUTHORING.md`](Docs/SCENE-AUTHORING.md#the-resident)) |
| `Deployment/` | Dockerfiles, compose files, the `kokoro-align` sidecar, browser harnesses, core-2 tunnel units |
| `Docs/` | See [`Docs/README.md`](Docs/README.md) for the index |
| `Website/` | Static explainer for [`metahuman.ci.computer`](https://metahuman.ci.computer/) |
| `Scripts/` | Build, test and E2E runners |

## Build and test (Windows)

The Vibe editor integrations and native Unreal MCP setup are documented in
[`Docs/EDITOR-AI-TOOLS.md`](Docs/EDITOR-AI-TOOLS.md).

The Unreal **Editor must be closed** — Live Coding holds a global lock and the build fails with
`Unable to build while Live Coding is active`.

```bash
pwsh Scripts/RunBackendTests.ps1
```

`RunTests.Common.ps1` now **compiles before it tests**. It previously ran the editor directly, which
loads whatever DLL was built last — so the suite could report a full green pass for source that was
never compiled.

To build without testing:

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MetaHumanLocalLLMEditor Win64 Development -project="C:\Users\hegem\OneDrive\Documents\Unreal Projects\MetaHumanLocalLLM\MetaHumanLocalLLM.uproject" -waitmutex
```

To package the game (authors the diorama first, then cooks):

```powershell
pwsh Scripts/BuildNative.ps1                    # MetaHuman resident (default): needs the Seo FBX export
pwsh Scripts/BuildNative.ps1 -Resident Nia      # opt-in Reallusion "Nia": needs her CC_ControlRig_Nia content
```

The default needs the MetaHuman "Seo" FBX export (`fbx\SKM_MHC_Seo_BodyMesh.fbx`,
`fbx\SKM_MHC_Seo_FaceMesh.fbx`, `fbx\MHC_Seo_Outfits.fbx`) in `-MetaHumanDir`, `MHLLM_MH_DIR`, or
`C:\devel\_mh-convert\out\seo`. It is not committed: copy core-2's `/home/ci/mh-seo`, or re-export it
in the 5.8 editor ([`Docs/SCENE-AUTHORING.md`](Docs/SCENE-AUTHORING.md#the-resident) has the steps).
The script checks for all three before launching the editor and says how to produce them if any are
missing. On core-2 the same choice is `MHLLM_RESIDENT=metahuman|nia` for
`Deployment/core2/m2_clean.sh`.

The end-to-end conversation harness runs against a local Ollama with no other services:

```bash
python Scripts/RunConversationE2E.py --llm-only --model llama3.2 --turns 10
```

Drop `--llm-only` to exercise TTS and STT as well; that needs Lemonade reachable at `:13305`.

## Configure

Every endpoint and credential comes from the environment, applied at module startup — one image,
every environment. See [`Docs/CONFIGURATION.md`](Docs/CONFIGURATION.md) for the full table and the
precedence rules, which have bitten before.

```sh
OLLAMA_BASE_URL=http://127.0.0.1:11434   OLLAMA_MODEL=llama3.2
KOKORO_SERVER_URL=http://127.0.0.1:13305 WHISPER_SERVER_URL=http://127.0.0.1:13305
WHISPER_MODEL=Whisper-Base               # REQUIRED by the OpenAI shape; blank omits the field
MHLLM_MEMORY=1                           # opt-in: persists transcripts to disk
```

## Deploy

See [`Deployment/README.md`](Deployment/README.md) and
[`Docs/BUILD-AND-DEPLOY.md`](Docs/BUILD-AND-DEPLOY.md).

## Known gaps

[`Docs/ROADMAP.md`](Docs/ROADMAP.md) is the authority. The largest remaining items are
**observability** (turn latency and failure rate are visible only by reading container logs over
SSH), **head motion and gesture**, **tool calling**, and **auth** — the OIDC gate has never worked
and `:8090` is open on the tailnet.

The default MetaHuman resident is **less finished than Nia was**: no idle animation (bind pose), the
flat constant-colour skin from the original port, and a face mapping (`ARKit`) that has not yet been
checked against the morph names on the FBX-exported Seo face — the `Face profile 'ARKit': …` line in the
runtime log says how many curves resolved. The measurements in the table above (lip-sync, gaze) were
taken on Nia. [`Docs/SCENE-AUTHORING.md`](Docs/SCENE-AUTHORING.md#the-resident) lists the gaps.

## Licensing

CI-MetaHuman's first-party code is free for noncommercial use under the [PolyForm Noncommercial License 1.0.0](LICENSE). See the [License FAQ](Docs/License-FAQ.md). Epic Games Unreal Engine and MetaHuman assets are **not** covered by this license; they remain under Epic's EULA terms (see [NOTICE.md](NOTICE.md)).

### The resident avatar

Builds place the **MetaHuman "Seo"** by default. The Reallusion Character Creator **"Nia"** is opt-in
(`-Resident Nia`, `MHLLM_RESIDENT=nia`), because this product is an AI companion and her licence
says no to AI:

- **Nia** — Fab listing [*Character Creator: Nia*](https://www.fab.com/listings/366cfc84-6d6a-44ff-89ad-4bffb7347c15)
  (Reallusion Inc; checked 2026-11-12): *License terms: Standard License*, **"Allows usage with AI:
  No"** — the `NoAI` tag — and a description that marks her *"Non-Commercial Use"*. The
  [Fab EULA](https://www.fab.com/eula) (last updated October 1, 2024), §6(b)(vii) and §16(l)(i): under
  a Standard License you may not use NoAI Content "(a) in datasets utilized by Generative AI Programs,
  (b) in the development of Generative AI Programs, or (c) as training inputs to Generative AI
  Programs", where Generative AI Programs are technologies "designed to automate the generation of or
  aid in the creation of new content". An LLM-driven character that speaks generated text is at best
  unclear against (b); treated as a licence issue, so she is no longer the default and her content is
  neither staged, mounted nor loaded unless asked for. Building with her is a decision for counsel,
  not for the build script.
- **MetaHuman** — Epic's [MetaHuman licence page](https://www.metahuman.com/license) (checked
  2026-11-12): MetaHuman "is included as part of your standard Unreal Engine license", MetaHuman
  characters and animation "are classed as Non-Engine Products under the Unreal Engine EULA", and on
  AI specifically: *"You can use MetaHuman characters and animation in workflows that incorporate
  artificial intelligence technology. However, you may not use MetaHuman characters or animation
  curves to build or enhance any database or train or test artificial intelligence, machine learning,
  deep learning, neural networks, or similar technologies."* The
  [Unreal Engine EULA change log](https://www.unrealengine.com/eula-change-log/unreal) records that
  remaining restriction as removed: *"EULA update 22 (October 30, 2026): Removes the restrictions in
  Section 6(e) on using the Licensed Technology as training or prompt input for generative AI, and on
  using MetaHuman digital characters and animation curves to build databases or to train or test AI
  and machine learning technologies."* The current [EULA](https://www.unrealengine.com/eula/unreal)
  §6(e) carries no AI clause. Rendering Seo, driving her face from TTS timing and streaming the result
  trains and tests nothing, so this use is permitted under either version. Until the update-22 EULA
  has been accepted, do not feed captured frames or animation curves of her into model training or
  evaluation. Distribution follows EULA §4: the packaged build and the container image carry the
  engine only in object code, and Pixel Streaming counts as Distribution ("make its functionality
  available on a network").

Still open, for the owner rather than the build: **where `mhc_seo.mhpkg` came from is not recorded.**
Its only dependencies are Epic's own MetaHuman Creator library (`/MetaHumanCharacter/...` grooms and
`WI_DefaultGarment`), which fits a character made in-house with MetaHuman Creator. If it was instead
obtained from a third party, that listing's own licence (and any NoAI tag) applies on top of Epic's.
Record its origin here once confirmed.

<!-- companion-hub-readme-deep-review -->

## Build and runtime reference

| Stage | Platform | Entry point |
|---|---|---|
| Build and test | Windows | Follow the project build/test commands below |
| Configure | Local environment | Use the checked-in configuration template and provide local credentials separately |
| Deploy | Target runtime described in this repository | Review the known-gaps section before relying on unfinished behavior |

This project distinguishes the character's intended role from the parts that are currently implemented. Treat the [Known gaps](#known-gaps) section as the source of truth for incomplete integrations, and do not commit secrets or private voice/persona data in configuration files.

