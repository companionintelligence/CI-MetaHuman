# Stage design: LONGVIEW Lab, Bay 7 (1963)

The stage the resident lives on, as designed and as built. The full design record, with the critique
it answers, the judged concepts it grafts from and the previz evidence, is
`C:\devel\ci\_scratch\stage-redesign\final\design.md`; this page is its working summary for whoever
changes the set or the camera. Renders are referenced by their scratch path and are not committed.

**Status.** The set, the shots and the opening's beats are implemented and data-driven (section 2).
Every comfort budget passes at all three tested resident heights in previz and in the automation
specs. One design gate does not pass: the share of plain wall in the conversation frame (section 7).
The experience around the camera (a lobby page and a "Step inside" gesture, captions in the DOM,
the room reacting to her state) is designed but not built yet (section 10). Nothing here has been
watched on the real stream: the sign-off step is a 60 fps capture (section 11).

## 1. The idea

It is 1963, at night. A glass storefront on a dark street glows from a luminous ceiling. One IBM 1401
system stands along a vermilion wall. A woman stands at the end of the console desk, reading a
printout. The visitor sees this from the pavement through the open door; the picture cuts inside and
walks in at her eye height on a gentle arc. She hears it, says "Oh, hello." while the camera is still
walking, looks up at 3.5 m and turns back to the lens, and gives the line that hands the visitor the
floor. The frame she lands in is the conversation shot for the rest of the session: a soft printer
corner in the foreground, her against a dark doorway, tape reels on her left, console lamps on her
right, and a clock running on host time to prove the stream is live.

## 2. Where it lives

| What | Where |
|---|---|
| The set, the shots and the beats: one file | `Deployment/stage_layout.json` (the layout format: `meta`, `props`, `blockouts`, `resident`, `lights`, `sounds`, `shots`, `beats`) |
| The map, built from it headlessly | `Deployment/author_diorama.py` (Docs/SCENE-AUTHORING.md has the schema and the gates) |
| The camera's data, written from it | `Deployment/stage_layout.py` writes `[/Script/CICompanion.CompanionStageSettings]` in `Config/DefaultGame.ini`; `--check` fails on a stale section; the native authoring run rewrites it |
| The camera | `UCompanionStageSettings` (the shots and beats), `CompanionStage::BuildPlan` and `FCompanionStagePath` (the plan, solved against her measured eyes), `UCompanionStageDirector` (plays it) |
| The tests | `Private/Tests/CompanionStageSpec.cpp` (`MetaHumanLocalLLM.Frontend.CompanionStage`) |

The camera is config, not map data, because the map is authored in a content-only project that cannot
carry a Sequencer asset or reference plugin classes (HANDOFF.md, Lesson 6). The map carries only engine
actors, tags and TargetPoints: `StageMark.Home` (her mark, yawed to her home facing),
`StageCam.Vitrine`, `StageCam.Vitrine.Portrait`, `StageCam.Frieze.0..2`, `StageCam.Cutaway.Hub` and
`StageCam.Cutaway.Lineup`, plus set-piece tags (`TapeReel`, `StageReact.Tape`, `ConsoleLamps`,
`PrinterFeed`, `Typewriter`, `InquiryLamp`, `StageReact.Inquiry`, `WallClock`, `ExteriorLight`).

Units are Unreal centimetres and degrees, Z up. X runs from the street glass (x 0) toward the
vermilion feature wall (x 1150); Y runs right as you face the room from the street; yaw turns +X
toward +Y.

## 3. The room

Plan: `C:\devel\ci\_scratch\stage-redesign\final\renders\top.png`.

| Item | Position and size |
|---|---|
| Room | x 0..1150, y -850..850: 17 m of glass on the long side, 11.5 m deep. Luminous ceiling at 340 cm |
| Her mark | (650, -132, 0). Home facing 180 (toward the lens). Eye anchor E = mark + 9k toward the lens, z = 0.93 x stature (k = stature / 170) |
| Doorway | 140 x 250, recessed 1.3 m in the feature wall, centred on the home axis (y -132): the dark field behind her head |
| Tape wall | Six IBM 729 II at the CHM size (74 x 86 x 175), fronts at x 973, 75 cm pitch, 91 cm off the wall; 729 #1's back corner on the home sightline to the doorway jamb |
| 1401 CPU | The CI-authored GLB at (971, 25), yaw 90, 140 cm rear clearance; its operator panel lands at 0.86-0.97 W in HOME |
| 1406, 1402, 1405 | (971, 146); (971, 359) with the lid propped; the RAMAC as the 1405 at (981, 644), scale 1.312 |
| 1407 console desk | Top 150 x 75 at 75 cm, centre (650, -239); she stands at its +Y end. Typewriter, INQUIRY lamp, key panel, phone, printout, THINK sign, desk lamp |
| 1403 printer | (586, -30) on a 70 cm chrome stand: HOME's soft foreground corner, 140 cm ahead of the lens |
| Street door | 2 m clear at y -245, on the line from the lobby camera to her eyes; leaves held open |

Why: one 1963 system laid out the way IBM showed it (the tape wall shoulder to shoulder, the doorway
between the tapes and the processor, the console desk as the hub), every machine at true size, every
cable run within IBM's lengths, and two sightlines fixing the rest: street > door > her eyes (the cut
inside is axial) and HOME lens > her eyes > doorway (a dark field behind her head for any skin tone
and height). Standing blocking needs no seated animation, so it works for any 150-195 cm resident,
including the mannequin placeholder.

Plan gates (logged by the design generator): 0 overlaps, 0 intrusions into the camera lane (80 cm
either side of the path, below 220 cm), 0 solids 0.3-3.3 m behind her head on the home axis, 0
service-clearance shortfalls, every cable run within IBM's lengths, 0 NoAI meshes placed.

## 4. The opening

`G` is the moment the move starts. Today that is level load plus the lobby hold (`HoldSeconds`, 3 s,
or `MHLLM_OPENING_DELAY`); the design's trigger is the visitor's "Step inside" gesture (section 10).
Times are for a 170 cm resident; the director re-solves them for the measured one.

| Move time | Picture | She |
|---|---|---|
| hold | VITRINE: the lobby still from the pavement, 28 mm | At work: body turned 30 deg toward the desk and door |
| 0 | A flagged cut to ARRIVAL key 0, inside the door; the 20 street-side lights (`ExteriorLight`) are culled | |
| 1.3 s | ARRIVAL, walking in at up to 0.75 m/s | "Oh, hello." (`GreetingLine`) |
| 2.55 s | The camera comes within 3.5 m of her eyes | The look-up |
| 2.75-3.70 s | | Her body turns 30 deg back to the lens (never more than 30) |
| 5.15 s | Under 2 cm/s: settled | |
| 5.56 s | The move lands exactly on HOME | |
| 5.75 s | HOME | The turn-yield line (`MHLLM_OPENING_LINE`); the stage hands over to live conversation |

Any input aborts to live: a flagged cut to HOME, her turn completed, the line dropped (the visitor
already has the floor). `MHLLM_OPENING_SHOT=0` is the reduced-motion path: HOME from the first frame,
the greeting 0.8 s and the line 2.0 s after the hold. Contact sheet of the move:
`C:\devel\ci\_scratch\stage-redesign\final\renders\sheet_ARRIVAL.png`; first frame:
`...\renders\opening.png`; the old opening at its true lens, for comparison:
`...\renders\sheet_CURRENT_OPENING.jpg`.

## 5. Shots

Every shot is a CineCamera: a focal length on a 36 x 20.25 mm (16:9) filmback, or 20.25 x 36 for
portrait. The camera stays level (pitch 0, roll 0) and composes her eyes with sensor shift. A
resident-frame key is (r, theta, dz): r from her eye anchor E, theta from her home facing toward her
left, dz above her eyes; r and dz scale with stature. The director picks `<SHOT>_PORTRAIT` when the
viewport is portrait.

| Shot | Lens | Camera | Eyes on screen | Use |
|---|---|---|---|---|
| VITRINE | 28 mm, f/5.6 | r 1514, theta -10: on the pavement, on the street > door > eyes line | (0.55, 0.62) | Lobby still, held until the move |
| ARRIVAL | 50 mm, f/4 | 5 keys, r 473 -> 231, theta -10 -> 0, dz 0 | (0.57, 0.53) -> (0.63, 0.33) | The move: 2.5 m in 5.56 s |
| HOME | 50 mm, f/4 | r 231.2 (1.36 x stature), theta 0 | (0.63, 0.33); shift (-4.68, -3.44) mm | The conversation frame; head 173 px at 720p |
| CLOSE | 80 mm, f/4 | HOME's position | (0.63, 0.33) | Punch-in for long replies (not wired yet) |
| *_PORTRAIT | 35 / 42 / 42 / 65 mm on 20.25 x 36 | as above | (0.50, ...) | Portrait lobby, move, home, close |
| FRIEZE | 32 mm | truck along the 729 faces, world frame | | Idle insert; off by default |
| CUTAWAY_HUB, CUTAWAY_LINEUP | 28, 40 mm | world frame (archive vantages) | | B-roll; off until the meshes hold up at 1-3 m |
| FAREWELL | 50 mm | ease back 1.2 m in 5 s | (0.63, 0.33) -> (0.58, 0.37) | After her goodbye (not wired yet) |

Shift sign: the design writes Blender's (+ moves the window right or up). Unreal's off-centre
projection reads the same way by its maths, but nobody has checked a captured frame yet;
`MHLLM_STAGE_SHIFT_SIGN=-1` flips it without a rebuild. Depth of field is on (`MHLLM_STAGE_DOF=0`
disables it if the stream host cannot afford it).

## 6. How the move is built, and its comfort budget

The path is a centripetal Catmull-Rom curve through the keys (C1, no overshoot), sampled by arc length
(1024 samples per segment) so equal times cover equal distances, timed by an S-curve: velocity rises
with smootherstep over the first 30 %, cruises, and falls over the last 50 % (continuous velocity and
acceleration, zero at both ends; peak speed 1.667 x mean). The aim stays on her eye anchor, so the
look-at cannot jump at a key.

Duration: T = max(1.667 L / vmax, the yaw term, 5 s), lengthened in 0.05 s steps until the time to
contact never drops under 4.2 s, then capped at 8 s. vmax is 0.8 m/s x min(1, stature / 170).
`MHLLM_FLY_SECONDS` replaces the 8 s cap, and a cap below the comfort floor is logged and ignored
(the compose default of 11 therefore changes nothing). The start never stands more than 473 cm from
her eyes (175 cm inside the glass), whatever her height.

| Previz, 120 Hz | 150 cm | 170 cm | 193 cm | Budget |
|---|---|---|---|---|
| Duration; path | 6.14 s; 2.52 m | 5.56 s; 2.50 m | 5.00 s; 2.19 m | 5-8 s |
| Peak speed | 0.68 m/s | 0.75 m/s | 0.73 m/s | 1.0 m/s |
| Peak acceleration | 0.70 m/s² | 0.84 m/s² | 0.92 m/s² | 1.0 m/s² |
| Peak yaw rate | 2.8 deg/s | 3.05 deg/s | 3.4 deg/s | 5.66 deg/s (HFOV/7) |
| Nearest to her eyes while moving | 2.06 m | 2.33 m | 2.64 m | 2.0 m |
| Time to contact | 4.22 s | 4.24 s | 4.73 s | >= 4 s |
| Lens height | eye height | eye height | eye height | eye +/- 10 cm |

The C++ re-derivation is not identical to the design generator's for other statures (it scales every
range with stature and caps the start, where the generator starts each stature as far out as the
glass allows), so its 150 cm move is somewhat shorter. `CompanionStageSpec` asserts the budgets on
the C++ plan itself at 150, 170 and 193 cm: speed and acceleration (240 Hz), distance, time to
contact, lens height, no jump between 60 fps frames, monotonic progress, beat order, a turn of at
most 30 degrees, HOME within 1 mm of the move's last key and the HOME shift. Compared with the
opening it replaces (42 m in 11 s at up to 11.6 m/s, 413 deg/s, through a pane, landing behind her),
ARRIVAL covers 2.5 m at 0.75 m/s and lands on her eyes.

## 7. The conversation frame, and the one gate it does not meet

HOME: 50 mm at 2.31 m, eyes at (0.63, 0.33), head 173 px, the doorway behind her head (0.50-0.76 W),
729 #1 and #2 lamp windows and reels above the caption plate, the 1401 panel at 0.86-0.97 W, the clock
top right, the 1403 corner (0.83-1.0 W, 0.68-1.0 H). Stills: `...\final\renders\stills\HOME_01.png`
and `HOME_PORTRAIT_01.png`; three statures: `...\renders\statures.png`.

| Census (previz, ray-cast) | Result |
|---|---|
| Emissive within 0.10 W of her eyes (HOME, CLOSE, every ARRIVAL frame, 150/170/193 cm) | 0 |
| Object edges within 0.03 W of her head (HOME) | 0 |
| First surface behind her head | the doorway, 6.5 m behind her eyes |
| **Wall-only share right of the captions (HOME)** | **22.1 % at 170 cm (17.7 % at 150, 30.3 % at 193) against a 15 % target** |

The wall share fails for a geometric reason: the camera is level at her standing eye height, every
machine of the period is 100-175 cm tall, so the band above her eye line is wall unless something
taller stands there. An oblique home axis was tested and is worse (25.4 %); raised letters crop and
brand the frame; a drape kills the vermilion. The options are the owner's: seat the resident once a
seated idle exists for a cleared character (about 12 %), or set the standing gate at 22 % with
emissive 0 and edges 0 kept as hard gates.

## 8. Light

A cool showroom with a warm human island: a 4100 K luminous-ceiling field (16 rect lights, emissive
panels), 2700-3000 K practicals, skin the warmest mid-tone and the vermilion wall the only saturated
plane. No brand colour in the light. 44 lights in candela, 2 cast shadows (the hero key and the desk
lamp pool); the 20 street-side ones are tagged `ExteriorLight` and culled after the cut, so 24 run
during the session. The 1403 cover is on lighting channel 1 only, so the hub key and the hub bays skip
it and the foreground corner stays a stop under her face. The exposure stays pinned (EV100 9.4,
carried over from the measured rc1 rig) and must be re-solved on a captured HOME frame: mean about
120/255, nothing above 240, UI rows excluded. White balance is set to 4300 K (`meta.whiteBalanceK`).

## 9. Set dressing and licensing

Placed GLBs, all from the licence-clean set (`C:\devel\ci\_scratch\metahuman-glb-licensed`): the
CI-authored `ibm-1401-cpu.glb` (recoloured by material), and two CC BY 4.0 models by
museudocomputador already credited in CREDITS.md, `ibm-ramac-305.glb` (the 1405 stand-in) and
`plugboard.glb`. The design deletes the RAMAC's loose platter in a one-time Blender edit; that edit is
not done, so it is placed whole. Not placed: the 3277 terminal and 3350 disk (after the room's date),
the Kenbak, Alto, PET and CP-500 (after the date; they move into her dialogue and the web page), the
Engelbart mouse, the unassembled 1401 mainframe, and `ibm-729-tape.glb` (its face is wrong: the
blockouts are its re-author spec, and the shipped asset keeps pivoted, `TapeReel`-tagged reels).
**`ibm-2401-tape.glb` must not ship** (NoAI-tagged); `author_diorama.py` refuses it by name.

Everything else is a true-size blockout and a spec to author as a CI prop: the six 729s (cream fascia,
smoked reel door, blue file reel and dark take-up reel with three-window flanges, 2700 K lamp window),
the 1402, 1403, 1406, the 1407 desk and typewriter, the shell chairs, the storefront and street. The
reels spin (`UTapeReelSpinner`), with their flange windows attached. The resident is Epic's mannequin
as a placeholder; Reallusion Nia and the MetaHuman Seo are Fab content marked NoAI and stay opt-in.
The cleared path is a MetaHuman authored in-house in MetaHuman Creator.

## 10. Designed, not built

- **Session start on the visitor's gesture** behind a CI lobby (UI-less frontend, muted autoconnect,
  "Step inside"), the queue message for a second visitor, reset on the last disconnect.
- **The DOM UI**: captions first, 16 px text, 44 px targets, the UI zones per shot.
- **The set as a state display**: INQUIRY lamp while the visitor types, panel chase and tape search
  while she thinks, the typewriter printing her replies, the 1403 feed, the clock on host time.
- **CLOSE** on long replies, **FAREWELL** and the idle **FRIEZE**; the archive **cutaways**.
- **A C++ head look-at**, after which ALPHA returns to 18 deg (body to frame left, head back to the
  lens); until then the layout carries the design's ALPHA = 0 fallback.
- **Sound**: one 60 Hz room tone (the score still hums at 50 Hz), the street bed, the push bar,
  machine cues.
- **Scripted lines as cached WAVs** appended to her history; today they go through TTS.

## 11. Verified, and not

Verified in previz (EEVEE, true lenses, stand-ins at 150, 170 and 193 cm): 0 overlaps, 0 lane
intrusions, 0 solids behind her head, 0 clearance shortfalls, every moving shot within every comfort
budget, HOME composition and the doorway at every stature, 0 emissive and 0 edges beside her head.
Report: `C:\devel\ci\_scratch\stage-redesign\final\renders\report.txt`.

In this repository: `author_diorama.py` builds the layout's 879 blockouts, 44 lights, 3 props, 12
TapeReel actors, 36 attachments and 8 TargetPoints with every count checked against the layout (run
against a recording stand-in for the `unreal` module; not yet authored in Unreal), and the camera
specs pass in the editor (32 `MetaHumanLocalLLM.Frontend.CompanionStage` specs, 355 of 355 plugin specs).

Not verified: Unreal's no-GI lighting and tonemapper, the cost of depth of field and reflections on
the stream host, the sign of the sensor shift, the lighting-channel flag, VP8 artefacts, the portrait
switch at runtime, and the real 729, 1402, 1403, 1406 and 1407 meshes. **Before sign-off, capture a
60 fps loop of ARRIVAL and HOME from the real stream, watch it full screen, and check VITRINE and HOME
against `C:\devel\ci\_scratch\stage-redesign\final\renders\vantages.png`.**

Open owner decisions: the wall-share gate (section 7); the crane (+54 cm, default off); when to enable
the cutaways and FRIEZE; the GDD amendments in the design record (section 13 there); rendering the
lobby still at build time; the character and wardrobe that replace the mannequin.
