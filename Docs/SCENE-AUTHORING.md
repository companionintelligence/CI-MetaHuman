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

Twelve hero props are Sketchfab GLBs. They are imported as **GLB, never FBX** — Blender's FBX
exporter only writes textures wired to a Principled BSDF, so several machines exported with no
textures at all and rendered as blank white blocks.

The tape units are **authored in Blender**, not scavenged, because no scavenged prop was an IBM
729 and the hall was consequently 20 featureless slabs. `Deployment/blender/make_ibm729.py`
rebuilds it: real proportions (0.76 x 0.74 x 1.80 m), off-white shell, chrome frame, dark glass
door over the reel bay, IBM-blue lower panel, amber supply reel, white take-up reel, CI-mint
status lamps.

```
blender --background --python Deployment/blender/make_ibm729.py -- --out <glb dir>
```

Every dimension in that script is an explicit constant rather than a viewport adjustment, and the
output is verified against the shipped `ibm-729-tape.glb`: node names and transforms, all seven
materials (including the lamp's emissive strength), and all seven primitives' vertex counts,
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

`UTapeReelSpinner` finds actors tagged `TapeReel` and rotates them on Roll.

Roll is correct and not a guess: in the GLB each reel is its own node with its origin at the hub,
disc in local XY, axis local Z. glTF Z maps to Unreal X, so the reel's axis *is* the actor's Roll
axis. `FRotator` composes as `Rz(Yaw) * Ry(Pitch) * Rx(Roll)`, so Roll is applied in local space
before the cabinet's +/-90 yaw — the units can face across the aisle and the reels still spin about
themselves rather than orbiting.

**None of this is visible on the stream, and that is a framing problem, not a reel problem.** The
shipped shot is a 21-degree portrait solved for the resident's face, from x=170 looking down +X.
The tape rank sits at x 560..1510, y +/-155/195, yawed +/-90 so the reel faces look *across* the
aisle. At the near bank the units are outside the horizontal cone entirely (half-width 72cm at
x=560 versus |y| 155); at the far bank they are inside it but edge-on and largely behind her. A
camera-held-still diff over four frames confirms it: the machine-only strips move 0.08% (right,
indistinguishable from the 0.02% encoder noise floor) and 4.8% (left, consistent with encoder
bleed from her adjacent hair and arm). `Tape reels spinning: 24` in the log is real; the pixels
simply do not contain a reel to watch.

Reviewing this asset needs a camera that is not the call framing — see the camera note below.

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
- **The camera cannot be moved from outside the app, by any route.** Three separate mechanisms have
  to agree and currently do not:
  1. `MHLLM_FREE_LOOK=1` used to un-ignore input while `LockPlayerView` still called
     `SetViewTarget(Camera)` on a camera that never moves. Input was live the whole time and the
     pawn really was walking around the set; the picture just did not come from it. Fixed — the
     pawn now receives the view and starts on the call framing.
  2. Even so, nothing reaches the pawn: the app arms the chat box on connect ("Chat input armed:
     cursor shown, keyboard focus on the text box"), so Slate owns cursor and keyboard. Mouse
     deltas and WASD from a browser go to the text field, not to look/move.
  3. `MHLLM_OPENING_SHOT=1` runs its beats correctly in the log — `Opening shot: 18.0s flythrough`,
     then `Stage handed over` 25s later — while the streamed image does not move at all. The map's
     auto-activating `StageCamera` wins the view target over the director's spawned camera, so the
     flythrough animates an actor nobody is looking through. **The opening shot has therefore never
     actually played on the stream.**
  For now, review non-hero angles by GPU-rendering the authored map offscreen with the camera moved
  in Python (no cook needed, and `ExposureOverride` makes renders comparable). Note this needs an
  engine image with Mesa/RADV installed — the stock `ghcr.io/epicgames/unreal-engine:dev-5.5` ships
  no Vulkan ICD, so `-Vulkan` finds no device and the editor dies right after `LogVulkanRHI`.

## Measured, not remembered

Numbers from captured stream frames (720x1280 portrait), 2026-07-31. **Exclude the top 34 and
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
- **The reels are flat discs, so their rotation is invisible even where the camera can see them.**
  `UTapeReelSpinner` turns them and the log confirms 24 spinning, but a featureless disc looks
  identical at every angle — the frame-diff evidence above measures the framing problem and would
  have measured the same 0.08% with a fixed camera pointed straight at one. Spokes, a hub slot or
  an asymmetric tape pack in `make_ibm729.py` would cost nothing and make the motion legible.
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
