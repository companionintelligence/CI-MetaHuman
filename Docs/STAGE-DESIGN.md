# Stage design: LONGVIEW Lab, Bay 7 (1979)

The stage the resident lives on, as designed and as built. The full design record (the critiques it
answers, the judged variants it grafts from and the previz evidence) was kept outside this
repository; this page is the maintained summary for whoever changes the set or the camera. Renders are
not committed: regenerate them with `Deployment/blender/previz_diorama.py` (`--out <folder>`).

**Status.** The set, the shots and the opening's beats are implemented and data-driven (section 2).
The room is the 1979 machine room the owner asked for after the 1963 one: machines in continuous
rows along the walls and a working office on the floor (section 3). The map has been authored
headlessly in Unreal 5.8 and captured from the previz cameras (section 11). Every comfort budget
passes at all three tested resident heights, in previz and in the automation specs, and the
conversation frame now meets every gate at all three heights, the plain-wall share included
(section 7). The opening plays once per SESSION: a stream holds the lobby still until a viewer
connects, a kiosk starts at load, and any key, click or Esc follows design 3.2 (section 4). Part of
the room already answers her state (the INQUIRY lamp, a 3420's reels, the clock on host time;
section 10). Still designed but not built: the CI lobby page and its "Step inside" gesture, the DOM
captions, the rest of the set's responses, her head look-at (section 10). Nothing here has been
watched on the real stream: the sign-off step is a 60 fps capture (section 11).

## 1. The idea

It is the autumn of 1979, at night. A glass storefront on a dark street looks into a working machine
room. An IBM 3420 tape string runs the length of the left wall; the lab's 1959 IBM 1401 installation,
still in service, lines the vermilion head wall; a 3350 disk string and its air unit stand along the
right wall. The floor between them is an office at work: steel desks and worktables at desk height
with IBM 3277 terminals, the lab's small computers, card trays, listings and the coffee of a night
shift. A woman stands at the 1401's console desk, reading a printout. The visitor sees this from the
pavement through the open door; the picture cuts inside and walks in down the tape aisle at her eye
height on a gentle arc. She hears it, says "Oh, hello." while the camera is still walking, looks up at
3.5 m and turns back to the lens, and gives the line that hands the visitor the floor. The frame she
lands in is the conversation shot for the rest of the session: her against the dark passage to Bay 6,
the 3420 string receding behind her left shoulder, the 1401's operator panel and a clock on host time
at her right.

## 2. Where it lives

| What | Where |
|---|---|
| The set, the shots and the beats: one file | `Deployment/stage_layout.json` (the layout format: `meta`, `props`, `blockouts`, `resident`, `lights`, `sounds`, `shots`, `beats`) |
| The map, built from it headlessly | `Deployment/author_diorama.py` (Docs/SCENE-AUTHORING.md has the schema, the GLB import edits and the gates) |
| The camera's data, written from it | `Deployment/stage_layout.py` writes `[/Script/CICompanion.CompanionStageSettings]` in `Config/DefaultGame.ini`; `--check` fails on a stale section and on the set gates; the native authoring run rewrites it |
| The camera | `UCompanionStageSettings` (the shots and beats), `CompanionStage::BuildPlan` and `FCompanionStagePath` (the plan, solved against her measured eyes), `UCompanionStageDirector` (plays it) |
| The tests | `Private/Tests/CompanionStageSpec.cpp` (`MetaHumanLocalLLM.Frontend.CompanionStage`) |

The camera is config, not map data, because the map is authored in a content-only project that cannot
carry a Sequencer asset or reference plugin classes (HANDOFF.md, Lesson 6). The map carries only engine
actors, tags and TargetPoints: `StageMark.Home` (her mark, yawed to her home facing),
`StageCam.Vitrine`, `StageCam.Vitrine.Portrait`, `StageCam.Frieze.0..2`, `StageCam.Cutaway.Hub` and
`StageCam.Cutaway.Lineup`, plus set-piece tags (`TapeReel`, `StageReact.Tape`, `ConsoleLamps`,
`PrinterFeed`, `Typewriter`, `InquiryLamp`, `StageReact.Inquiry`, `WallClock`, `ExteriorLight`).

Units are Unreal centimetres and degrees, Z up. X runs from the street glass (x 0) toward the
vermilion head wall (x 1150); Y runs right as you face the room from the street; yaw turns +X
toward +Y.

## 3. The room

Plan: `top.png`, written by the previz.

| Item | Position and size |
|---|---|
| Room | x 0..1150, y -850..850: 17 m of glass on the long side, 11.5 m deep. Suspended acoustic-tile ceiling at 340 cm |
| Her mark | (650, -500, 0) at the 1407 console desk. Home facing 170 (toward the lens; HOME looks along yaw -10). Eye anchor E = mark + 9k toward the lens, z = 0.93 x stature (k = stature / 170) |
| The passage (the dark field behind her head) | An opening in the head wall from y -780 (in the 3420 string's rear service aisle) to -517, 280 cm high, recessed 130 cm and turned to the HOME axis (yaw -10), so the level HOME lens looks straight down it. It leads to Bay 6, where the 370 stands |
| Left wall: the tape string | Nine IBM 3420 tape units (76 x 75 x 170 cm) and two 3803 tape controls (76 x 71 x 152 cm) in one row, x 168.6..1009, fronts at y -684, 76.4 cm pitch (4 mm joints), backs 91 cm off the wall; the controls stand mid-string, as cabled |
| Head wall: the 1401 line-up | Fronts at x 932: the 1401 CPU (CI GLB, y -428), 1406, 1402 (lid propped), six 729 II at a 74.4 cm pitch, an open tape rack and the 1405 (the RAMAC GLB) at the right corner |
| Right wall: the disk string | Four IBM 3350s (CI GLB, an A2 and three B2), fronts at y 642.8, 122.3 cm pitch, standing free on a 122 cm rear service aisle with their louvred rear doors on it, closed by a down-flow computer-room air unit (180 x 86 x 188 cm) in line; a second tape rack in the corner |
| 1407 console desk | Top 150 x 75 at 75 cm, long axis along X, centre (630, -428); she stands at its -Y side. Console typewriter, key panel with the INQUIRY lamp, black phone, her printout under the desk lamp, legal pad, mug, two reels in cases, THINK sign; the empty shell chair at the kneehole is the visitor's seat |
| 1403 printer | (772, -245) on its chrome stand behind the console desk |
| The workspace | The operators' bench (three steel desks, 3277s facing the seated operators), the research bench (oak, 220 x 80) with the Kenbak-1, a PET 2001 stand-in, the SRI mouse and an oscilloscope, the Xerox Alto at its street end, a 2 x 2 of programmers' desks (one with a 3277), the unit-record worktable with the plugboard, the IBM 029 card punch on its own desk, a typing desk with a Selectric II. Every top at 74 cm (the 1407 at 75) |
| Ceiling | A 60 x 120 cm white T-bar grid of mineral acoustic tile with 42 continuous 2 x 4 ft troffers (117 x 57 cm) in rows over the tape aisle, the head-wall aisle, the 3350 aisle and three workspace runs, parallel to the machine rows |
| Floor | A raised floor of 60 cm vinyl-faced panels on a depressed slab, flush with the street door (no ramp); 11 perforated panels in front of the strings, cable cut-outs behind the 1401 units and the 729s; no overhead trays (in 1979 the cables run under the floor) |
| Machine-floor fixtures | The air unit, a tape cart at the street end of the tape aisle, a NO SMOKING sign, an emergency-power-off station and a CO2 extinguisher at the street end of the tape string, a second extinguisher at the end of the disk string, the run sheet on the string's street end, a tape-library schedule board, a hygrothermograph, the bay's power panel |
| Street door | 2 m clear at y -265, on the line from the lobby camera to her eyes; leaves held open |

Why: the owner asked for realistic machine-room rows, hardware lined up along the walls, and the
smaller computers and props on tables that look like a real workspace. Every usable wall carries one
continuous row with its fronts on one line, its tops level and its pitch constant, as the
period photographs show a machine room; the disk string stands free on its rear service aisle; the
troffer rows run over the aisles, parallel to the rows they light; the desks are at true height with
their working clutter resting on the tops and their terminals facing their users. Two sightlines fix
the rest: street > door > her eyes (the cut inside is axial) and HOME lens > her eyes > passage (a dark
field behind her head for any skin tone and height). The passage reaches into the tape string's rear
aisle, and the string starts 1.7 m in from the glass, so neither the jamb nor the string's end
crosses behind her head during the walk-in.

Plan gates (the design generator's, recorded in `meta.gates`): 0 overlaps among blockouts and GLB
props, 0 intrusions into the camera lane (80 cm either side of the path, below 220 cm), 0 solids
0.3-3.3 m behind her head on the home axis, 0 IBM service-clearance shortfalls (91 cm for tapes,
controls, reader and printer, 122 cm for processors, disks and the air unit, front and rear), every
1401 cable run within IBM's lengths, every desktop machine resting on a table top, 0 NoAI meshes.

### The year: 1979, and why it moved

The 1963 room excluded every small computer the owner now wants on the desks. The room therefore
stands in the autumn of 1979: inside the owner's "60s-70s retro" brief, and back on the GDD's own
"late night 1979". **LONGVIEW Lab, Bay 7, est. 1959, as it stood in 1979.** Nothing in the room is
later than 1979.

| Generation | In the room |
|---|---|
| The lab's 1959 installation, still running (1401s ran well into the 1980s) | 1401 CPU, 1406, 1402, 1403, six 729 II, the 1405, the 1407 console desk and its typewriter |
| The 1970s system, cabled under the floor to the System/370 in Bay 6 | 3803 tape controls and 3420 tapes (1970-73), the 3350 string (1975), 3277 display stations (1971), the IBM 029 card punch (1964), a Selectric II (1971), the down-flow air unit |
| The research bench | Kenbak-1 (1971), Xerox Alto (1973), the 1968 SRI mouse (a prototype kept as a curiosity), a Commodore PET 2001 (1977), a Tektronix 465-class oscilloscope (1972) |

The Prologica CP-500 (1982) stays out. The vermilion head wall, the LONGVIEW letters, the roundels,
the host-time clock and the storefront are the 1963 room's.

## 4. The opening

A SESSION starts the opening; until then the room holds the lobby still with nothing spoken. What
starts one (`EStageTrigger`, `MHLLM_OPENING_TRIGGER=load|viewer`; by default `viewer` when the
command line names a Pixel Streaming server and `load` otherwise):

- **A stream**: the first viewer's WebRTC data channel opening (`OnDataTrackOpenNative`). When the
  last viewer leaves (`OnAllConnectionsClosedNative`) the room goes back to the lobby still, so every
  visitor sees the whole opening from the street and hears both lines. This stands in for the
  design's "Step inside" gesture until the CI lobby page sends one (section 10).
- **A kiosk or a native window**: the level load.
- **The visitor**: any key, click or Esc in the lobby (design 3.2's G).

The lobby still is then held `HoldSeconds` (3 s, or `MHLLM_OPENING_DELAY`) before the cut inside, or
0.3 s after the visitor's own key or click. Move times below count from that cut. Times are for a
170 cm resident; the director re-solves them for the measured one.

| Move time | Picture | She |
|---|---|---|
| hold | VITRINE: the lobby still from the pavement, 28 mm | At work: body turned 30 deg toward the printout on her left (`WorkTurnDegrees` -30) |
| 0 | A flagged cut to ARRIVAL key 0, inside the door; the 20 street-side lights (`ExteriorLight`) are culled | |
| 1.3 s | ARRIVAL, walking in down the tape aisle at up to 0.75 m/s | "Oh, hello." (`GreetingLine`) |
| 2.55 s | The camera comes within 3.5 m of her eyes | The look-up |
| 2.75-3.70 s | | Her body turns 30 deg back to the lens (never more than 30) |
| 5.15 s | Under 2 cm/s: settled | |
| 5.56 s | The move lands exactly on HOME | |
| 5.75 s | HOME | The turn-yield line (`MHLLM_OPENING_LINE`); the stage hands over to live conversation |

Input follows design 3.2 (`UCompanionStageDirector::RespondToInput`; an `IInputProcessor` sees every
key and click, local or streamed, before any widget does):

| Input | Lobby | During the move | After the turn-yield line |
|---|---|---|---|
| Key, click or tap | G: the cut inside 0.3 s later | Flagged cut to HOME this frame, her turn completed under it; "Oh, hello." only if due within 1 s; the turn-yield line dropped | Nothing: the line finishes |
| Esc | G | Cut to HOME; nothing dropped (the greeting now, the line 1.2 s later) | Nothing |
| Enter with text | G, and the turn is held: she answers it when the move lands, instead of the line | Cut to HOME; pending lines dropped; she answers | Barge-in (the conversation's own) |

`MHLLM_OPENING_SHOT=0` is the reduced-motion path: the lobby still, then one flagged cut to HOME at
the end of the hold (no move, no breathing), the greeting 0.8 s and the line 2.0 s after the cut.
`MHLLM_OPENING_REPLAY=N` is a kiosk reset only: it replays after N s without input, and never while
a stream viewer is connected. The previz writes a contact sheet of the
move (`sheet_ARRIVAL.png`) and its first frame (`opening.png`).

## 5. Shots

Every shot is a CineCamera: a focal length on a 36 x 20.25 mm (16:9) filmback, or 20.25 x 36 for
portrait. The camera stays level (pitch 0, roll 0) and composes her eyes with sensor shift. A
resident-frame key is (r, theta, dz): r from her eye anchor E, theta from her home facing toward her
left, dz above her eyes; r and dz scale with stature. The director picks `<SHOT>_PORTRAIT` when the
viewport is portrait.

| Shot | Lens | Camera | Eyes on screen | Use |
|---|---|---|---|---|
| VITRINE | 28 mm, f/5.6 | r 1514, theta +10: on the pavement at (-782, 19), on the street > door > eyes line, 20 deg to the glass | (0.55, 0.62) | Lobby still, held until the move: the tape string recedes along the left wall to the dark passage, the 1401 line-up across the head wall |
| ARRIVAL | 50 mm, f/4 | 5 keys, r 473 -> 231, theta +10 -> 0, dz 0: in at the street door and down the tape aisle round her left front | (0.59, 0.56) -> (0.63, 0.33) | The move: 2.5 m in 5.56 s |
| HOME | 50 mm, f/4 | r 231.2 (1.36 x stature), theta 0 | (0.63, 0.33); shift (-4.68, -3.44) mm | The conversation frame; head 172 px at 720p (about 258 px at 1080p) |
| CLOSE | 80 mm, f/4 | HOME's position | (0.63, 0.33) | Punch-in for replies of 6 s or more, at the first sentence boundary 1.5 s or more in; back to HOME on the visitor's first key or 4 s after the reply; automatic cuts at least 4 s apart |
| *_PORTRAIT | 35 / 42 / 42 / 65 mm on 20.25 x 36 | as above | (0.50, ...) | Portrait lobby, move, home, close |
| FRIEZE | 32 mm | square-on truck along the 3420 faces, 2.1 m off them at reel height, x 480 -> 294 in 9 s, world frame | | Idle insert, off by default: only machine faces in frame, ending on drive 180, never past the row end |
| CUTAWAY_HUB | 24 mm | world frame, from the street end of the tape aisle | | The whole room: the research corner, the operators' bench, the head-wall line-up, the 3350 string and its air unit. Off by default |
| CUTAWAY_LINEUP | 24 mm | world frame, from the window corner of the tape aisle | | The view down the longest row from the run sheet on its street end. Off by default |
| FAREWELL | 50 mm | ease back 1.2 m in 5 s | (0.63, 0.33) -> (0.61, 0.41) | After her goodbye (not wired yet) |

Shift sign: the design writes Blender's (+ moves the window right or up), and Unreal's off-centre
projection reads the same way: a captured HOME frame puts her eyes where the previz does with +1 and
on the wrong side with -1. `MHLLM_STAGE_SHIFT_SIGN=-1` stays as a switch. Depth of field is on
(`MHLLM_STAGE_DOF=0` disables it if the stream host cannot afford it). The held shots (the lobby
still, HOME, CLOSE) breathe by rotation only: yaw within 0.15 deg, pitch within 0.09 deg, slower than
0.1 deg/s (`CompanionStage::BreathOffset`, `BreathingDegrees`; `MHLLM_STAGE_BREATHING=0` or reduced
motion turn it off).

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

The stature checks use the move as the C++ plays it: the design generator carries a Python copy of
`CompanionStage::BuildPlan` (ranges scaled by stature, the start capped at `MaxRangeCm`, the duration
from the comfort floor), so the 150 and 193 cm numbers below are what ships. (The 1963 record's 150 cm
row, 6.14 s, came from the generator's own timing; the C++ gives 5.56 s.)

| Previz, 120 Hz, as the C++ plays it | 150 cm | 170 cm | 193 cm | Budget |
|---|---|---|---|---|
| Duration; path | 5.56 s; 2.21 m | 5.56 s; 2.50 m | 5.00 s; 2.20 m | 5-8 s |
| Peak speed | 0.66 m/s | 0.75 m/s | 0.73 m/s | 1.0 m/s |
| Peak acceleration | 0.74 m/s² | 0.84 m/s² | 0.92 m/s² | 1.0 m/s² |
| Peak yaw rate | 3.05 deg/s | 3.05 deg/s | 3.37 deg/s | 5.66 deg/s (HFOV/7) |
| Nearest to her eyes while moving | 2.06 m | 2.33 m | 2.64 m | 2.0 m |
| Clearance while moving | 131 cm | 128 cm | 141 cm | 80 cm |
| Time to contact | 4.24 s | 4.24 s | 4.71 s | >= 4 s |
| Lens height | eye height | eye height | eye height | eye +/- 10 cm |
| Comfort rules failed (ARRIVAL, ARRIVAL_PORTRAIT, FAREWELL, FRIEZE) | 0 | 0 | 0 | 0 |

`CompanionStageSpec` asserts the budgets on the C++ plan itself at 150, 170 and 193 cm: speed and
acceleration (240 Hz), distance, time to contact, lens height, no jump between 60 fps frames,
monotonic progress, beat order, a turn of at most 30 degrees in one direction (the layout picks the
side: -30 turns her to her left, toward the printout), HOME within 1 mm of the move's last key and the
HOME shift.

## 7. The conversation frame

HOME: 50 mm at 2.31 m, eyes at (0.63, 0.33), head 172 px at 720p. Behind her head the dark passage to
Bay 6, 8.6-9.2 m back; at her left the 3420 string recedes into it, its reels above the caption plate
and drive 186 (`StageReact.Tape`) among them; at her right the 1401's operator panel, the bay's power
panel, the hygrothermograph and the clock on host time. Nothing emissive is near her eyes and no object
edge is beside her head.

| Census (previz, ray-cast 128 x 72) | 150 cm | 170 cm | 193 cm | Gate |
|---|---|---|---|---|
| HOME: eyes; head at 720p | (0.63, 0.33); 173 px | (0.63, 0.33); 172 px | (0.63, 0.33); 172 px | (0.63, 0.33) |
| HOME: wall-only share right of the captions | **4.7 %** | **6.6 %** | **11.2 %** | 15 % or less |
| HOME and CLOSE: emissive within 0.10 W of her eyes; object edges within 0.03 W of her head | 0; 0 | 0; 0 | 0; 0 | 0; 0 |
| First surface behind her head (HOME) | the passage | the passage | the passage | |
| CLOSE: wall-only share; head | 2.3 %; 276 px | 3.5 %; 276 px | 4.9 %; 276 px | 15 % or less |
| HOME_PORTRAIT, CLOSE_PORTRAIT: emissive; edges | 0; 0 | 0; 0 | 0; 0 | 0; 0 (their wall share is measured above the UI: no target) |
| ARRIVAL, 9 frames: emissive; edges beside her head | 0; 0 in every frame | 0; 0 in every frame | 0; 0 in every frame | 0 |

The 1963 room's walk-in had 7-12 edge samples beside her head in its first two frames: the passage's
left jamb and the end of the tape string crossed behind it. The passage now reaches into the string's
rear aisle and the string starts one pitch nearer the street, so both stay 2.3 deg or more clear of
her head all through the move.

The plain-wall gate the 1963 room missed (22.1 % at 170 cm, 17.7 % at 150 and 30.3 % at 193, against
15 %) now passes at every height. The camera is level at her eye line, so a taller resident's camera
stands higher and farther back and sees more of the wall above the 1.5-1.75 m machines: the passage is
280 cm high and reaches into the tape string's rear aisle, and the power panel stands between the
passage and the clock, so that wall is dressed or dark wherever the 193 cm frame reaches.

## 8. Light

A cool machine room with a warm human island: a 4100 K troffer field (14 rect bay lights under 42
emissive troffers), 2700-3000 K practicals, skin the warmest mid-tone and the vermilion wall the only
saturated plane. No brand colour in the light. 43 lights in candela, 2 of them casting shadows (the
hero key and the desk lamp pool). The 19 street-side lights are tagged `ExteriorLight` and culled
after the cut, so 24 run during the session; the window band's can over the tape aisle stays on,
because it is the only light that reaches the tape string's street end (it faces the glass) and the
run sheet hanging there. The 1403 cover and its paper are on lighting channel 1 only, so the hub key
and the bays skip them; so is the printout on her desk, which the lamp's spill light (channel 0)
skips, and the warm pool is the lamp's own spot.

The units, as the engine reads them:

- **Light colours** are written sRGB-encoded (`lights[].srgb`, the designer's kelvin-table values)
  and decoded to linear before `set_light_color`, which reads linear. Passed straight in, the
  "4100 K" field rendered near 5400 K and the "2700 K" practicals near-neutral under the white
  balance.
- **Emissives** are cd/m2 per unit luminance of their colour, and their colours are DISPLAY colours,
  balanced into scene colours by the inverse of the white balance (`meta.emissiveUnits`). Each troffer
  carries the luminance of the bay light over its row, I / A of that light's 120 x 300 cm rect
  (261-319 cd/m2 emissive, 240-293 displayed), so the ceiling reads brighter than the floor it lights;
  the acoustic tile and the T-bar carry a small emissive for the bounce the stream has no GI to give.
  `stage_layout.py --check` gates every troffer against the floor below it, and the previz gates
  VITRINE and CUTAWAY_HUB on the rendered frame.
- **The floor fill** is a 50 deg spot from the floor 1.1 m in front of her, aimed at her chest; its
  cone cannot reach the ceiling inside its radius. The desk lamp is 1.3 cd from just under its bulb,
  7.3 cm over the printout (about 240 lx), on the printout's lighting channel, and the lamp's own shade
  and bulb cast no shadow (`castShadow`). The other desk lamps, the oscilloscope's trace and the 3277
  screens are emissive only (no light cost).
- **Exposure** is pinned at EV100 4.6, solved on Unreal HOME captures of this room: a bracket at 4.5,
  4.7, 4.9 and 5.4 gave a frame mean of 123, 118, 113 and 101 of 255, and a capture at 4.6 measured
  119.5 (`meta.exposureNote`). The 1963 room's 5.4 leaves this frame darker, because the passage
  behind her head is wider and taller. What clips above 240 is the placeholder mannequin's chrome;
  elsewhere under 1 % of the frame (the clock face, the 1401 panel's glow). White balance 4300 K
  (`meta.whiteBalanceK`).
- **The floor** is drawn from world position in `M_StageTile`: 60 cm panels with 6 mm joints, and the
  perforated panels at a 3 cm hole pitch with mid-grey holes (the same material, `tileCm` 3); a grid
  finer than about five pixels fades to its average, so the far panels cannot shimmer. Reflections
  stay off (`r.ReflectionMethod=0`): SSR changed the floor's median by under 0.5 % in a capture, and
  its cost has to be measured on the stream host; a baked cubemap needs a rendering editor, which the
  headless authoring run is not.

## 9. Set dressing, wardrobe and licensing

**Two zones, the 1979 rule.** The *office zone* (the research bench, the programmers' desks, the
typing desk) has fabric task chairs in orange, brown and mustard, desk lamps, greenbar listings,
card trays, IBM manuals between bookends, mugs, pencil cups and phones, and amber glass ashtrays,
allowed here only. The *machine floor* (the strings and their aisles, the 1407 console desk, the
operators' bench, the worktable and the 029) has no ashtrays: a NO SMOKING sign, the emergency
power-off station, CO2 extinguishers at the row ends, the tape cart, the tape-library racks and the
run sheet on the 3420 string's street end. Every desk has a wastebasket, and every chair is pulled up
to its desk, except the empty shell chair at the 1407: the visitor's seat.

**Wardrobe** (replaces the 1963 costume rule), for any human-scale resident: a shirtwaist or wrap
dress, or a cream blouse with a soft bow over a knee-length A-line skirt, with a cardigan or
unstructured blazer; rust, camel, chocolate, navy or cream; low block heels or knee boots; a laminated
lab badge on a clip; one CI-teal scarf or brooch as the brand's single tell. No shorts and no bare
shoulders. The previz stand-in keeps its blue-grey dress, and the mannequin placeholder cannot be
dressed.

**Placed GLBs**, all from the licence-clean prop set that `MHLLM_GLB_DIR` points at: eight files,
14 placements. CI-authored: `ibm-1401-cpu.glb` (the CPU, recoloured by material),
`ibm-3350-disk.glb` (four, one string) and `ibm-3178-terminal.glb` (four IBM 3277 display stations,
on the operators' desks and a programmer's desk). CC BY 4.0 by museudocomputador, credited in
CREDITS.md, NOTICE.md and the stream's credits page: `ibm-ramac-305.glb` (the 1405 stand-in, scale
1.312), `plugboard.glb` (scale 0.55), `xerox-alto.glb`, `engelbart-mouse.glb` (scale 2.4) and
`kenbak-1.glb` (scale 0.0156). `author_diorama.py` edits three of them on a copy before import, the
same edits the previz makes, so the map places what the previz shows: the RAMAC loses its loose
platter (`clipLocal`); the Kenbak-1, which the file has lying face down with its power cord draped off
it, loses the cord (`clipLocal`), is stood up (`rotateLocal`, +90 deg about glTF X) and has its 79
meshes merged into one; the mouse's 16 meshes are merged into one. (Interchange imports each glTF mesh
as its own StaticMesh, and a prop places one.) Every slot is imported without Nanite: Interchange builds
Nanite by default, and with Nanite off in this project a Nanite mesh draws its decimated fallback, which
turned the 1401 console's lamps into scattered triangles and its knobs into pinwheels beside her shoulder
(the CPU's 1,964 triangles fell to 904, the Kenbak-1's 13,008 to 491). The author turns Nanite off on each
import and fails unless every slot's LOD0 carries every triangle of its GLB. The 3277 screens glow
through an emissive overlay on each GLB's phosphor (`IBM3277_*_phosphor`), because the author's prop
material drops GLB emission.
The Kenbak-1 and the mouse are marked `opaque`: their glTF BLEND materials (the Kenbak's front-panel
lettering, the mouse's wooden body) kept a translucent blend mode after the import's re-parent and
rendered dark and see-through in Unreal. The Kenbak's lettering plane is also recoloured to the
panel's light grey (its lettering lived in the alpha the project material drops) and its case to the
1963 stand-in's Kenbak blue instead of the file's neon cyan (`materialColors`).

**Not placed:** `commodore-pet.glb` (its one texture renders scrambled, a stack of PET badges; a
true-size stand-in at the model's bounds takes its place until it is re-textured), `ibm-729-tape.glb`
(its face is wrong: an open reel bay with the reels below it, 80 x 79 x 194 cm; the 729 blockouts are
its re-author spec, with pivoted, `TapeReel`-tagged reels), the unassembled `ibm-1401-mainframe.glb`
and `prologica-cp500.glb` (1982). **`ibm-2401-tape.glb` must not ship** (NoAI-tagged);
`author_diorama.py` refuses it by name.

Everything else is a true-size blockout and a spec to author as a CI prop: the 3420s (classic-blue
sides, warm-grey front frame, smoked reel door over a blue file reel and an aluminium machine reel,
the operator panel on top), the 3803s, the six 729s (cream fascia, smoked reel door, blue file reel
and dark take-up reel, 2700 K lamp window), the 1402, 1403, 1406, the 1407 desk and typewriter, the
029, the Selectric II, the oscilloscope, the air unit, the desks, chairs and dressing, the storefront
and the street. The 30 reels spin (`UTapeReelSpinner`, a quaternion about each reel's own axle), with
their flange windows attached. The resident is Epic's mannequin as a placeholder; Reallusion Nia and
the MetaHuman Seo are Fab content marked NoAI and stay opt-in. The cleared path is a MetaHuman
authored in-house in MetaHuman Creator.

## 10. Designed, not built

- **The CI lobby page and its gesture** (UI-less frontend, muted autoconnect, "Step inside", a
  `session-start` message read through `IPixelStreaming2InputHandler::RegisterMessageHandler`), and
  the queue message for a second visitor. Until it exists the session starts when the viewer's data
  channel opens, and the room restages on the last disconnect (section 4).
- **The DOM UI**: captions first, 16 px text, 44 px targets, the UI zones per shot.
- **The rest of the set as a state display**: the 1401 panel chase while she thinks, the typewriter
  printing her replies, the 1403 feed. Built (`UCompanionSetStateDisplay`, `UTapeReelSpinner`, from
  `UCompanionRuntimeSubsystem::GetPresence`: Idle, Listening, Thinking, Speaking): the INQUIRY lamp
  lights while the visitor has text in the box; the reels of 3420 drive 186 (`StageReact.Tape`, behind
  her left shoulder in HOME) run the tape-search pattern while she thinks, read at 0.5 rev/s while she
  speaks and wait while the visitor types; the wall clock's hands (`WallClock.Hour`,
  `WallClock.Minute`) keep host time.
- **FAREWELL**, idle and "Still there?", and the idle **FRIEZE**; the archive **cutaways**.
- **A C++ head look-at**, after which ALPHA returns to 18 deg (body to frame left, head back to the
  lens); until then the layout carries the design's ALPHA = 0 fallback.
- **Sound**: one 60 Hz room tone (the score still hums at 50 Hz), the street bed, the push bar,
  machine cues.
- **Scripted lines as cached WAVs** appended to her history; today they go through TTS. The dialogue
  and the web page should date the room 1979, not 1963.

## 11. Verified, and not

Verified in previz (EEVEE, true lenses, stand-ins at 150, 170 and 193 cm; the previz writes
`report.txt` to its output folder): 0 box overlaps, 0 lane intrusions, 0 solids behind her head, 0
clearance shortfalls, 0 cameras inside or passing through a surface in any shot, every moving shot
within every comfort budget, HOME and CLOSE composed on the passage at every stature with nothing
emissive near her eyes and no edge beside her head, and no edge beside her head in any ARRIVAL frame.
The rendered troffers read above the floor in VITRINE (0.99 against 0.81) and CUTAWAY_HUB (1.00
against 0.87). Near-white is 13 % in VITRINE and 17 % in HOME; the wide cutaways run above the 1963
room's 15-30 % band (37 % in CUTAWAY_HUB, 34 % in CUTAWAY_LINEUP: the raised floor and the troffer
ceiling are both near-white).

In Unreal 5.8: `author_diorama.py` builds the layout's 1,572 blockouts, 43 lights, 14 props from
eight GLB slots (three edited before import, every slot one static mesh at full detail: Nanite off, its
LOD0 carrying every triangle of its GLB), 30 TapeReel actors, 90 attachments and 8 TargetPoints, every
count checked against the layout (SUCCESS). The map was captured from the previz cameras (game view,
the street-side lights culled for the interior shots):
VITRINE, ARRIVAL, HOME, CLOSE, CUTAWAY_HUB and CUTAWAY_LINEUP compose as the previz does, and at
EV100 4.6 the troffers read above the floor on the pixels the previz ray-cast (1.27 x in VITRINE,
1.11 x in CUTAWAY_HUB; floor blue/red 0.97-0.98). The 3277 phosphor overlays glow, the Kenbak-1
stands upright with a light front panel, the mouse is whole and wooden, and the run sheet on the
string's street end is lit. Recaptured with the props at full detail: the 1401 console beside her
shoulder in HOME and CLOSE shows square lamps in rows and round knobs, the 3277's knob is round and
its indicator a bar, and the 1401 nameplate is a clean plate (before, the Nanite fallback drew
triangles, pinwheels, a pentagon, a wedge and a smear). The specs pass in the editor (47
`MetaHumanLocalLLM.Frontend.CompanionStage` specs, 370 of 370 plugin specs). The perforated panels'
3 cm grid was checked for aliasing: at full contrast the panels a few metres out aliased into a moire
in a still capture, so the holes are a mid grey and `M_StageTile` fades a grid finer than about five
pixels to its average; in a recapture the near panels keep their perforation and the far ones read
flat, with no moire.

Checked and kept out: in Unreal `commodore-pet.glb` renders as it does in the previz, its one texture
a stack of PET badges, so the true-size stand-in stays until the file is re-textured.

Not verified: the session trigger, the input routing and the CLOSE punch-in on a live stream (they
are unit-tested, not watched), the perforated panels on the moving camera, the cost of depth of field
and reflections on the stream host, VP8 artefacts, the portrait switch at runtime, and the real 3420,
3803, 729, 1402, 1403, 1406, 1407 and 029 meshes. **Before sign-off, capture a 60 fps loop of ARRIVAL
and HOME from the real stream, watch it full screen, and check VITRINE and HOME against the previz's
VITRINE and HOME frames.**

Open owner decisions: the crane (+54 cm, default off); when to enable the cutaways and FRIEZE; the
GDD amendments in the design record; rendering the lobby still at build time; the character and
wardrobe that replace the mannequin.

**Until the GDD is amended, author the set from this page and `Deployment/stage_layout.json`.**
GDD.md section 2 (the 9 x 6 m diorama, the teal-navy world, the camera down the central aisle past the
2401 reels to the Alto) still describes the room the critique rejected, though its "late night 1979"
is this room's date again. Amending the brief is the owner's call (design record, section 13), so it
has not been edited here.
