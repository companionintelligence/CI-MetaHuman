# Handoff — Finish the MetaHuman skin (for Opus)

> **RESOLVED 2026-07-19.** Root cause was NOT the LOD material map or texture bakes — it was that
> `M_SeoSkin`/`M_SeoHidden` (created via `MaterialFactoryNew`) had **no usage flags**, so applying
> them to a SkeletalMesh made the cook fall back to the DEFAULT gray material (log: "Material with
> missing usage flag was applied to skeletal mesh"). That single cause explains why all 5 attempts
> below rendered pixel-identical gray. **Fix:** set `used_with_skeletal_mesh=True` +
> `used_with_morph_targets=True` on both materials before `recompile_material` (see
> `author_diorama.py`, the "gray-render root cause" comment). Verified: "missing usage flag"
> warnings 54→0; cooked + deployed; Seo now renders warm skin on the live stream. A **fast GPU
> preview loop** was also built to get here — `Deployment/Dockerfile.editorpreview` →
> `ci/ue55-editor-preview:mesa` + `/home/ci/gpu_preview.sh` (renders the UNCOOKED map on the GPU;
> see `metahuman-mhpkg-to-55.md` memory for the ICD/setpriv/group gotchas).
>
> **What "skin" means here — read before judging a frame.** Only her **head/neck, forearms and
> lower legs** render. **Torso, upper arms and thighs are ABSENT** — MetaHuman hides the body
> geometry that sits under clothing, and the garment mesh is meant to fill it. In the streamed
> frame the large pale slab behind her is a **prop box, not a dress** (her arm casts a shadow onto
> it); don't mistake it for her torso, and don't sample it for "is the skin warm?" — I did, and
> briefly mis-reported the torso as skinned. Compare `/home/ci/shots/ship-view.png` (pre-fix, gray)
> with `m2-canvas.png` (post-fix, warm): the SAME parts are missing in both, so it is geometry, not
> a material bug.
>
> **Next step, and it is cheap:** `Content/Seo/Outfit/MHC_Seo_Outfits.uasset` (+ its
> `MI_WI_DefaultGarment_*_Shirt/Short` materials) was already imported, but `author_diorama.py`
> **never spawns it** — `seo_parts` lists only Body and Face. Import/place the outfit at the same
> transform (and give its materials the same skeletal-mesh usage flags) to clothe the gap. After
> that: real skin textures (or `export_dcc` for MetaHuman's own materials) + the DNA face.
> The rest of this doc is the original investigation, kept for context.

---

Focused handoff for one job: **the converted MetaHuman "Seo" renders in the UE 5.5 AMD
Pixel Streaming diorama but is flat gray. Make her skinned.** Everything else about the
MetaHuman works. Read the general `HANDOFF.md` first for the project + the core-2 build loop;
this doc is the skin problem in depth so you don't re-walk the ground I already covered.

---

## 1. State — what works

`mhc_seo.mhpkg` (a 5.6 MetaHuman EditableCharacter) is **fully converted to UE 5.5 and
rendering live** in the diorama on core-2 (`http://core-2:8090`). Pipeline, all proven:

1. **Import + assemble (5.8, GUI):** `.mhpkg` imported via `MetaHumanPackageFactory`; assembled
   in the interactive UE 5.8 editor (headless assemble/export DEADLOCKS — game thread waits on
   async render work; must use the GUI). Assembly needs **no Epic login** (local DNA + local
   texture-synthesis model). Scratch project: `C:\devel\_mh-convert\MHConvert.uproject`.
2. **Export (5.8, GUI):** the assembled Face/Body `SkeletalMesh` assets exported to **FBX** via
   right-click / `Deployment/metahuman/03_export_fbx_from_GUI.py` run *inside the GUI editor*
   (a headless `AssetExportTask` FBX export also crashes — `UnrealEd` exporter faults in a
   commandlet). Output: `C:\devel\_mh-convert\out\seo\{fbx,textures}`.
3. **Re-import into 5.5:** the FBX import cleanly into dev-5.5 as skeletal meshes — Body ~137 cm
   (headless body; face is separate), Face head-sized with **849 morph targets** (the facial rig,
   your future viseme source). Confirmed with `04_probe_import_into_55.py`.
4. **Placed in the diorama:** `Deployment/author_diorama.py` imports Body+Face, places them at
   the camera's look target (`RES_POS=(330,20,0)`, `RES_YAW=-90` — a re-imported MetaHuman FBX
   has its visual front 90° off the actor forward axis), Body+Face at the same transform align
   perfectly. Cooked into `ci/metahuman-local-llm:5.5-m2`, deployed, verified by captured frame.

She stands correctly, faces the camera, is lit and shadowed. **She is gray.**

On core-2: FBX+textures at `/home/ci/mh-seo/{fbx,tex}`; the material/placement logic is the
"the resident: MetaHuman Seo" block in `author_diorama.py`.

## 2. The problem, precisely

**Materials assigned to the mesh do not reach the cooked render.** Across **five** cooked
iterations the streamed frame is *pixel-identical gray*:

| Attempt | Approach | Result |
|---|---|---|
| v15 | component `set_material(i, MIC)` override | gray |
| v16 | mutate `mesh.get_editor_property("materials")[i]` in place | gray (didn't persist) |
| v17 | build **fresh `SkeletalMaterial` structs** + `save_loaded_asset` | gray (persisted on disk, still gray render) |
| v18 | same, but material = **constant skin-tone color** (not texture) | **gray** |

v18 is the tell: a constant-color material *cannot* render gray if it is applied. Geometry
changes (the yaw flip) **do** render, so deploys land — only materials are inert.

### Already verified correct (don't re-check these)
- Texture assets valid: `T_Body_BC` 4096² has real skin color (186,141,129); `T_Teeth_BC` colored.
- **BUT** `T_Head_BC` is a flat neutral gray (94,94,94) — MetaHuman bakes a neutral base the full
  skin shader tints; a plain texture sample of it is gray by construction (root cause #1, but only
  explains the *face*, not the body).
- `M_SeoSkin` base-color connect returns `True`; MIC `MI_SeoBody` `BaseColor` override = `T_Body_BC`
  (or the constant color in v18). Material params all correct (`06_material_diagnostic.py`).
- On disk, in **both** `stage-out/Content` and `cook-m2/Content`, the meshes reference
  `MI_SeoBody` / `MI_SeoHead` etc. (`probe_seo_mats.py`), i.e. assignment persisted.
- `M_SeoSkin` compiles in the cook (no "using default material" fallback in the cook log).
- `deployment-project-1` runs the freshly-built image each time (checked image SHA/created time).

So: valid materials, correctly assigned on the asset, cooked, deployed — and not rendered.

## 3. The real blocker: no fast iteration

Every material test above cost a **~20-minute cook**. The fast loop I built to preview the
authored map on the GPU **without cooking** (`/home/ci/gpu_stage.sh`) fails:
`VK_ERROR_INCOMPATIBLE_DRIVER` — the **dev-5.5 builder image's Mesa is too old for the gfx1151
GPU**. (The *deploy* renders fine because it uses a *separate* `ubuntu:26.04` + newer-Mesa
runtime image that has only the packaged app, not the editor.)

**Your first task is a fast material-preview loop.** Options:
- Build a throwaway image = `ubuntu:26.04` + `mesa-vulkan-drivers` (+ oibaf PPA) **on top of the
  dev-5.5 builder** (so it has both the editor and new Mesa), then `gpu_stage.sh` will render the
  authored map on the GPU in ~4 min. `Deployment/Dockerfile.mesagit` is a starting point.
- Or render on an NVIDIA node (`docker-compose.nvidia.yml` exists) purely for previews.
Without this you'll waste hours on 20-min cooks. Fix it first.

## 4. Recommended fix (ranked)

1. **Carry MetaHuman's OWN materials across, don't rebuild them.** In the 5.8 GUI editor use
   `MetaHumanCharacterExportBlueprintLibrary.export_dcc(character, MetaHumanDCCExportParams{...})`
   — it writes mesh **+ materials + textures** as a DCC package to disk (works in the GUI; the
   headless version deadlocks). Re-import that package into 5.5 to get real, textured materials
   instead of my hand-built ones. This most likely sidesteps the whole assignment problem.
2. **Find why asset-material assignment isn't rendered** (the actual bug). Strong hypothesis:
   MetaHuman skeletal meshes carry per-LOD section→material mappings (`LODInfo` /
   `LODMaterialMap`), and the top-level `materials` array I set is *not* what the rendered LOD
   sections resolve to. Inspect `mesh.get_editor_property("lod_info")` and each LOD's section
   material indices; also try setting the full `override_materials` **array** on the level actor's
   `SkeletalMeshComponent` (serializes into the `.umap`, which definitely cooks). Verify with a
   constant-color material and the fast preview loop from §3 — should take minutes once that loop
   exists.
3. Last resort: rebuild MetaHuman's skin shader in 5.5 (big; only if 1 & 2 fail).

## 5. Build / deploy / verify loop (reference)

- Author + cook + deploy: `ssh ci@core-2 'bash /home/ci/m2_full.sh > /home/ci/mNN.log 2>&1 &'`
  (authors `author_diorama.py` in a content-only project → copies content into `cook-m2` →
  `docker build Dockerfile.amd --build-arg RELEASE=5.5` → `docker compose up -d project`).
- **Author-only** (no cook, ~3 min) to verify asset state: `bash /home/ci/run_author_m2.sh`,
  then probe `stage-out/Content` with `05/06/probe_seo_mats.py`. **Always verify the asset on
  disk before spending a cook** — I lost cooks skipping this.
- Capture the live frame (no mouse input, so it doesn't rotate the camera):
  `docker run ... mcr.microsoft.com/playwright:... node /home/ci/shots/cap5.mjs` → pull
  `/home/ci/shots/ship-view.png` and **look at it**. `meanLuma ~0.03` = black; skin should read
  warm, not neutral gray.
- Known cook gotchas (all fixed but they recur if server copies drift — diff `cook-m2` vs repo):
  `Target.cs` must be `.Latest`; `MoverTests` in all cook-side `.uproject`s; `WaterBodyCollision`
  profile in `DefaultEngine.ini`; raise host `fs.inotify.max_user_watches`; `m2_full.sh` now
  purges `cook-m2/Content/Seo` before copy (it didn't, and cooked stale gray meshes — that cost
  me a cook).

## 6. Files

- `Deployment/author_diorama.py` — the diorama author incl. the Seo import + material block to fix.
- `Deployment/metahuman/` — the conversion scripts: `01_import_and_api_probe.py` (the working
  import + the full MetaHuman Python API dump), `02_export_headless_DEADLOCKS.py` (what NOT to do),
  `03_export_fbx_from_GUI.py` (the working export — run it *in the GUI editor*),
  `04_probe_import_into_55.py`, `05_probe_material_slots.py`, `06_material_diagnostic.py`,
  `MHConvert.uproject` (the scratch conversion project's uproject).
- Assets: FBX+TGA at `C:\devel\_mh-convert\out\seo`; the `.mhpkg` sources at `C:\devel\llm-metahuman`.
- Memory: `metahuman-mhpkg-to-55.md` (the conversion pipeline + the assemble/export deadlock).

The other characters (`mhc_crowd80/84`) and the `khaos` outfit convert identically once skin is solved.
