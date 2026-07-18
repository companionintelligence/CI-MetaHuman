# Metahermit — Successor Handoff

> Written 2026-07-18 by the outgoing agent. Read the **Lessons** first (you were told to), then
> **Current state**, then **The immediate next step**. Everything else is reference.

---

## 0. TL;DR

**Metahermit** is an embodied AI companion: a real-time character that lives inside a **1970s
research-datacenter diorama**, GPU-rendered in **Unreal Engine 5.5**, streamed to a browser via
**Pixel Streaming**, running on an **AMD** box (core-2). Backed by a local LLM (Ollama `gemma4:e2b`)
today; CI-ecosystem integration (Hub/Portal/Memory/Hermes) is roadmapped.

- **The diorama is LIVE on the AMD stream** (`http://core-2:8090`): enclosed machine room, 12 real
  period machines at true scale, a rigged humanoid placeholder, on-brand lighting.
- **Right now** we're mid-task on swapping that placeholder for a **real MetaHuman** from
  `C:\devel\llm-metahuman`. The conversion is proven feasible; one step (assemble+export) needs the
  interactive UE 5.8 editor and the user is doing it manually. See §5.
- Two GitHub PRs open (§2). Design docs live beside this file: `GDD.md`, `ANIMATION-PLAN.md`.

---

## 1. LESSONS (start here)

These are the traps that cost real time. Internalize them before touching the cook loop.

### Process

1. **Verify with pixels, not logs.** `LoadMap complete` + player page `HTTP 200` proves the *stream
   is up*, NOT that it shows anything. I repeatedly reported "it works" from logs while the actual
   frame was black / all-ceiling / floor-tiles. **Always capture and LOOK at a decoded frame.** The
   tool is a headless Playwright capture on core-2 that grabs the WebRTC `<video>` into a canvas PNG
   + luminance stats (`/home/ci/shots/cap2.mjs`, `cap5.mjs`). `meanLuma ~0.03` = black; `~255` =
   blown white; a real frame is in between. This single habit would have saved ~5 cook cycles.

2. **Deploy from a clean git checkout, not hand-maintained server copies.** THREE separate ~20-min
   cook failures had the same root cause: core-2's working copies (`/home/ci/ci-metahermit`,
   `cook-m2`, `stage-out`) drifted from the repo. Each fix already existed in git but hadn't reached
   the cook tree — `Target.cs` (`V7` vs `.Latest`), the `MoverTests` plugin ref, the water-collision
   profile. **Make the cook `git clone` a pinned ref.** Until then, before every cook, diff the
   cook tree's `Source/*.Target.cs`, `*.uproject` plugins, and `Config/DefaultEngine.ini` against the
   repo.

3. **Tune lighting/render against the DEPLOY config, not a local preview.** Local UE 5.8 has **Lumen
   GI on by default**; the stream runs **GI disabled** (`r.DynamicGlobalIlluminationMethod 0` et al —
   the RADV stability workarounds). A scene that looked well-lit locally rendered **black** on the
   stream because there was no bounce. The right test is rendering in the container on the real AMD
   GPU with the exact stream `ExecCmds` (see `/home/ci/gpu_render.sh` pattern) — no cook needed.

4. **Read the API's actual contract.** `unreal.Rotator`'s Python constructor is **`(roll, pitch,
   yaw)`** — NOT pitch-first like C++ `FRotator`. Passing positionally tipped every prop onto its
   face and rolled the camera horizon. Pass rotation by keyword.

5. **Measure in the destination; don't trust the source.** Sketchfab FBX/`.blend` props carry wildly
   inconsistent unit scales, and some resist Blender fixing entirely (multi-user mesh data makes
   `transform_apply` a silent no-op) — that shipped a **29-metre "desk terminal"** that enclosed the
   camera and rendered the whole stream black. The fix: normalize scale by measuring the **imported
   mesh's bounds in UE** against a declared real-world target dimension. Self-correcting for any
   source. (`Deployment/author_diorama.py`, `ue_prop_fbx.py`.)

### Unreal / cook

6. **Author levels in a throwaway content-only project.** A `.umap` referencing only `/Engine/`
   (and engine-plugin) content is portable across same-engine-version projects. Authoring in a
   content-only project (`Modules: []`, only `PythonScriptPlugin` + whatever plugin content you
   reference) sidesteps the plugin-load *abort* that killed the full-project commandlet: the real
   `.uproject` enables editor-only plugins like `MCPClientToolset` that are **missing on disk in the
   dev-5.5 image**, and the editor `Fatal error`s at plugin-config. The cook (a *game* target) skips
   editor-only plugins, which is why it tolerates them but the editor commandlet does not.
   Pattern: author in `/home/ci/authorproj` (content-only) → drop the `.umap` + imported assets into
   the cook tree → cook.

7. **Enabling a plugin has a blast radius.** `MoverTests` (for the `SKM_Manny` humanoid) pulls in the
   **Water** plugin → the cook demands a `WaterBodyCollision` collision profile in
   `DefaultEngine.ini` or it fatal-errors unattended. It also multiplied UE's DirectoryWatcher usage
   → exhausted the host **inotify** ceiling (see §infra). Add plugins deliberately and to **all**
   cook-side uprojects (`stage-out` + `cook-m2`, not just `ci-metahermit`).

8. **MetaHuman assemble/export CANNOT be scripted headlessly.** `build_meta_human` /
   `export_geometry` block the game thread while waiting on async render work that needs the game
   thread to *tick* → deadlock (headless with `-unattended` it SIGSEGVs in `TextureGraph.dll`;
   without it, headless or full-GUI-via-`-ExecCmds`, it hangs forever). Must be driven from the
   interactive MetaHuman Character editor UI (its buttons run as message-pumping slow-tasks).
   Full detail + the working import/export API in memory `metahuman-mhpkg-to-55.md`.

9. **The engine split is deliberate and load-bearing.** UE **5.8's Vulkan render SIGABRTs in RADV on
   the gfx1151 GPU** (reproducible across every Mesa/firmware/flag). UE **5.5 renders fine.** So:
   **deploy/cook on 5.5**, author/MCP/MetaHuman-work in 5.8. Don't "upgrade" the deploy to 5.8.

10. **Git-Bash mangles `/Game/...` args** into `C:/Program Files/Git/Game/...`. When invoking
    `UnrealEditor-Cmd.exe` with a `/Game/...` map path from the Bash tool, set
    `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'`.

### Infrastructure (core-2)

11. **Docker images get pruned out from under you.** Happened **twice** — no cron/timer/disk-pressure
    on core-2, so it's a *parallel agent/session* running `docker system prune -a`. Re-pulling
    `dev-5.5` is **85 GB**. If images vanish, re-pull `ghcr.io/epicgames/unreal-engine:dev-5.5`,
    `pixel-streaming-signalling-server:5.5`, `ubuntu:26.04` (ghcr auth persists; verify with
    `docker manifest inspect`).

12. **BuildKit cache corrupts** → `unable to lease content: lease does not exist`. Fix:
    `docker builder prune -af` (cache only; reclaimed 136 GB last time). Note the next build then
    recompiles every layer from scratch (slower).

13. **inotify watch ceiling.** Stock `fs.inotify.max_user_watches=65536` is exhausted by a UE cook
    with many plugins → `inotify_add_watch ... (Out of inotify watches)` → RunUAT exit 25. Raised
    at runtime to `524288` via `sysctl -w` as **root@core-2** (reverts on reboot — the user can make
    it permanent in `/etc/sysctl.d/` themselves; do NOT edit their `/etc` files without asking).

14. **Pixel Streaming has no HW encode on this box.** HW encode is NVENC (NVIDIA) or AMF (AMD); on
    AMD/RADV/Linux neither works, so it falls back to **software VP8** (higher CPU, acceptable at
    720p). This is a deliberate, documented trade-off, not a bug.

15. **The OIDC login gate has never worked.** `deployment-oidc-proxy` exits(1): the configured issuer
    `portal.companionintelligence.com` is **NXDOMAIN** (real issuer is
    `https://hub.companionintelligence.com/api/auth`, verified 200) and `CI_PORTAL_CLIENT_SECRET` is
    the literal placeholder `__NEEDS__`. I fixed the issuer in `Deployment/.env`; the **client
    id/secret must be registered in CI-Portal by the user** (redirect `http://core-2:8095/oauth2/callback`).
    Until then `:8090` is unauthenticated and open on the tailnet.

### Boundaries (respect these)

- Don't edit core-2 `/etc` files; don't run destructive git/`rm` on the staged copies; don't `pkill`
  patterns that could match the launching shell; **never enter the user's credentials** (Epic,
  CI-Portal, etc.) — those are user actions.
- **MetaHuman EULA:** may render/animate freely, may **not** train models on MetaHuman assets.
- The web repo (`CI-Spatial-Companion-WebXR`) is proprietary (Lifescope Inc.); its `CLAUDE.md` says
  npm-only but the repo is actually a **pnpm** workspace (has `pnpm-lock.yaml`) — I used pnpm and it
  built clean.

---

## 2. Repos & PRs

| Repo | What | State |
|---|---|---|
| **`companionintelligence/Companion-MetaHuman`** (formerly `ci-metahermit`) | The UE 5.5 / Pixel Streaming app. This repo. | PR **#1 merged** (GDD, animation plan, CompanionStage, viseme groundwork). PR **#2 open** (`feat/companion-stage-viseme-groundwork`): diorama authoring + scale normalization + 5.5 cook fixes + voice hardening. |
| **`companionintelligence/CI-Spatial-Companion-WebXR`** | A *separate* web-native (React 19 + R3F) avatar app. The diorama scene was ported here too (React component + 12 GLBs). | PR **#116 open**. **Parked** — user said stop web work; UE is the active path. |

Local checkouts: UE project at `C:\Users\hegem\OneDrive\Documents\Unreal Projects\MetaHumanLocalLLM`
(git module still named `MetaHumanLocalLLM`, remote `Companion-MetaHuman`). `gh` authed as
`mrhegemon` (org admin). Uses Git LFS for `*.uasset`/`*.umap`.

---

## 3. The two render paths

- **UE → Pixel Streaming (ACTIVE).** Renders on core-2's AMD GPU, streams VP8 over WebRTC. Photoreal
  target. This is what "launch the pixelstreamed version" means.
- **Web → Three.js/WebGL (PARKED).** Renders on the *viewer's* GPU, no server. Lives in the WebXR
  repo. Built during an earlier detour; user chose the UE path and told me to stop the web version.
  Don't resume it unless asked.

---

## 4. Infrastructure & the build/deploy loop

**core-2**: AMD Ryzen AI Max+ 395 / Radeon 8060S, Ubuntu 26.04, Tailscale `100.101.156.33`.
SSH `ci@core-2` (docker group) or `root@core-2` (for sysctl). This box also runs the CI ecosystem
(ci-memory/ci-hermes/ci-openclaw/ci-import-tools) — **don't disturb those containers.**

**The live stream** = compose project `deployment` from
`Deployment/docker-compose.{yml,amd,ollama,ue55}.yml`, service `project`
(`deployment-project-1`), image tag pinned in `docker-compose.ue55.yml`. Player page `:8090`.
Backend `gemma4:e2b` via Ollama (`docker-compose.ollama.yml`).

**The cook loop** (helper scripts live in `/home/ci/` on core-2, generated this session):
1. **Author** the map headlessly in the content-only project: `run_author_m2.sh` runs
   `author_diorama.py` in a `dev-5.5` container (mounts `/home/ci/props-glb`), writes
   `/home/ci/stage-out/Content` + `AUTHOR_RESULT.txt`.
2. **Prep + cook**: `m2_full.sh` = author → copy `stage-out/Content` into `cook-m2` → set
   `GameDefaultMap` → `docker build -f Deployment/Dockerfile.amd --build-arg RELEASE=5.5 -t
   ci/metahuman-local-llm:5.5-m2 .` → deploy.
3. **Deploy**: point `docker-compose.ue55.yml` at the new tag → `docker compose ... up -d project`.
4. **Verify**: run `cap5.mjs` (Playwright, no mouse input) → pull `/home/ci/shots/ship-view.png`
   to core-2 → `scp` it here → **Read the PNG and LOOK at it.**

A cook is ~20 min (longer after a cache prune). Kick it in the background and poll for
`M2v3 DONE` / `BUILD_FAIL` in the log; on success the script auto-captures a frame.

**Known-good authoring facts** baked into `author_diorama.py`:
- Props imported as **GLB** (not FBX) via UE Interchange — FBX dropped textures; GLB carries
  materials+textures. Four `.blend`-sourced props needed materials *attached* (they referenced no
  image node); `blend_fix_mats.py` rewires each shipped texture into a Principled BSDF.
- Scale normalized in-UE from measured bounds (see Lesson 5).
- Rotator passed by keyword (Lesson 4).
- Low even ceiling grid + warm/aqua/mint accents for a GI-off scene (Lesson 3); manual-exposure PPV.
- Resident = `SKM_Manny` + `ABP_Manny` from the engine `MoverTests` plugin (rigged, no Epic login).

---

## 5. Current state by workstream

| Workstream | State | Notes |
|---|---|---|
| **Diorama on AMD stream** | **LIVE & verified** (captured frame, `meanLuma 117`) | 12 textured props, true scale, enclosed room, rigged humanoid, framed camera. Art still reads "bright room" not "70s machine room" — task #22. |
| **Real MetaHuman** | **In progress — user doing the manual export step now** | See "immediate next step". |
| **Camera/input** | **Known bug** (task #23) | View only frames correctly via an auto-activating `StageCamera`, which welds the view target → free-look input is inert. The `DefaultPawn` ignores the `PlayerStart` (spawns at origin in the floor) for reasons not yet understood; re-authoring the set around origin didn't fix it. Shipping fixed-camera (GDD intent) for now. |
| **OIDC login** | **Broken, needs user** | Issuer fixed to hub.../api/auth; client id/secret still `__NEEDS__` (Lesson 15). |
| **Design docs** | Done | `Docs/GDD.md` (9 sections + appendices), `Docs/ANIMATION-PLAN.md` (cross-platform phoneme visemes, anti-Audio2Face). |

---

## 6. THE IMMEDIATE NEXT STEP (pick up here)

We are converting `mhc_seo.mhpkg` → a MetaHuman the 5.5 stream can show.

**Proven** (offline, no Epic login — full detail in memory `metahuman-mhpkg-to-55.md`): `.mhpkg`
imports via `MetaHumanPackageFactory`; `can_build_meta_human = True`; export API is
`MetaHumanCharacterExportBlueprintLibrary.export_geometry/export_dcc/export_dna`. `mhc_seo` is
imported + saved in the scratch project `C:\devel\_mh-convert\MHConvert.uproject`.

**Blocked step** (Lesson 8): assemble+export must be done in the **interactive UE 5.8 editor**. The
editor is open on the user's machine; they were given steps to **Assemble `MHC_Seo` and export the
Face+Body skeletal meshes to `C:\devel\_mh-convert\out\seo\fbx\`**.

**When the FBX lands, automate the rest** (all scriptable):
1. Prep the FBX/textures like a prop (the `ue_prop_fbx.py` / GLB pipeline; watch scale — Lesson 5).
2. Stage to core-2 (`/home/ci/props-glb` or a new dir), import into the CompanionStage authoring.
3. Replace the `SKM_Manny` resident block in `author_diorama.py` with the imported MetaHuman
   skeletal mesh (keep the same transform/label).
4. Cook (`m2_full.sh`) — **first diff the cook tree vs repo** (Lesson 2). `dev-5.5` was just
   re-pulled and is present.
5. Deploy + **capture-and-look** (Lesson 1).
6. Later: wire the exported **DNA** into a 5.5 Face rig for visemes (see ANIMATION-PLAN.md); and the
   viseme groundwork in `CICompanion` (`OnSetVisemeWeights`/`OnSetEmotion`).

The other characters (`mhc_crowd80/84`, and the `khaosmdf492` *outfit*) convert the same way.

---

## 7. Open issues / task list

- **#22** Diorama art pass — several large props render pale despite importing textures (check UE
  Interchange binds the glTF base-color param; the web build renders the same GLBs correctly).
  Push era mood (CRT glow, panel lamps, patina) per GDD §2.
- **#23** DefaultPawn ignores PlayerStart → free-look input dead (see §5). Next probe:
  `-AllowPixelStreamingCommands=true` (container restart, no cook) + dump `PlayerCameraManager`/pawn
  transform at runtime; or set a project GameMode with explicit `DefaultPawnClass`.
- **#17** the MetaHuman swap (this is the active task, §6).
- OIDC client registration (user).
- Make the inotify sysctl permanent (user).

---

## 8. Key paths

- UE repo: `C:\Users\hegem\OneDrive\Documents\Unreal Projects\MetaHumanLocalLLM` → `Deployment/`
  (`author_diorama.py`, `ue_prop_fbx.py`, `blend_fix_mats.py`, `Dockerfile.amd`, compose files),
  `Docs/` (`GDD.md`, `ANIMATION-PLAN.md`, `FOLLOW-UP.md`, this file).
- Assets: `C:\devel\llm-metahuman` (`.mhpkg` MetaHumans, `scene/` prop zips, `scene/glb/` +
  `scene/ue-fbx/` converted props).
- MetaHuman scratch: `C:\devel\_mh-convert` (`MHConvert.uproject`, `stage2b_export_nobuild.py`, the
  `INTROSPECT.txt`/`STAGE1.txt` API dumps, `out/seo/` for the FBX).
- core-2 helpers: `/home/ci/*.sh`, `/home/ci/{authorproj,stage-out,cook-m2,props-glb,shots}`.
- Memory: `C:\Users\hegem\.claude\projects\C--devel\memory\` — read `metahuman-local-llm.md` and
  `metahuman-mhpkg-to-55.md`.
