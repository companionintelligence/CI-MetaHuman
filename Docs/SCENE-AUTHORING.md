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

- Cook and verify the 729 hall; confirm `Tape reels spinning: N` in the log.
- Pull troffer emissive down — the ceiling is blowing out.
- Author scan-line art on the CRTs (currently flat emissive colour).
