# Scene authoring: the stage set

How the set is built, why it is built that way, and what to reach for next. The set itself, LONGVIEW
Lab, Bay 7 as it stood in 1979, and its camera are described in [STAGE-DESIGN.md](STAGE-DESIGN.md).

## The constraint everything follows from

The map is authored **headlessly by Python in a content-only project** that cannot reference
plugin C++ or embed a Blueprint (HANDOFF.md Lesson 6). So the `.umap` stays a purely visual
asset — plain `StaticMeshActor`s and `SkeletalMeshActor`s — and every behaviour is attached at
runtime by a `UWorldSubsystem` that finds actors by **tag**.

Tags, never labels: actor labels are editor-only and do not survive a cook, so a label-based
lookup works in the editor and silently finds nothing in the shipped build.

Subsystems in play: `UCompanionFaceSubsystem` (face), `UCompanionRuntimeSubsystem` (brain, voice,
HUD), `UCompanionStageDirector` (the stage camera and the opening's beats), `UTapeReelSpinner` (reels).

## Who runs the authoring

`Deployment/stage_layout.json` is the **only** definition of the set, and `Deployment/author_diorama.py`
the only thing that builds it. Two things run the script, and both must, or you ship an empty room:

| consumer | how | inputs |
|---|---|---|
| the Linux cook on core-2 | `Deployment/core2/run_author_m2.sh` -> `dev-5.8` container, `-nullrhi`; `stage.sh` stages the script, `stage_layout.json` and `stage_layout.py` side by side | mounted at `/glb`, `/audio`; `/mh` **only** with `MHLLM_RESIDENT=metahuman` or `nia`, `/nia` **only** with `nia` |
| the native Windows build | `Scripts/BuildNative.ps1` -> installed `UnrealEditor-Cmd.exe`, `-nullrhi` | `MHLLM_GLB_DIR` / `MHLLM_MH_DIR` (`-MetaHumanDir`, read only with `-Resident MetaHuman`) / `MHLLM_AUDIO_DIR` / `MHLLM_GEN_DIR`; Nia staged into `Content/CC_ControlRig_Nia` **only** with `-Resident Nia` |

The script reads the layout from beside itself (`MHLLM_STAGE_LAYOUT` overrides the path).

Both pass the resident choice to the script as `MHLLM_RESIDENT` — see [The resident](#the-resident).

The script reads every external path through `MHLLM_*` environment variables whose **defaults are
the container mount points**, so the server pipeline is unchanged and the local build simply
points them at Windows paths. Never hard-code a new input path — add it to that block, or the
native build silently diverges from the cook.

That rule has already been broken once and cost the set two features. `T_CRT_Vector.png` and
`S_RoomTone.wav` were read straight out of `GLB_DIR`, where they existed **only on core-2**, and
both imports are wrapped in `try/except` — so every native build shipped with unpowered CRT
screens and a silent hall, and the only evidence was `There was nothing to import` buried in the
authoring log. Both files are now generated into the repo (below) and resolved through
`MHLLM_GEN_DIR`, whose default is `GLB_DIR` so the container keeps reading them from `/glb`
exactly as before. The import sites now also log when they find nothing, and `BuildNative.ps1`
fails the build if the author's `self-check: ... all counts match` line or `room tone placed` is
missing from `AUTHOR_RESULT.txt` (the CRT gate went with the textured CRTs: the 1979 room's 3277
screens are lit by emissive overlays on the GLB, which the blockout count covers).

## The layout: `Deployment/stage_layout.json`

The set is data in the stage design's layout format, one array element per line so a diff reads like
a change list. The script is its interpreter: change the set by changing the JSON.

| section | what the author does with it |
|---|---|
| `meta` | `exposureEV100` pins the post-process volume (solved on a captured HOME frame: `exposureNote` says how) and `whiteBalanceK` sets its white balance; `emissiveUnits` and `lightColorEncoding` state the units below; `eyeForwardCm` places her eye anchor; the design's plan gates, UI zones and licensing record ride along unread |
| `props` | each distinct `glb` is imported once into `/Game/Diorama/<slot>`, behind the NoAI guards, then placed at its `x, y, z` / `yaw` at `scale` (the file's own scale times this), or normalised to `targetCm` by measured bounds (older layouts). `materialColors` recolours imported materials by name; `opaque` (true) drops the translucent blend-mode override Interchange leaves on a glTF BLEND or MASK material after the re-parent onto `M_PropGLTF`, which has no opacity, so the prop renders opaque instead of dark and see-through (the Kenbak-1's front panel, the SRI mouse's wooden body). A slot can be edited before import, on a copy of the GLB in the temp directory, the same way the previz edits it: `clipLocal` (`{"x": [lo, hi], ...}`, glTF scene-space metres) removes every triangle with a vertex outside the box (the RAMAC's loose platter, the Kenbak's power cord); `rotateLocal` (`[["x", 90], ...]`, turns about the glTF scene axes, in order) stands up a model exported lying down (the Kenbak), so every prop is still placed by yaw alone; and a file of several meshes is merged into ONE mesh on one node, its node transforms baked in, because Interchange makes a StaticMesh of each glTF mesh and a prop places one (the SRI mouse is 16 meshes, the Kenbak 79). A slot that names sub-meshes (`mesh`, `reels`) is never merged, and the import line reports how many static meshes a slot produced (`prop INCOMPLETE` when a prop would place one of several). `tags` and `reels` (separately pivoted sub-meshes, Movable, tagged) as given |
| `blockouts` | true-size engine primitives: `size` in cm in the blockout's own frame, `shape` box or cylinder (`axis` x, y or z), `color` (sRGB hex) with `rough`, `emissive`, `opacity` or `tileCm` deriving a material instance of one of four masters (`/Game/Diorama/StageMats`; identical looks share one instance). `emissive` is cd/m2 per unit luminance of `color`, and an emissive blockout's `color` is its DISPLAY colour, balanced into a scene colour by the inverse of `meta.whiteBalanceK` (Unreal reads emissive 1 as 1 cd/m2, the unit of its candela lights; the previz's default `--emissive nits` reads it the same way). `tileCm`, `groutCm`, `groutColor` and `groutRough` draw a tile grid from world position (`M_StageTile`). `text` puts a sign on the box's -X face; `tags`, `mobility`, `attachTo` (attached after everything is placed, read back), `lightingChannels` and `castShadow` (false: a light fitting's own parts, which must not shadow its light) as given. An explicit `material` object (paint, emissive, glass, texture types) is also accepted |
| `lights` | `point`, `spot`, `rect` or `directional`, aimed by yaw/pitch; `units` (`cd`), `radiusCm`, `srgb` (the colour sRGB-ENCODED, decoded to linear before `set_light_color`, which reads linear), `linear` or `kelvin`, `shadows`, `sourceRadiusCm`, `coneDeg` (the full cone), `sizeCm`, `lightingChannels`, `tags` |
| `resident` | her mark (`x, y, z`) and `actorYaw` (these meshes face 90 degrees off the actor's forward axis); `homeFacingYaw` becomes the `StageMark.Home` TargetPoint; `eyeZFallback` is the eyeline used only when no mesh could be measured |
| `sounds` | imported, looped and placed as `AmbientSound`s; `volume` and `spatialize` when given |
| `shots` | the shot labelled `StageCamera` becomes the authored fallback camera and the `PlayerStart`; a `targetPoint` tag becomes TargetPoints at its keys; every shot and the `beats`' `cue` fields also become the camera's config (below) |

**Self-checks.** Every expected count comes from the layout: blockouts, lights, props (less any slot the
NoAI guard refused), TapeReel actors, attachments, TargetPoints, and zero material failures. The
script prints them as one `self-check:` line and stops before `SUCCESS` on any mismatch.

**Set gates.** `python Deployment/stage_layout.py --check` (CI and `core2/stage.sh` run it) also fails
on the set itself, with no engine: every emissive ceiling fitting (`lum_*`) must read at or above the
floor below it (the floor lit by the ceiling bays in candela, as Unreal lights it with GI off), and no
two objects may overlap, wall-mounted dressing (`census` "signage": letters, roundels, the clock)
included. The author logs the same gates as `layout gate` lines.

**One file drives the camera too.** `Deployment/stage_layout.py` writes the layout's shots and beat
cues into `[/Script/CICompanion.CompanionStageSettings]` in `Config/DefaultGame.ini`, which the C++
stage director loads (a Sequencer asset is not an option: Lesson 6). The native authoring run rewrites
that section of the project it authors; the container authors a content-only project, so the cook
there reads the committed section, and `core2/stage.sh` refuses to stage when
`python3 Deployment/stage_layout.py --check` says it is stale. After editing shots or beats, run
`python Deployment/stage_layout.py` and commit both files.

`Content/Maps/CompanionStage.umap` in the repo is a **23 KB stub** (sky + ground). It exists only
so `Dockerfile.amd` has something to find; it is not the set. A build that does not author will
load it, log `No resident found; face driver inactive`, and open onto nothing — which is exactly
what the native build did until `BuildNative.ps1` learned to author. `BuildNative.ps1` therefore
fails loudly if the authoring did not report `SUCCESS`, did not place a resident, or produced a
map under 200 KB, and it puts the stub back afterwards: an authored `.umap` is a build artifact of
ONE engine version, and a 5.8-authored map cannot be loaded by the 5.5 cook.

## The resident

The default resident is **Epic's UE mannequin**, a placeholder. The MetaHuman **"Seo"** and the
Reallusion Character Creator **"Nia"** are still fully supported, but only when asked for by name:

| | UE mannequin (default) | MetaHuman "Seo" (opt-in) | Reallusion "Nia" (opt-in) |
|---|---|---|---|
| select it | nothing, or `-Resident Mannequin` / `MHLLM_RESIDENT=mannequin` | `-Resident MetaHuman` / `MHLLM_RESIDENT=metahuman` | `-Resident Nia` / `MHLLM_RESIDENT=nia` |
| input | none: `SKM_Manny` and `MM_Idle` from the engine's MoverTests plugin | FBX export: `fbx/SKM_MHC_Seo_BodyMesh.fbx`, `fbx/SKM_MHC_Seo_FaceMesh.fbx`, `fbx/MHC_Seo_Outfits.fbx` | the `CC_ControlRig_Nia` content folder |
| where it lives | the engine (`Engine/Plugins/Experimental/MoverTests`) | core-2 `/home/ci/mh-seo` (`/mh`); Windows `-MetaHumanDir`, `MHLLM_MH_DIR`, default `C:\devel\_mh-convert\out\seo` | core-2 `/home/ci/nia-content`; Windows `-NiaContent` / `MHLLM_NIA_CONTENT` |
| placed as | one `SkeletalMeshActor` playing `MM_Idle` | three `SkeletalMeshActor`s (Face, Body, Outfit) at one transform; **Body and Outfit attached to the Face** | one `SkeletalMeshActor` (`SK_Nia`) with her baked idle |
| tags | `CompanionFace` (no rig, so no `FaceProfile=`) | `CompanionFace`, `FaceProfile=ARKit` on the Face | `CompanionFace`, `FaceProfile=CC` |
| gate line | `resident: MANNEQUIN placed` | `resident: METAHUMAN placed` (else `METAHUMAN INCOMPLETE`) | `resident: NIA placed` — unchanged |
| licence | Licensed Technology under the Unreal Engine EULA; no AI restriction | Fab listing tagged **NoAI** | Fab listing tagged **NoAI** |

**Why the default is a mannequin.** Both characters the project has used are Fab content whose
listings read **"Allows usage with AI: No"** (the `NoAI` tag), checked 2026-11-12: *Character Creator:
Nia* by Reallusion Inc, and *Seo - Editable MetaHuman Character* by Groomchenko, which is what
`mhc_seo.mhpkg` is (same name and format, and its embedded thumbnail shows the listing's face). Under
the Fab EULA (§6(b)(vii), defined in §16(l)(i)), NoAI content may not be used "(a) in datasets utilized
by Generative AI Programs, (b) in the development of Generative AI Programs, or (c) as training inputs
to Generative AI Programs". An LLM companion that speaks through either of them is, at best, unclear
against (b), and an unclear licence is treated as a licence problem. Epic's MetaHuman terms do not
rescue Seo: the Unreal Engine EULA §1 excludes assets downloaded from Fab from the Licensed
Technology, so the seller's licence governs her, not Epic's MetaHuman FAQ. The mannequin is
Epic-provided content installed with the engine, which §1 does include, and the current EULA has no
AI clause (EULA update 22, October 30, 2026, removed §6(e)'s). The README's *Licensing* section has the
links and the quotes; [`NOTICE.md`](../NOTICE.md) is the licensing record.

**A face without a licence problem** needs a character the project may use with AI. The practical
route is a MetaHuman created in-house with MetaHuman Creator (not one bought on Fab): assemble it in
the interactive 5.8 editor and export it the way Seo was (below). The export and authoring path is
still written for Seo, so `03_export_fbx_from_GUI.py`'s source path, the `SKM_MHC_Seo_*` file names in
`author_diorama.py` and `BuildNative.ps1`, and the `/Game/Seo` destinations need to take the new
character's name. Written permission for AI use from Groomchenko or Reallusion is the other route.

Seo and Nia are **not even loaded** unless requested. A mannequin build does not read `MH_DIR` and
creates nothing under `/Game/Seo`. A checkout that once built with `-Resident Nia` keeps her staged in
`Content/CC_ControlRig_Nia` (gitignored, not deleted); the old script loaded her unconditionally and
would have made her the resident of every later build. A leftover copy is not cooked into another
resident's build either: `DefaultGame.ini`'s `MapsToCook` lists only `/Game/Maps/CompanionStage`, so
the cook takes that map and what it references, and those maps reference nothing of hers.
`m2_clean.sh` additionally refuses to cook a mannequin or MetaHuman build whose tree contains her, and
a mannequin build whose tree contains `Content/Seo`. The builds that cook a map without authoring it
(`CHECK_MAP` in both Dockerfiles, `BuildNative.ps1 -SkipAuthoring`) refuse the same, and cook an
authored map only beside the `Content/AUTHOR_RESULT.txt` its authoring run wrote: every line of the
result goes there too, ending with the saved map's `map sha256` and `SUCCESS`, so a map authored
before the switch, or before the NoAI prop gate, cannot pass for a current one
([`BUILD-AND-DEPLOY.md`](BUILD-AND-DEPLOY.md) §2.2).

**What the mannequin does not have.** A face. It carries no morph targets, so the face driver binds
and logs `0/N profile curves resolved` (the profile falls back to `CC`; nothing matches). Lip-sync,
expression, blinking and gaze are all inert, by design rather than by fault. Its eyeline is measured
from the mesh for the authored fallback camera, and the stage director frames it like any resident.

**Where her eyes are.** The stage director measures them at BeginPlay, in order: the eye bones
(`FACIAL_L/R_Eye` for a MetaHuman, `CC_Base_L/R_Eye` for Character Creator, `eye_l/r`), the `head` bone
plus 9 cm (the mannequin has no eye bones), then 93 % of the height of the resident plus its attached
actors. Her stature is that height / 0.93, and every resident-frame shot scales with it (STAGE-DESIGN.md
section 5). The log line `Stage plan from ...` names the method it used.

**Why the Face carries the tags.** The runtime treats the *tagged actor* as the whole character:
`UCompanionFaceSubsystem` drives the morph targets on its skeletal mesh, and `UCompanionStageDirector`
turns it during the opening and frames the shot on it. Seo's facial morph targets (849) are on the
Face mesh, so the Face is tagged; Body and Outfit are **attached** to it so the opening turn rotates the
whole figure rather than a head above a still body; and the director's last-resort eyeline is measured
over the resident *plus its attached actors*, because the face's own bounds are a head-sized box.
Before this, Seo carried no tag at all: the face driver reached her only through its "most morph
targets" fallback, and the stage director found no resident.

**Which rig the face assumes.** `MHLLM_FACE_PROFILE` if set, else the resident's `FaceProfile=` tag,
else `CC` — so an old Nia map with no profile tag behaves exactly as before.
`docker-compose.face.yml` now leaves the variable empty for that reason; it used to pin `CC`.

**Producing the Seo export.** The export is not committed (it is a build input, like the prop GLBs).
core-2's authoring mounts it from `/home/ci/mh-seo`; if that folder is intact, copying it is the
quickest way to a local one. To make it from source, `mhc_seo.mhpkg` (a UE 5.6 MetaHuman
EditableCharacter) has to be imported and **assembled in the interactive 5.8 editor** and exported
with `Deployment/metahuman/03_export_fbx_from_GUI.py` from *Tools > Execute Python*, which writes
`C:\devel\_mh-convert\out\seo\fbx\`. Both steps deadlock headless — see `HANDOFF-METAHUMAN-SKIN.md`.
`BuildNative.ps1` checks for all three FBX files before it launches the editor and names the missing
ones; the outfit is not optional, because MetaHuman removes the body geometry under clothing.

**What the opt-in MetaHuman resident does not have yet** — none of this blocks a build, all of it is
visible:

- **The face mapping is unverified.** Her face is tagged `FaceProfile=ARKit`, the profile this codebase
  designates for MetaHuman faces, but nobody has listed the morph-target names on the *FBX-exported*
  `SKM_MHC_Seo_FaceMesh`. A MetaHuman face is normally driven through RigLogic (joints plus corrective
  shapes); an FBX round-trip keeps the blend shapes, possibly under DNA-style names rather than ARKit
  ones. The runtime log settles it: `Face driver bound to '…': R/N profile curves resolved`, followed
  by either `Face profile 'ARKit': all N curves resolved on '…'` or the list of missing curves. If R is
  0, the resident needs a MetaHuman-specific profile.
  `Deployment/metahuman/09_probe_face_rig.py` lists a mesh's morph names and counts the ARKit and CC
  samples it carries; set `MHLLM_PROBE_MESH` to the Seo face under `/Game/Seo/Face` (it defaults to
  Nia) and `MHLLM_PROBE_OUT` to where the report should go.
- **No idle animation.** She stands in the export's bind pose. Nia ships a baked idle; Seo does not.
- **Flat skin.** The constant-colour `M_SeoSkin` instances from the original port, not MetaHuman's
  skin shader (`HANDOFF-METAHUMAN-SKIN.md` §4 ranks the fixes).
- **Eyeline fallback.** The authored fallback `StageCamera` uses the layout's `resident.eyeZFallback`
  (158.1 cm, the design's 170 cm reference) because only Nia's and the mannequin's heights are measured
  in the script. The runtime director, which owns the shot, measures her eyes.

**Prop slots that only take CI-authored models.** `ibm-1401-cpu.glb`, `ibm-3178-terminal.glb` and
`ibm-3350-disk.glb` used to hold maxdragon's CC BY 4.0 Sketchfab models, whose pages carry the `noai`
tag; Sketchfab's Terms of Use bar NoAI content from use "as inputs to generative AI programs". The
script now reads each of those files' glTF `asset.copyright` before importing it and accepts it only
if it names Companion Intelligence, which is what `Deployment/blender/make_period_props.py` writes into
its replacements. Anything else is never imported: the result shows `prop REFUSED …`, the slot and any
other prop that reuses it stay empty, and `NoAI prop slots: N of 3 refused` sums it up. Neither
`BuildNative.ps1` nor `m2_clean.sh` fails on it, so a prop folder that still holds the old downloads
gives a licence-clean set with gaps rather than no build. `ibm-2401-tape.glb` (also NoAI-tagged, with
no CI replacement) is refused by name if a layout ever lists it. The README's *Licensing* section has
the sources, and [`CREDITS.md`](../CREDITS.md) the CC BY attributions.

## Assets: scavenged vs authored

The 1979 set places eight GLBs from the licence-clean set, 14 times: the CI-authored 1401 processing
unit, a string of four 3350s and four 3277 display stations (one slot each, placed several times), and
five CC BY 4.0 models by museudocomputador, credited in [`CREDITS.md`](../CREDITS.md): the RAMAC 305
(as the 1405), the plugboard, the Xerox Alto, the SRI mouse and the Kenbak-1. Everything else is a
true-size blockout, and each blockout's note is the spec for the CI prop that will replace it
(STAGE-DESIGN.md section 9). Props are imported as **GLB, never FBX**. Blender's FBX exporter only
writes textures wired to a Principled BSDF, so several machines exported with no textures at all and
rendered as blank white blocks.

**Three slots are authored because their Sketchfab models carried a NoAI tag.** The models that
used to fill `ibm-1401-cpu.glb`, `ibm-3178-terminal.glb` and `ibm-3350-disk.glb` are CC BY 4.0,
but their pages carry Sketchfab's NoAI meta tag. Sketchfab's Terms of Use bar NoAI-tagged content
from use as inputs to generative AI programs, and this product is an AI companion, so
`Deployment/blender/make_period_props.py` builds replacements under the same file names:

```
blender --background --python Deployment/blender/make_period_props.py -- --out <glb dir>
```

- **IBM 1401 Processing Unit**, 4-cube: 1.475 x 0.780 x 1.527 m (Ingenium artifact 1979.0133.001).
  The front follows Figure 8 of IBM's 1960 General Information Manual: four framed cubes, a console
  in the upper-left cube, grey ridged panels and a blue band across the top.
- **IBM 3277 Display Station Model 2** with keyboard: 0.457 x 0.749 x 0.483 m (IBM SY27-2349-4).
  The slot keeps the `3178` file name, but the 3178 is a 1983 terminal. The 3277 (1971) is what
  a 1970s machine room had on its desks.
- **IBM 3350 dual-drive unit**, with a 3-inch end cover on each side: 1.219 x 0.851 x 1.181 m
  (IBM GC22-7064-10). The front follows Figure 1 of the 3350 Reference Manual (GA26-1638-2): a door,
  a smoked window and a top-mounted operator panel per drive.

Each prop faces -Y like the model it replaces. Each is ONE mesh object, because `import_prop()` takes
the first StaticMesh it finds (a scavenged file of several meshes is merged at import instead; see
`props` above). Lamps and phosphor get a bright base colour as well as emission, because the
`M_PropGLTF` re-parent drops emission; where a screen has to glow in Unreal, the layout lays an
emissive blockout on it (the 3277s' `*_phosphor` panels, 1.5 mm in front of the GLB's screen). The
script fails if any exported bounding box drifts more than 0.5 mm from its published envelope, and two
builds are byte-identical.

The stage layout places the CPU at its true size (`scale` 1.0, origin on its location), and the RAMAC
at the scale that makes its cabinet about 170 cm. A layout prop given `targetCm` instead is normalised
from its measured bounds, the way the old hall filled each model to its slot's envelope.

The tape units are **authored in Blender**, not scavenged, because no scavenged prop was an IBM 729.
`Deployment/blender/make_ibm729.py` rebuilds it: real proportions (0.76 x 0.74 x 1.80 m), hollow
off-white shell, chrome frame, dark-backed open reel bay, IBM-blue lower panel, amber supply reel,
white take-up reel, CI-mint status lamps. The stage does **not** place it: its face is wrong for the
period (an open reel bay, the reels below the bay, 80 x 79 x 194 cm), and the six 729 blockouts (the
CHM 74 x 86 x 175 cm face: cream fascia, smoked reel door, blue file reel and dark take-up reel,
2700 K lamp window) are the spec for its re-author. The re-authored GLB keeps separately pivoted,
`TapeReel`-tagged reels.

```
blender --background --python Deployment/blender/make_ibm729.py -- --out <glb dir>
```

Every dimension in that script is an explicit constant rather than a viewport adjustment, and the
output is verified against the shipped `ibm-729-tape.glb`: node names and transforms, all seven
materials (including the lamp's emissive strength), and all eleven primitives' vertex counts,
index counts and bounding boxes match to four decimals. The prop is reproducible from source, not
just re-creatable by hand.

**The reels are separate objects with their own origins.** This is the load-bearing decision. A
reel baked into the shell looks identical in a still frame and can never turn; spinning tape is
the most recognisable motion in a machine room of this era. Keeping them separate is what makes
the hall able to move at all. For the same reason the chrome **hub caps belong to the body, not
to the reels** — a cap that turned with its reel would put a rotating specular highlight on all
24 of them and strobe the whole hall.

## Assets: generated

Three pieces of the set are **synthesized by scripts in this repo** rather than sourced. All are
stdlib-only Python, and all are committed so a fresh checkout can author the complete set:

| output | generator | why generated |
|---|---|---|
| `Deployment/audio/machine_room.wav` | `Scripts/MakeAmbience.py --mode score` | 44.1kHz, 32s. The bed, non-spatialized at the camera. |
| `Deployment/generated/S_RoomTone.wav` | `Scripts/MakeAmbience.py --mode roomtone` | 48kHz (UE's mixer rate, so no resample), 20s. A positioned emitter out in the hall. |
| `Deployment/generated/T_CRT_Vector.png` | `Scripts/MakeCRTTexture.py` | 512x384 emissive scan-line trace for the old hall's CRT screens. The 1979 set does not import it (its 3277 screens are emissive overlays); the layout's `texture_emissive` material type still can. |

Generating rather than sourcing buys three things: no licence question about shipping them, the
freedom to tune them to this specific room, and — for the loops — a **computed** join instead of
an editor crossfade. Every partial sits at an exact integer number of cycles per loop, so each
sine ends where it began.

Noise cannot be cycle-locked that way. The score hides its noise seam under the sines; the room
tone has no pad to hide behind, so its noise is filtered **circularly** — the one-pole is run
twice round the same periodic input and only the second lap is kept, making the filter state at
sample 0 the state it ends on. Measured, the room tone's wrap is **0.92x its own median
sample-to-sample step**: the join is not merely quiet, it is indistinguishable from any other
sample transition. Judge a loop by that ratio, never by raw `|x[0] - x[-1]|` — a brighter bed
scores "worse" on the raw number while looping better.

The CRT texture exists because a flat-colour screen reads as **painted plastic**. With GI off,
the set is lit entirely by explicit lights, so a screen that is merely bright-coloured renders
dimmer than the cabinet beside it and looks switched off. What sells a CRT is the raster:
alternating bright and dark lines, a trace with a bloom around it, a phosphor black that is not
quite black, and a persistence ghost trailing the beam so a still frame implies motion. The trace
is drawn at six sub-pixel samples per column — a per-pixel walk breaks into dashes on the steep
flanks and reads as a corrupted signal. Phosphor colour is CI mint `#58ebbf`, authored in **sRGB
display space** because the material samples it as an sRGB base colour and multiplies it into
emissive; converting to linear here would double-apply the transform.

## Reels

`UTapeReelSpinner` finds actors tagged `TapeReel` and turns each about its YAWED Y axis whatever its
roll, so a reel must have its axle there: every tick it sets `FQuat(axle, angle) * rest` from the
rotation the reel had at BeginPlay and an angle it accumulates itself. It used to add to the Pitch of
the rotation it read back, and Unreal hands that back NORMALISED (pitch in [-90, 90]), so every reel
crept to a quarter turn and stopped. The Roll animation before that turned around the unrelated actor
X axis and made the discs wobble edge-on.

In the 1979 set the 30 reels are blockout cylinders, two on each 729 and each 3420. A cylinder's
length runs along the engine mesh's local Z, so `author_diorama.py` turns every cylinder with roll -90
and, for an axis along the blockout's X, yaw +90: the axle then lies on the actor's yawed Y axis and
the spinner turns each reel about its own hub. The reels are Movable (set before the mesh is assigned,
or the cook freezes them), and each reel's three flange windows are attached to it so they turn with
it; the chrome hub caps are not, because a cap that turned with its reel would put a rotating
highlight on every unit and strobe the wall. The reels of the 3420 behind her left shoulder in HOME
also carry `StageReact.Tape` for the set-state work.

A GLB with separately pivoted reels (the old hall's 729, and the re-authored one to come) is placed
through the prop's `reels` list: Interchange does not keep a node's translation when it imports the
node as a standalone `StaticMesh`, so the author restores each hub offset, rotated with the cabinet.
Spawning at the cabinet origin once left the reels floating outside the cabinets, and a positive
`Tape reels spinning` count did not prove that their placement or visible rotation was correct.

Rates are deliberately **not uniform**. A hall turning in lockstep reads as a screensaver; real
machines run independent jobs. Each reel's speed (90-240 deg/s) and direction are seeded from its
world position, so the variation is deterministic across runs — a captured frame is reproducible —
but visually uncorrelated between neighbours.

## Colour

A cool machine room with a warm human island (STAGE-DESIGN.md sections 8 and 9): one vermilion feature
wall (`#B5441A`, the only saturated plane), warm off-white walls (`#E6E1D3`), a raised floor of
vinyl-faced panels (`#E0DCD1`), the 1401 line-up in Deep Charcoal with a Sky Blue accent (`#2E302C`,
`#3D86B8`), the 1970s strings in IBM classic blue and warm grey (`#2B5D94`, `#D6D3CA`), continuous
4100 K troffer rows in a white acoustic-tile ceiling, and 2700-3000 K practicals, so skin is the warmest
mid-tone in the frame.

**No brand colour in the world.** The old hall painted its side walls CI teal and lit its depth with a
sage rim, and claimed that split gave the references their depth; it did not, and it dated the room to
no year at all. The set carries the GDD's own fictional facility name, LONGVIEW LAB, BAY 7, EST.
1959, in period letters; CI appears in the lobby, the HUD and one teal brooch on the resident
(GDD.md:984). The CI-Common wordmark and tokens (phthalo `#0a6358`, sage `#c5e8dc`, navy `#041620`,
mint `#3ae7ba`) stay for those surfaces; `Deployment/generated/ci-logo-white-banner.png` is no longer
placed in the room.

**Convert sRGB to linear before use.** The layout writes colours as sRGB hex and the author converts
them for the material instances. Pasting hex straight into a `LinearColor` gives washed-out colour,
and the symptom reads as "the colour looks wrong" rather than as a colour-space mistake.

## Traps already paid for

- `les.new_level()` **refuses to overwrite**. It returns `False` with
  "Failed to validate the destination. There's already an asset at the destination", and the run
  then authors 60-odd actors into the UNTITLED transient level, which `save_current_level` cannot
  write. The container never saw this because it authors into a project whose `Content/` was just
  `rm -rf`'d. Anywhere the stub already exists, the whole run produced no map — and still printed
  `SUCCESS`, because that line was unconditional. Both are fixed; the map path is deleted first
  and `SUCCESS` now means a `.umap` reached disk.
- `import_prop()` returns `(mesh, texture_count)`. Binding the **whole tuple** is what left the
  hall with no tape machines: `if t_body:` is true for a tuple, so it read as working, and
  `set_static_mesh` then raised a nativize `TypeError` that the outer `except` swallowed into a
  one-line warning — before a single reel actor was spawned. That was `Tape reels spinning: 0`.
- `paint()` derived material names from colour, so two identically-coloured walls asked
  `create_asset` for the same package path twice. The second call trips a **fatal C++ assertion
  that Python's try/except cannot catch** — the commandlet dies with `appError` despite the error
  handling written to prevent exactly that. Explicit material names must now carry one definition
  (the author stops on a second, different one), and derived instances are named from their values
  and created once.
- Portrait needs TWO changes in step: `-ResX/-ResY` **and** releasing the camera's aspect-ratio
  constraint. Setting only the resolution yields a 16:9 image letterboxed inside a 9:16 canvas.
- Camera height is derived from her measured eyes (bones, then bounds), not hand-picked. A guessed z
  cut her head off.
- GI is **disabled** on this stream, so a coloured wall cannot bounce on its own. Colour bounce is
  placed as explicit point lights.
- **The camera cannot be moved from outside the app, by any route.** Three separate mechanisms have
  to agree and currently do not:
  1. `MHLLM_FREE_LOOK=1` used to un-ignore input while `LockPlayerView` still called
     `SetViewTarget(Camera)` on a camera that never moves. Input was live the whole time and the
     pawn really was walking around the set; the picture just did not come from it. Fixed — the
     pawn now receives the view and starts on the call framing.
  2. Even so, nothing reaches the pawn: the app arms the chat box on connect ("Chat input armed:
     cursor shown, keyboard focus on the text box"), so Slate owns cursor and keyboard. Mouse
     deltas and WASD from a browser go to the text field, not to look/move.
  3. The authored `StageCamera` used to auto-activate over the director's camera, so the flythrough
     advanced in logs while the stream stayed static. Its auto-activation has now been removed, and
     the director's opening move is default-on. **This code-path still needs a live Pixel Streaming
     verification** once a compatible Unreal editor/build host is available.
  For now, review non-hero angles by GPU-rendering the authored map offscreen with the camera moved
  in Python (no cook needed, and `ExposureOverride` makes renders comparable). Note this needs an
  engine image with Mesa/RADV installed — the stock `ghcr.io/epicgames/unreal-engine:dev-5.5` ships
  no Vulkan ICD, so `-Vulkan` finds no device and the editor dies right after `LogVulkanRHI`.

## Exposure — the room was never lit wrong, it was graded wrong

> **The stage's value is solved on Unreal captures.** `meta.exposureEV100` is re-solved on a captured
> HOME frame whenever the rig changes (`meta.exposureNote` records the bracket); the 9.4 below was the
> rc1 hall's. Everything below is the history of how the pinning came about, and it still holds.

`Config/DefaultEngine.ini` carries
`r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True`. That single line changes the
**units** of the PostProcessVolume's `AutoExposureMinBrightness` / `MaxBrightness` from average
scene luminance to **EV100**. The values that sat in `author_diorama.py` for months, `0.03` and
`8.0`, are UE**4**'s luminance-space defaults. Read as EV100 they are not "wide bounds" — they are
a hard ceiling of EV100 8.0, i.e. an average scene luminance of `1.2 * 2^8` = 307 cd/m². This room
meters near EV100 11.5 (~3500 cd/m²). Auto exposure asked for 11.5, got clamped to 8.0, and every
frame rendered **3.5 stops — 11x — overexposed**.

That is the whole "white haze", and the giveaway is in the histogram rather than the mean:

| packaged Win64 build, 720x1280, HUD off | before | after |
|---|---|---|
| mean luminance | 221.5 | 122.9 |
| median | 226.1 | 117.8 |
| 5th percentile | 191.2 | 67.6 |
| 95th percentile | 245.1 | 187.7 |
| max | 252.2 | 222.9 |
| std dev | 20.7 | 37.7 |
| %px >= 250 | 0.27% | 0.00% |
| %px >= 240 | 14.7% | 0.00% |
| %px >= 200 | 90.1% | 1.9% |
| %px <= 40 | 0.04% | 1.7% |

**The 5th percentile is the number to read.** At 191 the *darkest* five percent of the frame was
brighter than middle grey: the entire image lived in the top fifth of the range, which is what a
white haze is. The orange back wall looked like the one surviving colour only because its albedo
is (0.62, 0.20, 0.09) — its RED channel was pinned at 255 like everything else and green and blue
were what was left. It was clipping too.

Two structural faults fed the same result and are fixed alongside it:

- **The ceiling grid hung 60cm under the ceiling.** Point lights at z=430 under a 0.90-albedo slab
  whose underside is at 490 put `I/0.6²` on the ceiling and `I/4.3²` on the floor — **51x** more
  on the ceiling. The single brightest surface in a machine room was its ceiling. At z=330 that
  ratio is 4.3:1. The fittings that read as *sources* are the emissive troffers at 492; they are
  unchanged, and per the measurements below they never lit that band anyway.
- **Attenuation radii spanned the whole room.** At r=1500..2600 in a 28m hall, every "accent" lit
  every surface, so twenty-odd lights summed into one omnidirectional wash — the flat evenly-lit
  box. This is a no-GI-specific trap: with bounce light you spend a few big sources and let the
  room distribute them; without it the temptation is to keep adding sources until nothing is dark,
  which is the same thing as deleting the shading. Held to r~850..1200 each light lights its bay
  and the hall recedes. Measured on the near cabinets, left vs right went from 240 : 229 (1.05:1,
  no falloff) to 169 : 108 (1.6:1).

**Exposure is now PINNED** — `min == max == EV100 9.4` in the volume — and that is deliberate, not
a shortcut:

1. `r.EyeAdaptation.*` console vars do **not** rescue it. Verified on the packaged build: EV
   overrides of 11/12/13/14 via `-ExecCmds` moved the frame by less than 0.2/255, because a volume
   that overrides exposure wins. The lever has to be the volume. (This is the same dead end the
   previous attempt hit, and why `gpu_preview.sh` hard-codes `ExposureOverride 14` and still
   cannot be trusted to judge exposure.)
2. One camera, one fixed shot, one light rig — there is nothing for adaptation to adapt to, so
   every stop it can move is a stop of unpredictability, not of robustness.
3. Auto exposure silently **re-grades a lighting change away**. See the troffer numbers below:
   halving the emissive made the frame come back *brighter*. Pinned, luminance means something
   across builds, and it converges on frame **zero** — which is what finally makes a single
   offscreen render trustworthy.

`meta.exposureEV100` and the light intensities are **one setting solved together**. Change either and
re-measure: capture a frame and land the mean near 120/255 with nothing over 240. On the rc1 hall,
9.7 came back at mean 112 and lost the highlights; 8.0 was the bug, at 221.

### Verifying a lighting change in two minutes

The set is pure content, so no cook is needed to look at it:

```
Deployment/author_diorama.py via UnrealEditor-Cmd -run=pythonscript   # ~20 s
UnrealEditor.exe <proj> /Game/Maps/CompanionStage -game -ResX=720 -ResY=1280 -windowed
```

Screen-capture the client area (a D3D swapchain does not render into a `PrintWindow` DC; copy it
off the desktop instead) and measure. That path was checked against the packaged build and is
**pixel-equivalent**: mean 221.6 vs 221.5 before, 122.9 vs 122.9 after. `MHLLM_HUD=0` keeps the
chat overlay out of the measurement; `MHLLM_OPENING_SHOT=0` gives the static call framing (HOME
after the lobby hold).

What that loop is **not** good for: `Content/Maps/CompanionStage.umap` is left authored on disk
afterwards, and `BuildNative.ps1` will then back up and restore *that* instead of the committed
stub. Check `git status` before committing.

### What can and cannot judge this

| path | GI off | same `Config/` | trustworthy for exposure |
|---|---|---|---|
| packaged Win64 (`BuildNative.ps1`) | yes | yes | **yes** |
| `UnrealEditor -game` on this project | yes | yes | **yes** (verified pixel-equal) |
| editor viewport | no (Lumen) | n/a | no |
| `core2/gpu_preview.sh` | yes | **no** — renders in `authorproj`, whose config has no `ExtendDefaultLuminanceRange`, so the volume's EV100 numbers are read as luminance | no |
| the live stream | yes | yes | yes, but only after a fresh cook |

## Measured, not remembered

These numbers are from the rc1 machine hall, which the stage set replaces; they are kept for the
method, not as a description of the current room.

Numbers from captured stream frames (720x1280 portrait), 2026-07-31. **These came off the
deployment that was running that day, which is NOT this source tree** — a frame captured from the
live stream on core-2 while writing the exposure section above shows black untextured cabinets and
measures mean 102.3, so that container predates the current authoring script. Treat the table as a
record of that build, not as evidence about this one; the exposure numbers above were taken from a
packaged build of the current source. **Exclude the top 34 and
bottom 28 rows before measuring anything**: the chat HUD is drawn by the app and is therefore *in*
the video, and its pure-white prompt text puts `max luminance = 255` in every single frame. That
one artefact is why the ceiling was reported as "blowing out" — the brightest thing in the actual
render was 211.

| build | troffer emissive | frame mean | render max (HUD excluded) | %px >= 250 | ceiling band mean | ceiling / frame | troffer fittings mean |
|---|---|---|---|---|---|---|---|
| before | 7.0 | 100.4 | 211 | 0.00% | 141.3 | 1.41x | 192.1 |
| after  | 3.8 | 101.3 | 213 | 0.00% | 146.3 | 1.45x | 193.1 |

Three things follow, and the third is the useful one:

- **The ceiling has never clipped.** Not at 7.0, not at 3.8. `%px >= 250` is 0.00 in both, and peak
  render luminance is ~211/255 with roughly 4-5% of the ceiling band above 200. It reads pale and
  washed because it is a large, evenly-lit, near-white surface — not because it is saturating.
- **Halving the troffer emissive did essentially nothing.** A 46% cut moved the troffer fittings'
  own pixels from 192.1 to 193.1. That is not a small effect, it is *no* effect.
- **Therefore the troffer emissive is not what lights that band.** The pale ceiling comes from the
  nine-point ceiling grid at z=430 (intensity 3200, radius 2600) washing a 0.90/0.90/0.88 slab
  70cm above it, plus the hall troffer-run lights. The emissive material only makes the fittings
  read as *sources*; it contributes almost nothing to the surface around them. Anyone trying to
  darken the ceiling must go at the **point-light grid**, not the emissive value.

Auto-exposure is why the "after" numbers are slightly *higher*: the stream runs histogram
auto-exposure with wide bounds, so it re-grades the whole frame whenever the light budget moves.
Absolute luminance across two builds is therefore not a clean comparison — compare a ratio, or
render with `r.EyeAdaptation.ExposureOverride` fixed.

## Next
- **Watch it on the stream.** The set is authored headlessly in Unreal and captured from the previz
  cameras (STAGE-DESIGN.md section 11); capture a 60 fps loop of ARRIVAL and HOME from the real stream
  and check the perforated floor panels' 3 cm grid for shimmer on the VP8 stream.
- **Replace the blockouts with CI props**, from their notes: the 3420 and 3803 (the left-wall string),
  the 729 re-author (`make_ibm729.py`), the 1402, 1403 and stand, 1406, the 1407 desk and typewriter,
  the 029 card punch, the Selectric II, the oscilloscope, the air unit, the desks and chairs.
  Re-texture `commodore-pet.glb` (its one texture is scrambled) before it replaces the PET stand-in.
  Licence a slab-serif font for the LONGVIEW letters.
- The room tone's `AmbientSound` has **no attenuation asset**, so a sound placed deliberately among
  the machines plays at the same level everywhere. It is mixed low enough not to matter today, but
  the position currently buys nothing. The score bed still hums at 50 Hz against the room tone's 60.
- Every summary line reports a count that a failure would change (`blockouts placed: N of M`,
  `TargetPoints placed: N of M`, `self-check: ...`); keep it that way. A line that prints the same
  thing whether or not anything was placed is the shape of lie `Tape reels spinning: 0` was.
