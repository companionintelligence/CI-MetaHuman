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
| the native Windows build | `Scripts/BuildNative.ps1` -> installed `UnrealEditor-Cmd.exe`, `-nullrhi` | `MHLLM_GLB_DIR` / `MHLLM_MH_DIR` / `MHLLM_AUDIO_DIR` / `MHLLM_GEN_DIR`, Nia staged into `Content/CC_ControlRig_Nia` |

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
exactly as before. Both import sites now also log when they find nothing, and `BuildNative.ps1`
fails the build if `CRT vector displays placed` or `room tone placed` is missing from
`AUTHOR_RESULT.txt`.

`Content/Maps/CompanionStage.umap` in the repo is a **23 KB stub** (sky + ground). It exists only
so `Dockerfile.amd` has something to find; it is not the set. A build that does not author will
load it, log `No resident found; face driver inactive`, and open onto nothing — which is exactly
what the native build did until `BuildNative.ps1` learned to author. `BuildNative.ps1` therefore
fails loudly if the authoring did not report `SUCCESS`, did not place a resident, or produced a
map under 200 KB, and it puts the stub back afterwards: an authored `.umap` is a build artifact of
ONE engine version, and a 5.8-authored map cannot be loaded by the 5.5 cook.

## Assets: scavenged vs authored

Eight hero props are Sketchfab GLBs under CC BY 4.0, credited in [`CREDITS.md`](../CREDITS.md).
The 1401 processing unit, the terminal, the 3350 and the tape units are authored in Blender by
scripts in this repo. All of them are imported as **GLB, never FBX**. Blender's FBX exporter only
writes textures wired to a Principled BSDF, so several machines exported with no textures at all
and rendered as blank white blocks.

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

Each prop faces -Y like the model it replaces, so the yaws in `PROPS` and `HALL` still hold. Each
is ONE mesh object, because `import_prop()` takes the first StaticMesh it finds. Lamps and
phosphor get a bright base colour as well as emission, because the `M_PropGLTF` re-parent drops
emission. The script fails if any exported bounding box drifts more than 0.5 mm from its published
envelope, and two builds are byte-identical.

`place_prop()` still normalizes each of these props to its `PROPS`/`HALL` target. They therefore
fill the same envelope as the models they replaced, not their true size. The 3350, for example,
comes out 1.50 m wide and 1.45 m tall instead of 1.22 x 1.18 m, and the hero 3277 at two-thirds
of its real size. To show them at true scale, set each target to the model's largest real
dimension (153, 75 and 122 cm). Do that with a capture, because it changes the composition.

The tape units are **authored in Blender**, not scavenged, because no scavenged prop was an IBM
729. `Deployment/blender/make_ibm729.py` rebuilds it: real proportions (0.76 x 0.74 x 1.80 m),
hollow off-white shell, chrome frame, dark-backed open reel bay, IBM-blue lower panel, amber supply
reel, white take-up reel, CI-mint status lamps. The authored 729 replaces the legacy IBM 2401
placeholder at each tape-machine station in the hall.

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
| `Deployment/generated/T_CRT_Vector.png` | `Scripts/MakeCRTTexture.py` | 512x384 emissive scan-line trace for the CRT screens. |

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

`UTapeReelSpinner` finds actors tagged `TapeReel` and rotates them on Pitch. The reel disc lies in
the cabinet's local XZ plane, so its axle is local Y. The previous Roll animation turned around
the unrelated actor X axis and made the discs wobble edge-on.

Each reel is a separate GLB node with its pivot at the hub, but its node translation is not retained
when Interchange imports it as a standalone `StaticMesh`. `author_diorama.py` therefore restores
the authored hub offsets (16/17cm to either side, 25cm behind the front, 126cm above the floor)
before rotating the machine banks across the aisle. Spawning at the cabinet origin left the reels
floating outside the cabinets; a positive `Tape reels spinning` count did not prove that their
placement or visible rotation was correct. The 729 asset now has a hollow shell, dark-backed open
bay, and eight-spoke reels with separate flanges and a dark tape pack. This keeps the animated
parts visible inside the machine silhouette instead of layering solid discs over the cabinet.

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
`docs/colors.md`): phthalo `#0a6358`, sage `#c5e8dc`, navy `#041620`, mint `#3ae7ba`.
The approved white-banner wordmark is staged from `CI-Common/assets/logos/rendered/` as
`Deployment/generated/ci-logo-white-banner.png`; Unreal imports it onto a navy plaque behind the
resident. The retro Blender stage packs that same logo PNG and its embedded GLB images into the
`.blend` file.

**Convert sRGB to linear before use.** Pasting hex straight into a `LinearColor` gives a washed-out
brand, and the symptom reads as "the teal looks wrong" rather than as a colour-space mistake.

CI mint doubles as period CRT phosphor — `#3ae7ba` sits almost exactly where a 70s phosphor does,
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

`EXPOSURE_EV100` and the light intensities are **one setting solved together**. Change either and
re-measure: capture a frame and land the mean near 120/255 with nothing over 240. 9.7 came back at
mean 112 and lost the highlights; 8.0 was the bug, at 221.

### Verifying a lighting change in two minutes

The set is pure content, so no cook is needed to look at it:

```
Deployment/author_diorama.py via UnrealEditor-Cmd -run=pythonscript   # ~20 s
UnrealEditor.exe <proj> /Game/Maps/CompanionStage -game -ResX=720 -ResY=1280 -windowed
```

Screen-capture the client area (a D3D swapchain does not render into a `PrintWindow` DC; copy it
off the desktop instead) and measure. That path was checked against the packaged build and is
**pixel-equivalent**: mean 221.6 vs 221.5 before, 122.9 vs 122.9 after. `MHLLM_HUD=0` keeps the
chat overlay out of the measurement; `MHLLM_OPENING_SHOT=0` gives the static call framing.

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
- The imported secondary machine props remain comparatively low-detail. Replace them only where
  the call framing shows them clearly; the custom tape machines now provide the recognizable
  animated detail in the visible hall.
- The room tone's `AmbientSound` has **no attenuation asset**, so a sound placed deliberately out
  in the hall at (700, 20, 150) plays at the same level everywhere. It is mixed low enough not to
  matter today, but the position currently buys nothing.
- **The CRT screens render but are effectively invisible to the shipped shot.** Confirmed in a
  captured frame from the packaged build (debug camera, 89-degree FOV): the emissive trace is
  there — dark phosphor panel, readout blocks along the top, mint trace pixels mid-screen — but
  the screen occupies about 11x36 px at 1280x720, and at 26 degrees off-axis it is outside the
  21-degree portrait framing altogether. They were placed to be caught on the opening flythrough,
  which (see above) has never actually played. Placing at least one screen INSIDE the call cone,
  facing the camera, is a one-line change and the only way the texture earns its place.
- The authoring log now reports `CRT vector displays placed: N of 3` rather than an
  unconditional success line, and `BuildNative.ps1` gates on `[1-9]`. The old line printed even
  when `/Engine/BasicShapes/Plane` failed to load and three empty actors were spawned — the same
  shape of lie as `Tape reels spinning: 0`. Worth auditing the remaining `placed` lines for it.
