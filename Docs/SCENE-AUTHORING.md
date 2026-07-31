# Scene authoring — the 1970s machine hall

How the diorama is built, why it is built that way, and what to reach for next.

## The constraint everything follows from

The map is authored **headlessly by Python in a content-only project** that cannot reference
plugin C++ or embed a Blueprint (HANDOFF.md Lesson 6). So the `.umap` stays a purely visual
asset — plain `StaticMeshActor`s and `SkeletalMeshActor`s — and every behaviour is attached at
runtime by a `UWorldSubsystem` that finds actors by **tag**.

Tags, never labels: actor labels are editor-only and do not survive a cook, so a label-based
lookup works in the editor and silently finds nothing in the shipped build.

Subsystems in play: `UCompanionFaceSubsystem` (face), `UCompanionRuntimeSubsystem` (brain, voice,
HUD), `UCompanionStageDirector` (opening shot), `UTapeReelSpinner` (reels).

## Who runs the authoring

`Deployment/author_diorama.py` is the **only** definition of the set. Two things run it, and both
must, or you ship an empty room:

| consumer | how | inputs |
|---|---|---|
| the Linux cook on core-2 | `Deployment/core2/run_author_m2.sh` -> `dev-5.5` container, `-nullrhi` | mounted at `/glb`, `/mh`, `/audio`, `/nia` |
| the native Windows build | `Scripts/BuildNative.ps1` -> installed `UnrealEditor-Cmd.exe`, `-nullrhi` | `MHLLM_GLB_DIR` / `MHLLM_MH_DIR` / `MHLLM_AUDIO_DIR`, Nia staged into `Content/CC_ControlRig_Nia` |

The script reads every external path through `MHLLM_*` environment variables whose **defaults are
the container mount points**, so the server pipeline is unchanged and the local build simply
points them at Windows paths. Never hard-code a new input path — add it to that block, or the
native build silently diverges from the cook.

`Content/Maps/CompanionStage.umap` in the repo is a **23 KB stub** (sky + ground). It exists only
so `Dockerfile.amd` has something to find; it is not the set. A build that does not author will
load it, log `No resident found; face driver inactive`, and open onto nothing — which is exactly
what the native build did until `BuildNative.ps1` learned to author. `BuildNative.ps1` therefore
fails loudly if the authoring did not report `SUCCESS`, did not place a resident, or produced a
map under 200 KB, and it puts the stub back afterwards: an authored `.umap` is a build artifact of
ONE engine version, and a 5.8-authored map cannot be loaded by the 5.5 cook.

## Assets: scavenged vs authored

Twelve hero props are Sketchfab GLBs. They are imported as **GLB, never FBX** — Blender's FBX
exporter only writes textures wired to a Principled BSDF, so several machines exported with no
textures at all and rendered as blank white blocks.

The tape units are **authored in Blender**, not scavenged, because no scavenged prop was an IBM
729 and the hall was consequently 20 featureless slabs. `Deployment/blender/make_ibm729.py`
rebuilds it: real proportions (0.76 x 0.74 x 1.80 m), off-white shell, chrome frame, dark glass
door over the reel bay, IBM-blue lower panel, amber supply reel, white take-up reel, CI-mint
status lamps.

**The reels are separate objects with their own origins.** This is the load-bearing decision. A
reel baked into the shell looks identical in a still frame and can never turn; spinning tape is
the most recognisable motion in a machine room of this era. Keeping them separate is what makes
the hall able to move at all.

## Reels

`UTapeReelSpinner` finds actors tagged `TapeReel` and rotates them on Roll.

Rates are deliberately **not uniform**. A hall turning in lockstep reads as a screensaver; real
machines run independent jobs. Each reel's speed (90-240 deg/s) and direction are seeded from its
world position, so the variation is deterministic across runs — a captured frame is reproducible —
but visually uncorrelated between neighbours.

## Colour

Two palettes are deliberately split by DEPTH rather than blended:

- **Warm at eye level** — Olivetti amber, lands on faces and cabinet fronts
- **CI-cool in the distance** — teal side walls, cool rim deep in the hall

That warm-near / cool-far split is what gives the reference photograph its depth, and it lets the
brand register as *light* rather than only as paint. IBM orange keeps the hero back wall; it is
the colour the reference is built around.

Brand tokens come from `companionintelligence/CI-Common` (`assets/brand.config.json`,
`docs/colors.md`): CI teal `#0f717a`, light `#abd4d8`, navy `#041620`, mint `#58ebbf`.

**Convert sRGB to linear before use.** Pasting hex straight into a `LinearColor` gives a washed-out
brand, and the symptom reads as "the teal looks wrong" rather than as a colour-space mistake.

CI mint doubles as period CRT phosphor — `#58ebbf` sits almost exactly where a 70s phosphor does,
so the brand accent and the era cue are the same colour with no compromise.

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
  handling written to prevent exactly that. Names are explicit now.
- Portrait needs TWO changes in step: `-ResX/-ResY` **and** releasing the camera's aspect-ratio
  constraint. Setting only the resolution yields a 16:9 image letterboxed inside a 9:16 canvas.
- Camera height is derived from measured mesh bounds, not hand-picked. A guessed z cut her head off.
- GI is **disabled** on this stream, so a coloured wall cannot bounce on its own. Colour bounce is
  placed as explicit point lights.

## Next

- **Check the exposure on a captured frame.** The first native build showed the hall washed to
  near white — walls, cabinets and floor all clipping, only the orange back wall holding colour.
  The grade was tuned against the stream's no-GI render, so it has to be re-checked against a
  captured frame on each target rather than by eye in the editor.
- `T_CRT_Vector.png` and `S_RoomTone.wav` are read from the GLB directory but exist only on
  core-2, so the native build skips the CRT scan-line screens and the room tone entirely. They
  should be generated into the repo like `machine_room.wav` is (`Scripts/MakeAmbience.py`).
- `Deployment/blender/make_ibm729.py` is referenced above but the directory holds only `.keep` —
  the 729 GLB cannot currently be rebuilt from this repo.
- Author scan-line art on the CRTs (currently flat emissive colour).
