# Headless UE authoring — M2: the 1970s research-datacenter diorama.
# Imports the 12 prop FBXs (textures alongside), builds CompanionStage v2 with the same
# layout as the web DatacenterDiorama (web meters -> UE cm: x_ue=-100*z_w, y_ue=100*x_w,
# z_ue=100*y_w; yaw_ue=-deg(rotY_w)), era lighting per the GDD ("diegetic warm, chrome
# cool"), desks under the desk-height artifacts, blockout resident, PlayerStart framing.
# Writes /out/AUTHOR_RESULT.txt with progress + SUCCESS/FAIL.
import unreal, traceback, math, os

# INPUT LOCATIONS. The container mounts these at fixed absolute paths (see
# Deployment/core2/run_author_m2.sh), and every default below is that container path, so the
# server pipeline behaves exactly as before. Scripts/BuildNative.ps1 runs this SAME script
# against the locally-installed editor by overriding them with Windows paths — which is the
# whole point: the set has ONE definition, and a native build authors it rather than shipping
# a stale copy of it. Any new external input must be read through here, never hard-coded, or
# the local build silently diverges from the cook again.
def _dir(var, default):
    return os.environ.get(var, default).replace("\\", "/").rstrip("/")

GLB_DIR   = _dir("MHLLM_GLB_DIR",   "/glb")      # prop GLBs (scavenged Sketchfab assets)
MH_DIR    = _dir("MHLLM_MH_DIR",    "/mh")       # MetaHuman "Seo" FBX/TGA (fallback resident only)
AUDIO_DIR = _dir("MHLLM_AUDIO_DIR", "/audio")    # Deployment/audio — the machine-room score
# Deployment/generated — set dressing this repo SYNTHESIZES rather than scavenges:
# T_CRT_Vector.png (Scripts/MakeCRTTexture.py) and S_RoomTone.wav (Scripts/MakeAmbience.py
# --mode roomtone). Both are committed, so any checkout can author the full set.
#
# The DEFAULT IS GLB_DIR, not a repo path, and that is deliberate. On core-2 these two files
# have only ever existed inside the props mount, so defaulting anywhere else would break the
# server pipeline to fix the native one. The container therefore keeps reading them from /glb
# exactly as before, while BuildNative.ps1 points MHLLM_GEN_DIR at Deployment/generated.
#
# This is also the bug this variable exists to close: both files were previously read straight
# out of GLB_DIR, so a checkout without core-2's props directory silently shipped no powered
# CRT screens and no room tone -- the imports are wrapped in try/except and the only trace was
# "There was nothing to import" in the authoring log.
GEN_DIR   = _dir("MHLLM_GEN_DIR",   GLB_DIR)
RESULT    = os.environ.get("MHLLM_AUTHOR_RESULT", "/out/AUTHOR_RESULT.txt")
def w(s):
    try:
        with open(RESULT, "a") as f: f.write(str(s) + "\n")
    except Exception: pass
    unreal.log("M2: " + str(s))

# name, fbx, pos (x,y,z cm), yaw deg, real-world target for the mesh's LARGEST dim (cm).
# Scale is normalized HERE, from the imported mesh's measured bounds — not trusted from the
# source file. Sketchfab FBX/.blend sources carry wildly inconsistent unit scales (and some
# resist Blender-side baking entirely: multi-user mesh data makes transform_apply a no-op,
# which once shipped a 29-metre "desk terminal" that swallowed the camera). Measuring in UE
# is the only self-correcting place to do this.
# Props are imported as GLB, not FBX. Blender's FBX exporter only writes textures wired to a
# Principled BSDF base color, so most of these Sketchfab materials exported with no maps at
# all and every machine rendered as a blank white block. The GLB export carries materials +
# textures intact (it is the same asset set the web build renders correctly), and UE 5.5's
# Interchange glTF importer brings Texture2D + MaterialInstanceConstant across with the mesh.
PROPS = [
    # name,        glb,                    pos (x,y,z cm),      yaw, target_cm
    ("ramac305",   "ibm-ramac-305.glb",     ( 460, -360, 0),  -20, 170),
    ("ibm1401",    "ibm-1401-mainframe.glb",( 490, -160, 0),   -7, 170),
    # Moved OFF the centre line (was y=30, directly behind the resident). In a portrait framed on
    # her, that one cabinet filled the entire background and hid the hall completely -- the shot
    # read as "woman in front of a white box" rather than "inside a machine room". The sightline
    # down the aisle is the composition, so nothing may stand in it.
    ("ibm1401cpu", "ibm-1401-cpu.glb",      ( 490, -300, 0),    4, 170),
    ("ibm3350",    "ibm-3350-disk.glb",     ( 490,  210, 0),    7, 150),
    ("ibm2401",    "ibm-2401-tape.glb",     ( 460,  370, 0),   20, 183),
    ("kenbak1",    "kenbak-1.glb",          ( 110, -330, 74), -29,  48),
    ("mouse63",    "engelbart-mouse.glb",   (  85, -285, 74), -11,  15),
    ("plugboard",  "plugboard.glb",         ( 155, -355, 74), -34,  90),
    ("xeroxalto",  "xerox-alto.glb",        ( 260,  270, 0),   40, 120),
    ("pet",        "commodore-pet.glb",     ( 170,  335, 0),   49,  50),
    ("ibm3178",    "ibm-3178-terminal.glb", (  60,  370, 0),   60,  50),
    ("cp500",      "prologica-cp500.glb",   ( -50,  355, 0),   69,  55),
]

try:
    open(RESULT, "w").close()
    w("start")
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # ---------- import all 12 props (GLB via Interchange; materials+textures come with) ----------
    def import_prop(name, glb):
        task = unreal.AssetImportTask()
        task.filename = GLB_DIR + "/" + glb
        task.destination_path = "/Game/Diorama/" + name
        task.automated = True
        task.save = True
        task.replace_existing = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        assets = ar.get_assets_by_path("/Game/Diorama/" + name, recursive=True)
        mesh, tex = None, 0
        for a in assets:
            k = str(a.asset_class_path.asset_name)
            if k == "StaticMesh" and mesh is None:
                mesh = unreal.load_asset(str(a.package_name))
            elif k == "Texture2D":
                tex += 1
        return mesh, tex

    imported = {}
    for name, glb, pos, yaw, target_cm in PROPS:
        sm, tex = import_prop(name, glb)
        imported[name] = sm
        w("import %-11s -> %s (%d tex)" % (name, "OK" if sm else "NO_STATICMESH", tex))

    # ---------- fresh level ----------
    # THE empty-stage root cause. new_level REFUSES to overwrite ("Failed to validate the
    # destination. An asset already exists at this location") and returns False, and everything
    # after it then authors into the UNTITLED transient level -- which save_current_level cannot
    # write ("Can't save the level because it doesn't have a filename"). The container never hit
    # this because it authors into a project whose Content/ was just rm -rf'd, so there was
    # nothing to collide with; anywhere the repo's own committed CompanionStage.umap exists, the
    # whole run silently produced no map at all. Delete first, and refuse to continue if the
    # fresh level did not actually take.
    MAP_PATH = "/Game/Maps/CompanionStage"
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        unreal.EditorAssetLibrary.delete_asset(MAP_PATH)
        w("removed the pre-existing %s so new_level can claim the path" % MAP_PATH)
    if not les.new_level(MAP_PATH):
        raise RuntimeError("new_level(%s) failed; refusing to author into an untitled level" % MAP_PATH)
    w("new_level ok")

    # The streamed pawn spawns at the WORLD ORIGIN, not at our PlayerStart: GameModeBase does
    # not honour it here (verified — the map's PlayerStart is correct at (-250,-60,175) yet
    # the live view sits at (0,0,0), inside the floor plane, which is why the stream showed a
    # grazing checkerboard). Rather than fight the spawn, the whole set is authored around the
    # camera: SET_OFFSET shifts every actor so that world origin IS the intended camera pose,
    # looking down +X. That keeps the DefaultPawn (and therefore live WASD/mouse input) while
    # framing the shot deterministically — no CameraActor hijacking the view target.
    SET_OFFSET = (0.0, 0.0, 0.0)   # set authored in world space; view comes from StageCamera
    def spawn(cls, loc=(0,0,0), yaw=0.0, pitch=0.0, roll=0.0):
        world = (loc[0] + SET_OFFSET[0], loc[1] + SET_OFFSET[1], loc[2] + SET_OFFSET[2])
        return eas.spawn_actor_from_class(
            cls, unreal.Vector(*world), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))

    # ---------- lighting: diegetic warm key, cool CI rim ----------
    # CRITICAL: the Pixel Streaming app runs with GI DISABLED (r.DynamicGlobalIlluminationMethod 0
    # and friends — the RADV stability workarounds), so DIRECT light is all there is. Do not tune
    # these against a local editor preview: Lumen is on by default there and will bounce a
    # 2.5-lux directional into a convincingly-lit room, while the real stream renders black.
    # Every value below is chosen for a no-GI, direct-light-only scene.
    MOVABLE = unreal.ComponentMobility.MOVABLE
    dl = spawn(unreal.DirectionalLight, (0,0,600), pitch=-42.0, yaw=-25.0)
    c = dl.get_component_by_class(unreal.DirectionalLightComponent)
    c.set_editor_property("mobility", MOVABLE)
    c.set_intensity(8.0); c.set_light_color(unreal.LinearColor(1.0, 0.82, 0.62))  # tungsten
    def point(pos, color, inten, radius=2000.0):
        p = spawn(unreal.PointLight, pos)
        c = p.get_component_by_class(unreal.PointLightComponent)
        c.set_editor_property("mobility", MOVABLE)
        c.set_intensity(inten); c.set_attenuation_radius(radius)
        c.set_light_color(unreal.LinearColor(*color))
    # Even ambient FIRST. With GI off there is no bounce to fill the room, and a SkyLight is
    # not a substitute: SLS_SPECIFIED_CUBEMAP with no cubemap assigned (and a captured one
    # with sky-capture disabled) contributes exactly nothing. That left isolated pools of
    # light — the default view read black while a turn to +Y blew out white. A wide ceiling
    # grid is the reliable no-GI fill.
    # A LOW even base — just enough that nothing falls to pure black. Cranking this grid up
    # is what flattened the room: nine bright ceiling lights wash out every texture and kill
    # the shadows the set reads by. The base only lifts the floor; the accents do the work.
    #
    # GRID_Z: the grid used to hang at 430, SIXTY CENTIMETRES under a 0.90-albedo ceiling slab
    # whose underside sits at 490. Inverse square does the rest: the ceiling caught I/0.6^2 while
    # the floor 4.3m down caught I/4.3^2 — FIFTY-ONE TIMES less. So the single brightest surface
    # in a "1970s machine room" was its ceiling, by a factor no exposure can accommodate, and it
    # dragged the pale upper walls up with it. Dropping the grid to 330 puts it 1.6m under the
    # ceiling and 3.3m over the floor: 4.3:1, which is what a room lit from above actually looks
    # like. This costs nothing — the fittings that READ as sources are the emissive troffers at
    # 492, and they are unchanged.
    GRID_Z = 330
    for gx in (-100, 150, 400):
        for gy in (-320, 0, 320):
            point((gx, gy, GRID_Z), (0.93, 0.96, 1.00), 340, 1500.0)
    # Accents, re-graded to the Old IBM Datacenter reference. That room is lit by RANKS OF
    # FLUORESCENT TROFFERS -- cool, bright and even -- not by warm tungsten pools, and its only
    # strong colour is the orange back wall bouncing onto white cabinets and a white tile floor.
    # The previous warm-amber + CI-aqua scheme read as a moody exhibit; this reads as a working
    # machine room with the lights on.
    #
    # ACCENT LEVELS AND RADII. Radii matter as much as intensities here. At r=1200..1500 every
    # accent reached every surface of a 28m room, so twenty-odd "accents" summed into one
    # omnidirectional wash and the set lost its falloff — that is the flat evenly-lit box, and it
    # is a no-GI-specific trap: with bounce light you spend a few big sources and let the room
    # distribute them, without it the temptation is to add sources until nothing is dark, which is
    # the same thing as deleting the shading. Pulled in to r~900..1200 each light lights its own
    # bay and the hall recedes.
    point(( 50,  260, 240), (0.92, 0.95, 1.00), 5600, 1000.0)  # cool fluorescent, terminal row
    point(( 450, -300, 300), (0.90, 0.94, 1.00), 4800, 1100.0) # cool fill, big iron
    point(( 450,  300, 300), (0.90, 0.94, 1.00), 4200, 1000.0)
    # Warm orange BOUNCE off the back wall. With GI off nothing bounces on its own, so the
    # reference's signature orange cast has to be placed by hand or the wall reads as a flat
    # painted panel with no effect on the room.
    point((1500, -200, 260), (1.00, 0.42, 0.18), 9000, 1200.0)
    point((1500,  240, 260), (1.00, 0.42, 0.18), 9000, 1200.0)
    point((-150, 0, 250), (0.95, 0.96, 1.00), 3000, 1000.0)     # camera-side fill on the resident
    point(( 100, -320, 160), (0.95, 0.70, 0.35), 2400, 650.0)   # desk-lamp practical (kept warm)
    # Key on the resident, slightly cool to sit in the fluorescent room, plus a soft frontal fill
    # so a portrait crop is not lit only from above.
    point(( 300, -60, 250), (0.96, 0.97, 1.00), 3400, 900.0)
    point(( 200,  20, 175), (1.00, 0.97, 0.92), 1600,  520.0)   # close portrait fill

    # ---------- exposure: PINNED, and this is the fix for the white-haze room ----------
    #
    # THE BUG. Config/DefaultEngine.ini sets
    #     r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True
    # which changes the UNITS of AutoExposureMinBrightness / MaxBrightness from average scene
    # luminance to EV100. The values that used to be here, 0.03 and 8.0, are UE4's LUMINANCE-space
    # defaults, pasted into a field this project reads as EV100. So "wide bounds" was in fact a
    # hard ceiling of EV100 8.0 = an average scene luminance of 1.2 * 2^8 = 307 cd/m^2. Metered,
    # this room sits near EV100 11.5 (~3500 cd/m^2). Auto exposure asked for 11.5, got clamped to
    # 8.0, and every frame shipped 3.5 STOPS — 11x — overexposed.
    #
    # That is the whole "white haze": measured off the packaged build, mean luminance was 221/255
    # with 90% of pixels above 200 and the 5th percentile at 191 — the entire image squeezed into
    # the top fifth of the range. The orange back wall looked like the one surviving colour only
    # because its albedo is (0.62, 0.20, 0.09): its RED channel was pinned at 255 like everything
    # else, and green and blue were what was left. It was clipping too.
    #
    # WHY PIN IT RATHER THAN WIDEN IT. Three reasons, all specific to this build:
    #  1. r.EyeAdaptation.* console vars do NOT rescue it. Verified: EV overrides of 11/12/13/14
    #     passed via -ExecCmds changed the frame by less than 0.2/255. A volume that overrides
    #     exposure wins, so the lever has to be the volume.
    #  2. One camera, one fixed shot, one light rig. There is nothing for adaptation to adapt to,
    #     so every stop it can move is a stop of unpredictability, not of robustness.
    #  3. Auto exposure silently RE-GRADES a lighting change away. Docs/SCENE-AUTHORING.md records
    #     halving the troffer emissive and measuring the frame come back BRIGHTER; that is the
    #     histogram re-metering, not the lights. Pinned, luminance means something across builds.
    # min == max is the supported way to say "fixed exposure" in a volume, and it converges on
    # frame ZERO — which is why an offscreen single-frame render is now trustworthy.
    #
    # TUNING IT. +1 EV halves the image, -1 doubles it. This value is NOT independent of the
    # lights above — it is solved for them, so changing either without re-measuring the other
    # ships a wrong frame. Re-derive it by capturing a frame and landing the mean near 120/255
    # with nothing above 240: that is a lit interior through the filmic tonemapper, with the
    # highlights still on the curve. 9.4 was measured, not assumed (9.7 came back at mean 112 and
    # lost the highlights; 8.0 was the bug, at 221). Judge it on a captured frame from the game,
    # never in an editor viewport, which is neither GI-off nor governed by this volume.
    EXPOSURE_EV100 = 9.4
    ppv = spawn(unreal.PostProcessVolume, (0,0,0))
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_method", True)
    s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    s.set_editor_property("override_auto_exposure_min_brightness", True)
    s.set_editor_property("auto_exposure_min_brightness", EXPOSURE_EV100)
    s.set_editor_property("override_auto_exposure_max_brightness", True)
    s.set_editor_property("auto_exposure_max_brightness", EXPOSURE_EV100)
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", 0.0)
    ppv.set_editor_property("settings", s)
    w("lights ok (no-GI: dir=8lux, 9-light ceiling grid @z=%d + accents, "
      "exposure PINNED at EV100 %.2f)" % (GRID_Z, EXPOSURE_EV100))

    # ---------- room shell ----------
    plane = unreal.load_asset('/Engine/BasicShapes/Plane')
    cube  = unreal.load_asset('/Engine/BasicShapes/Cube')
    # The room is DEEP on purpose. The shot is a portrait call framed on the resident, and what
    # sells "inside a mainframe centre" is the receding rank of cabinets behind her — a wall 2.7m
    # back (the old x=600) gave her a flat backdrop and nowhere for perspective to go.
    # Surface colours follow the Old IBM Datacenter reference: WHITE TILE floor, ORANGE back
    # wall, pale walls and ceiling. With GI disabled these are pure albedo — they do not light
    # anything, so the orange bounce is placed explicitly as point lights above.
    def paint(actor, name, rgb, rough=0.55):
        """Constant-colour set material. The engine's default grey is what made the old room read
        as untextured concrete regardless of lighting.

        The NAME is passed in rather than derived from the colour. Deriving it meant two surfaces
        sharing a colour (the two side walls) asked create_asset for the same package path twice,
        and the second call trips a fatal C++ assertion in SharedPointer.h that Python cannot
        catch — the whole authoring commandlet died with appError, after the try/except had
        already been written to make this "safe"."""
        try:
            mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                name, "/Game/Diorama/SetMats", unreal.Material, unreal.MaterialFactoryNew())
            if not mat:
                w("paint: create_asset returned None for %s" % name); return
            col = unreal.MaterialEditingLibrary.create_material_expression(
                mat, unreal.MaterialExpressionVectorParameter, -380, 0)
            col.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            if not unreal.MaterialEditingLibrary.connect_material_property(
                    col, "RGB", unreal.MaterialProperty.MP_BASE_COLOR):
                unreal.MaterialEditingLibrary.connect_material_property(
                    col, "", unreal.MaterialProperty.MP_BASE_COLOR)
            r = unreal.MaterialEditingLibrary.create_material_expression(
                mat, unreal.MaterialExpressionConstant, -380, 250)
            r.set_editor_property("r", rough)
            unreal.MaterialEditingLibrary.connect_material_property(
                r, "", unreal.MaterialProperty.MP_ROUGHNESS)
            unreal.MaterialEditingLibrary.recompile_material(mat)
            actor.static_mesh_component.set_material(0, mat)
        except Exception as e:
            w("paint warn: %s" % e)


    # ---------- Companion Intelligence brand tokens ----------
    # Source: companionintelligence/CI-Common -> assets/brand.config.json + docs/colors.md.
    # CI teal #0f717a is the brand's load-bearing colour and navy #041620 its ground. Both sit
    # COOL, which is why they read against warm Olivetti amber instead of muddying it: the set is
    # warm at eye level and CI-cool in the machine light and the deep background. Mint #58ebbf is
    # the accent and is used sparingly -- it is the brightest thing in frame wherever it appears.
    def srgb(h):
        h = h.lstrip("#")
        f = [int(h[i:i+2], 16) / 255.0 for i in (0, 2, 4)]
        # approximate sRGB -> linear so UE does not wash the brand colours out
        return tuple((c / 12.92) if c <= 0.04045 else (((c + 0.055) / 1.055) ** 2.4) for c in f)
    CI_TEAL   = srgb("#0f717a")
    CI_TEAL_L = srgb("#abd4d8")
    CI_NAVY   = srgb("#041620")
    CI_MINT   = srgb("#58ebbf")

    floor = spawn(unreal.StaticMeshActor, (450,0,0))
    floor.static_mesh_component.set_static_mesh(plane)
    floor.set_actor_scale3d(unreal.Vector(30,15,1))
    paint(floor, "M_SetFloor", (0.82, 0.82, 0.80), 0.35)                # white raised-floor tile
    back = spawn(unreal.StaticMeshActor, (1750,0,250))    # back wall, now ~14m behind her
    back.static_mesh_component.set_static_mesh(cube)
    back.set_actor_scale3d(unreal.Vector(0.2,15,5))
    paint(back, "M_SetBackWall", (0.62, 0.20, 0.09), 0.75)                 # the signature IBM orange wall
    left = spawn(unreal.StaticMeshActor, (450,-720,250))  # left wall
    left.static_mesh_component.set_static_mesh(cube)
    left.set_actor_scale3d(unreal.Vector(28,0.2,5))
    paint(left, "M_SetWallL", CI_TEAL, 0.7)      # CI teal, not neutral grey
    right = spawn(unreal.StaticMeshActor, (450,720,250))  # right wall (closes the set)
    right.static_mesh_component.set_static_mesh(cube)
    right.set_actor_scale3d(unreal.Vector(28,0.2,5))
    paint(right, "M_SetWallR", CI_TEAL, 0.7)
    # Ceiling: r.SkyAtmosphere is disabled on the stream, so anything above the walls renders
    # as dead black sky. A real ceiling both closes the room and bounces nothing (no GI) —
    # it is there to frame the shot, not to light it.
    ceil = spawn(unreal.StaticMeshActor, (450,0,500))
    ceil.static_mesh_component.set_static_mesh(cube)
    ceil.set_actor_scale3d(unreal.Vector(28,15,0.2))
    paint(ceil, "M_SetCeiling", (0.90, 0.90, 0.88), 0.8)                  # pale troffer ceiling
    w("shell ok (walls + ceiling)")

    # ---------- reference match: fluorescent troffers + operator desk ----------
    # From the Old IBM Datacenter reference set. Two details do most of the work in those images
    # and neither is prop geometry, which is why they are worth doing even though we do not own
    # that asset pack: the ceiling is a GRID OF LIT TROFFERS (bright rectangles reading as light
    # SOURCES, not a pale surface), and the operator sits at a white console desk in the
    # foreground with the tape ranks behind. Our ceiling was a flat pale slab, so with GI off it
    # read as grey card -- lit by nothing, lighting nothing.
    def emissive(actor, name, rgb, strength):
        try:
            mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                name, "/Game/Diorama/Mat", unreal.Material, unreal.MaterialFactoryNew())
            MELx = unreal.MaterialEditingLibrary
            col = MELx.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -380, 0)
            col.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            mul = MELx.create_material_expression(mat, unreal.MaterialExpressionMultiply, -180, 0)
            amt = MELx.create_material_expression(mat, unreal.MaterialExpressionConstant, -380, 160)
            amt.set_editor_property("r", strength)
            MELx.connect_material_expressions(col, "", mul, "A")
            MELx.connect_material_expressions(amt, "", mul, "B")
            MELx.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            MELx.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
            MELx.recompile_material(mat)
            actor.static_mesh_component.set_material(0, mat)
        except Exception as e:
            w("emissive warn: %s" % e)

    # Troffer bays, slightly BELOW the ceiling slab so they are visible as fittings rather than
    # co-planar z-fighting with it. Cool white, matching the reference's fluorescent tubes.
    #
    # TROFFER_EMISSIVE: 7.0 clipped. An emissive that far above the scene's exposed range does
    # not read as "a bright light" — it reads as a hole, because every troffer pixel pins at 255
    # and the fitting loses its own edges and the pale ceiling around it goes with them under
    # bloom. The reference photographs have troffers that are clearly the brightest thing in
    # frame while STILL showing their tube divisions. 3.8 keeps them the brightest surface in the
    # room without saturating, so the fittings read as fittings. Tune this by looking at a frame,
    # never in an editor preview: the stream runs with GI off and its own auto-exposure curve.
    TROFFER_EMISSIVE = 3.8
    troffers = 0
    for tx in range(0, 6):
        for ty in (-1, 0, 1):
            t = spawn(unreal.StaticMeshActor, (60 + tx * 300, ty * 380, 492))
            t.static_mesh_component.set_static_mesh(cube)
            t.set_actor_scale3d(unreal.Vector(2.2, 0.55, 0.06))
            emissive(t, "M_Troffer_%d_%d" % (tx, ty + 1), (0.95, 0.97, 1.00), TROFFER_EMISSIVE)
            troffers += 1
    w("troffers placed: %d" % troffers)

    # Operator console desk, set BETWEEN the camera and her so the shot reads as a workstation in
    # a machine room rather than a portrait taken in one. She stands behind it; the desk crops at
    # the bottom of a 9:16 frame exactly where the reference desks do.
    console = spawn(unreal.StaticMeshActor, (250, 20, 68))
    console.static_mesh_component.set_static_mesh(cube)
    console.set_actor_scale3d(unreal.Vector(1.5, 2.6, 0.10))
    paint(console, "M_ConsoleTop", (0.88, 0.87, 0.84), 0.30)   # white console laminate
    for i, dy in enumerate((-95, 95)):
        ped = spawn(unreal.StaticMeshActor, (250, 20 + dy, 33))
        ped.static_mesh_component.set_static_mesh(cube)
        ped.set_actor_scale3d(unreal.Vector(1.2, 0.5, 0.66))
        paint(ped, "M_ConsolePed_%d" % i, (0.30, 0.30, 0.31), 0.55)
    w("operator console placed")

    # ---------- Olivetti + IBM + Cray: vector displays, warm grade, curved bench ----------
    # The brief is a 70s mainframe STOREFRONT, not a museum floor: Olivetti warmth (amber, cream,
    # rounded forms) over IBM structure (orange wall, blue panels, white tile), with Cray's
    # signature curved padded bench. The reference storefront photograph reads warm because the
    # interior is lit AMBER against a dark exterior -- so the fluorescents stay cool and tight
    # while everything at eye level is warm. With GI off, that contrast has to be placed by hand.
    #
    # Vector displays matter more than their size suggests. Every machine in the reference has a
    # lit face, and a dark rectangle in a bright room reads as switched-off equipment -- exactly
    # the "museum after hours" feel we are trying to avoid. Phosphor green and amber are the
    # period-correct CRT colours and they are the only saturated light in the room.
    VECTOR_GREEN = CI_MINT          # CI mint doubles as period phosphor green
    VECTOR_AMBER = (1.00, 0.62, 0.12)
    panels = 0
    for i, (px, py, pz, col) in enumerate([
            ( 470, -300, 150, VECTOR_GREEN),   # 1401 CPU face
            ( 470, -160, 145, VECTOR_AMBER),   # 1401 mainframe
            ( 470,  210, 130, VECTOR_GREEN),   # 3350 disk
            ( 440,  370, 155, VECTOR_AMBER),   # 2401 tape
            ( 440, -360, 140, VECTOR_GREEN),   # RAMAC
            ( 245,   20,  74, VECTOR_AMBER)]): # the operator console she stands behind
        panel = spawn(unreal.StaticMeshActor, (px, py, pz), yaw=90.0)
        panel.static_mesh_component.set_static_mesh(cube)
        panel.set_actor_scale3d(unreal.Vector(0.06, 0.42, 0.16))
        emissive(panel, "M_Vector_%d" % i, col, 4.5)
        panels += 1
    w("vector display panels: %d" % panels)

    # Cray-1 bench: the curved padded seat ring that made that machine iconic. Approximated as a
    # short arc of segments -- close enough at this distance, and it puts a warm horizontal mass
    # low in frame where the reference has one.
    import math as _m
    for seg in range(7):
        ang = _m.radians(-52 + seg * 17)
        bx = 690 + 150 * _m.cos(ang)
        by = -40 + 150 * _m.sin(ang)
        bench = spawn(unreal.StaticMeshActor, (bx, by, 44), yaw=_m.degrees(ang) + 90.0)
        bench.static_mesh_component.set_static_mesh(cube)
        bench.set_actor_scale3d(unreal.Vector(0.42, 0.62, 0.42))
        # Olivetti oxblood/tan leatherette, the warm mass the palette needs at seat height.
        paint(bench, "M_Bench_%d" % seg, (0.42, 0.14, 0.09), 0.65)
    w("cray bench ring placed")

    # Warm eye-level wash. The ceiling troffers stay cool and overhead; these sit low and amber so
    # faces and cabinet fronts pick up Olivetti warmth rather than clinical white.
    #
    # At 5200cd/r=1500 these four were the largest single block of light in the front of the room
    # AND the most saturated (1.00, 0.72, 0.42), and a 15m radius from z=205 put that amber on the
    # CEILING and the white raised-floor tile as readily as on a face. Measured off the frame, the
    # "white tile" floor came back (116, 80, 64) and the pale ceiling came back pink. That is the
    # reference look inverted: it wants a cool fluorescent room WITH a warm eye-level note, not an
    # amber room. Held to r=850 the wash stays at eye level, where the note belongs.
    for wx, wy in ((300, -260), (300, 300), (620, -120), (620, 160)):
        point((wx, wy, 205), (1.00, 0.72, 0.42), 1700, 850.0)
    w("warm eye-level wash placed")

    # CI-cool rim from deep in the hall. Warm at eye level, cool in the distance -- that split is
    # what gives the storefront reference its depth, and it lets the brand teal register as light
    # rather than only as paint.
    # r=2200 from x=1500 reached back past the camera at x=170, which is the one thing this light
    # must not do: a "distance" cue that also lands on the foreground is not a distance cue.
    for ry in (-300, 0, 300):
        point((1500, ry, 300), CI_TEAL_L, 1400, 1400.0)
    w("CI-cool depth rim placed")

    # ---------- desk under the small artifacts ----------
    desk = spawn(unreal.StaticMeshActor, (115,-320,66))
    desk.static_mesh_component.set_static_mesh(cube)
    desk.set_actor_scale3d(unreal.Vector(1.6,1.4,0.12))   # 160x140cm top at ~72cm
    w("desk ok")

    # ---------- the 12 props: measure -> normalize scale -> ground -> place ----------
    # One placement routine, used both for the hero props and for the repeated hall behind them,
    # so a duplicated cabinet is normalized and grounded by exactly the same rules as the original.
    def place_prop(name, pos, yaw, target_cm, label=None):
        sm = imported.get(name)
        if not sm:
            w("SKIP place %s (no mesh)" % name); return False
        a = spawn(unreal.StaticMeshActor, pos, yaw=yaw)
        a.static_mesh_component.set_static_mesh(sm)
        a.set_actor_label(label or ("prop_" + name))
        # normalize from MEASURED bounds so the source file's unit scale can't matter
        box = sm.get_bounding_box()
        size = box.max - box.min
        max_dim = max(size.x, size.y, size.z)
        sc = (target_cm / max_dim) if max_dim > 0.001 else 1.0
        a.set_actor_scale3d(unreal.Vector(sc, sc, sc))
        # sit the mesh's base exactly on its placement height (no float/sink)
        a.set_actor_location(unreal.Vector(pos[0], pos[1], pos[2] - box.min.z * sc), False, False)
        return True

    placed = 0
    for name, glb, pos, yaw, target_cm in PROPS:
        if place_prop(name, pos, yaw, target_cm):
            placed += 1
    w("placed %d/12 hero props" % placed)

    # ---------- re-home the glTF prop materials into /Game ----------
    # THE reason the props render pale. Interchange parents every imported material instance to
    #   /InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque_DS
    # which is PLUGIN content, not /Game. The cook stages /Game, so in the shipped build that
    # parent is missing, every instance falls back to the engine default, and each prop's real
    # colour -- carried in BaseColorFactor, e.g. (0.15,0.37,0.64) IBM blue, (0.80,0.41,0.10)
    # orange, (0.31,0.31,0.31) charcoal -- is silently discarded. It looks exactly like "textures
    # failed to load", which is why it went unexplained for so long: the assets are all present and
    # correct in the editor.
    #
    # The master below deliberately declares parameters with the SAME NAMES Interchange used
    # (BaseColorTexture / BaseColorFactor / MetallicFactor / RoughnessFactor). Re-parenting then
    # preserves every per-prop override automatically -- no copying values, nothing to drift.
    try:
        MEL2 = unreal.MaterialEditingLibrary
        tools2 = unreal.AssetToolsHelpers.get_asset_tools()
        M_Prop = tools2.create_asset("M_PropGLTF", "/Game/Diorama/Mat", unreal.Material,
                                     unreal.MaterialFactoryNew())
        tex = MEL2.create_material_expression(M_Prop, unreal.MaterialExpressionTextureSampleParameter2D, -600, -120)
        tex.set_editor_property("parameter_name", "BaseColorTexture")
        white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
        if white:
            tex.set_editor_property("texture", white)
        fac = MEL2.create_material_expression(M_Prop, unreal.MaterialExpressionVectorParameter, -600, 120)
        fac.set_editor_property("parameter_name", "BaseColorFactor")
        fac.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
        mul = MEL2.create_material_expression(M_Prop, unreal.MaterialExpressionMultiply, -320, 0)
        MEL2.connect_material_expressions(tex, "RGB", mul, "A")
        MEL2.connect_material_expressions(fac, "RGB", mul, "B")
        MEL2.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)

        rough = MEL2.create_material_expression(M_Prop, unreal.MaterialExpressionScalarParameter, -600, 300)
        rough.set_editor_property("parameter_name", "RoughnessFactor")
        rough.set_editor_property("default_value", 0.62)   # period enamel: matte, not plastic
        MEL2.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        metal = MEL2.create_material_expression(M_Prop, unreal.MaterialExpressionScalarParameter, -600, 420)
        metal.set_editor_property("parameter_name", "MetallicFactor")
        metal.set_editor_property("default_value", 0.0)
        MEL2.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
        MEL2.recompile_material(M_Prop)

        ar3 = unreal.AssetRegistryHelpers.get_asset_registry()
        reparented = 0
        for a in ar3.get_assets_by_path("/Game/Diorama", recursive=True):
            if str(a.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            mi = unreal.load_asset(str(a.package_name))
            if not mi:
                continue
            parent = mi.get_editor_property("parent")
            # Only rescue the ones pointing outside /Game; leave anything already local alone.
            if parent and not parent.get_path_name().startswith("/Game"):
                mi.set_editor_property("parent", M_Prop)
                unreal.EditorAssetLibrary.save_loaded_asset(mi)
                reparented += 1
        w("prop materials re-homed to /Game: %d re-parented onto M_PropGLTF" % reparented)
    except Exception as e:
        import traceback as _t3
        w("PROP MATERIAL REPARENT FAILED (props will stay pale): %s | %s" % (e, _t3.format_exc()[:300]))

    # ---------- the machine hall ----------
    # Twelve museum pieces read as a museum, not as a working computer room. A 1970s installation
    # was RANKS of identical cabinets down an aisle, and that repetition — plus the perspective it
    # creates — is what makes the background of a close portrait read as "inside a mainframe
    # centre" rather than "in front of a machine".
    #
    # These reuse the already-imported meshes, so repeating costs no import time and no extra
    # memory beyond the actors. Two banks flank a centre aisle running down +X directly behind the
    # resident, so the aisle recedes past her shoulders in the portrait shot; two outer rows add a
    # second layer of depth behind those.
    # Aisle width and START DISTANCE are the composition. The first attempt put the banks at
    # y=-270/+310 starting 3m behind her, and in a portrait crop they shrank to little boxes at
    # the frame edge -- the shot read as an empty room. The reference photographs work because
    # the cabinets are CLOSE and TALL enough to fill the background behind a person.
    #
    # A real 1401 aisle was roughly 3m; these banks sit ~1.6m either side of the centre line and
    # begin ~0.9m behind her, so the nearest cabinets flank her shoulders and the rank converges
    # into frame as it recedes.
    HALL = []
    for i, x in enumerate((420, 600, 780, 980, 1200, 1440)):
        HALL.append((("ibm2401", "ibm3350", "ibm1401cpu")[i % 3], x, -155,  90, (183, 150, 170)[i % 3]))
        HALL.append((("ibm1401", "ibm2401", "ibm3350")[i % 3],    x,  195, -90, (170, 183, 150)[i % 3]))
    for i, x in enumerate((520, 760, 1020, 1300)):
        # Second rank behind the first, giving depth past the near cabinets.
        HALL.append((("ibm2401", "ibm3350")[i % 2], x, -430,  90, (183, 150)[i % 2]))
        HALL.append((("ibm3350", "ibm2401")[i % 2], x,  470, -90, (150, 183)[i % 2]))
    # A far rank across the end of the hall, closing the vista.
    HALL.append(("ramac305", 1690, -120, 180, 170))
    HALL.append(("ibm1401",  1690,  160, 180, 170))

    hall = 0
    for name, hx, hy, hyaw, tgt in HALL:
        if place_prop(name, (hx, hy, 0), hyaw, tgt, label="hall_%s_%d_%d" % (name, hx, hy)):
            hall += 1
    w("machine hall: %d repeated cabinets" % hall)

    # The hall needs its own light or it falls off into black past the hero row — the original
    # ceiling grid only covered the front of the set.
    # Same two corrections as the front grid: off the ceiling (GRID_Z, not 430) so the slab stops
    # being the brightest thing in the room, and off r=2400, which spanned the entire hall and
    # made fifteen lights behave as one flat ambient with no depth cue in it.
    for hx in (450, 700, 950, 1200, 1500):
        point((hx, 0, GRID_Z), (0.93, 0.96, 1.00), 400, 1500.0)   # troffer run down the aisle
        point((hx, -300, 300), (0.90, 0.94, 1.00), 1900, 950.0)   # cool fill, left rank
        point((hx,  340, 300), (0.90, 0.94, 1.00), 1900, 950.0)   # cool fill, right rank


    # ---------- IBM 729 tape units (authored in Blender, not scavenged) ----------
    # The scavenged props had no tape machine in them, so the hall was 20 featureless slabs. This
    # GLB is modelled to real 729 proportions (0.76 x 0.74 x 1.80m) with the reference palette.
    # Its two reels are SEPARATE objects with their own origins: that is the whole point, because a
    # reel baked into the shell looks identical in a still and can never turn. Spinning tape is the
    # single most recognisable motion in a machine room of this era.
    try:
        # import_prop returns (mesh, texture_count). Binding the whole TUPLE here is what left
        # the hall with no tape machines at all: `if t_body:` is true for a tuple, so the code
        # read as working, and set_static_mesh then raised a nativize TypeError that the outer
        # except swallowed into a one-line warning -- before a single reel actor was spawned.
        # That is the "Tape reels spinning: 0" in the runtime log.
        t_body, _t_body_tex = import_prop("ibm729", "ibm-729-tape.glb")
        reel_meshes = []
        for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(
                "/Game/Diorama/ibm729", recursive=True):
            if str(a.asset_class_path.asset_name) == "StaticMesh" and "Reel" in str(a.asset_name):
                reel_meshes.append(unreal.load_asset(str(a.package_name)))
        w("729: body=%s reels=%d" % (t_body is not None, len(reel_meshes)))

        placed729 = 0
        for bank_y, face in ((-155.0, 90.0), (195.0, -90.0)):
            for i in range(6):
                bx = 560.0 + i * 190.0
                if t_body:
                    a = spawn(unreal.StaticMeshActor, (bx, bank_y, 0), yaw=face)
                    a.static_mesh_component.set_static_mesh(t_body)
                    a.set_actor_label("IBM729_%s_%d" % ("L" if bank_y < 0 else "R", i))
                    placed729 += 1
                # Reels ride as their own actors at the same transform, tagged for the spinner.
                for rm in reel_meshes:
                    r = spawn(unreal.StaticMeshActor, (bx, bank_y, 0), yaw=face)
                    r.static_mesh_component.set_static_mesh(rm)
                    r.tags = ["TapeReel"]
                    r.set_actor_label("Reel_%s_%d_%s" % ("L" if bank_y < 0 else "R", i, rm.get_name()))
        w("729 tape units placed: %d bodies, %d reel actors" % (placed729, placed729 * len(reel_meshes)))
    except Exception as e:
        import traceback as _t
        w("729 PLACEMENT WARN: %s" % e)

    # ---------- the resident: MetaHuman "Seo" ----------
    # Converted from mhc_seo.mhpkg (5.6 EditableCharacter): assembled in a local 5.8 editor,
    # exported Face+Body+Outfit to FBX (version-agnostic), re-imported HERE into 5.5 as
    # skeletal meshes. Face carries 849 morph targets = the MetaHuman facial-blendshape rig
    # (the future viseme source). Placed at the camera's look target.
    # RES_YAW: a re-imported MetaHuman FBX has its visual front 90deg off the actor's forward
    # axis, so yaw 180 rendered her in profile. The earlier -90 was 180 degrees out: it turned
    # her BACK to the StageCamera. Confirmed from the streamed frame — the head showed both ears
    # and no face at all. (That went unnoticed while she was still untextured gray, which is why
    # it was previously recorded as "verified".) +90 faces her at the camera. Always confirm
    # facing by looking for EYES in a captured frame, not by reasoning about the forward vector.
    RES_POS, RES_YAW = (330, 20, 0), 90.0

    # ---------- preferred resident: Reallusion Character Creator "Nia" ----------
    # Nia ships as a UE 5.3 project with NO plugin dependencies and PLAIN SkeletalMesh /
    # Texture2D / AnimSequence assets, so UE 5.5 loads them directly — no FBX round-trip, and
    # we keep her REAL PBR materials (M_HQSkin + per-region instances + SSS profiles) instead
    # of the flat constant colours Seo needs. Those materials are properly authored by
    # Reallusion, so they already carry the skeletal-mesh usage flags we had to add by hand.
    # Her folder structure is preserved verbatim under /Game/CC_ControlRig_Nia/... because UE
    # assets reference each other by PACKAGE PATH — relocating them silently breaks the refs.
    # Her CC Control Rig is a Blueprint and is deliberately NOT used: it would not survive the
    # Linux cook. A baked AnimSequence played on the component does, and gives us the idle.
    NIA_SK   = "/Game/CC_ControlRig_Nia/RLContent/Nia/SK_Nia"
    NIA_ANIM = ["/Game/CC_ControlRig_Nia/RLContent/Nia/Motions/MF_Idle",
                "/Game/CC_ControlRig_Nia/RLContent/Nia/Motions/MM_Idle"]
    # Facing, measured rather than assumed. At actor yaw 180 the streamed frame showed her in
    # LEFT PROFILE, nose toward screen-left. The camera looks along +X with +Z up, so screen-left
    # is -Y (visual yaw 270) => her mesh carries a +90 intrinsic offset (visual = actor + 90).
    # Facing the camera means visual = 180 (-X), so actor = 90. (Same offset Seo's export had.)
    # ALWAYS confirm facing by finding EYES in a captured frame, never by reasoning alone.
    NIA_YAW = 90.0
    nia_placed = False
    try:
        nia = unreal.load_asset(NIA_SK)
        if nia:
            a = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=NIA_YAW)
            a.set_actor_label("Nia")
            # TAG, not label: actor labels are EDITOR-ONLY and do not survive the cook, so a
            # label-based lookup works in the editor and silently finds nothing in the shipped
            # build. UCompanionFaceSubsystem finds the resident by this tag at BeginPlay and
            # attaches the face driver — which is how behaviour reaches a map that is authored
            # in a content-only project and therefore cannot reference plugin C++ or Blueprints.
            a.set_editor_property("tags", ["CompanionFace"])
            comp = a.skeletal_mesh_component
            comp.set_skeletal_mesh(nia)
            try:
                bb = nia.get_bounds().box_extent
                # MEASURE the eyeline instead of guessing it. The first portrait attempt aimed at
                # a hand-picked z=152 and framed her chest with the head cut off, because she is
                # ~193cm in heels rather than the ~175cm assumed. Eyes sit at ~93% of stature.
                NIA_EYE_Z = (bb.z * 2.0) * 0.93
                w("Nia: half-height=%.1fcm -> stature ~%.0fcm, eyeline z=%.0fcm"
                  % (bb.z, bb.z * 2.0, NIA_EYE_Z))
            except Exception:
                pass
            mats = nia.get_editor_property("materials")
            w("Nia: %d material slots, e.g. %s" % (len(mats), ",".join(
                (s.get_editor_property("material_interface").get_name()
                 if s.get_editor_property("material_interface") else "NONE") for s in mats[:4])))
            # Idle animation. Set animation_data as a WHOLE struct — mutating the struct
            # returned by get_editor_property does not persist (same trap as the materials array).
            anim = next((x for x in (unreal.load_asset(p) for p in NIA_ANIM) if x), None)
            if anim:
                comp.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                ad = comp.get_editor_property("animation_data")
                ad.set_editor_property("anim_to_play", anim)
                ad.set_editor_property("saved_looping", True)
                ad.set_editor_property("saved_playing", True)
                comp.set_editor_property("animation_data", ad)
                back = comp.get_editor_property("animation_data").get_editor_property("anim_to_play")
                w("Nia: idle anim = %s (persisted=%s)" % (anim.get_name(), back.get_name() if back else "NONE"))
            else:
                w("Nia: NO idle anim found (tried %s) — she will stand in bind pose" % NIA_ANIM)
            nia_placed = True
            w("resident: NIA placed at %s yaw %.0f" % (RES_POS, NIA_YAW))
        else:
            w("Nia: %s not found — falling back to the MetaHuman Seo path" % NIA_SK)
    except Exception as e:
        import traceback as _tb
        w("NIA ERROR (falling back to Seo): %s\n%s" % (e, _tb.format_exc()[:600]))

    def import_skel(fbx, dest):
        ui = unreal.FbxImportUI()
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("import_materials", True)
        ui.set_editor_property("import_textures", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        ui.skeletal_mesh_import_data.set_editor_property("import_morph_targets", True)
        ui.skeletal_mesh_import_data.set_editor_property("convert_scene", True)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", fbx)
        t.set_editor_property("destination_path", dest)
        t.set_editor_property("automated", True); t.set_editor_property("save", True)
        t.set_editor_property("replace_existing", True); t.set_editor_property("options", ui)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
        ar2 = unreal.AssetRegistryHelpers.get_asset_registry()
        for a in ar2.get_assets_by_path(dest, recursive=True):
            if str(a.asset_class_path.asset_name) == "SkeletalMesh":
                return unreal.load_asset(str(a.package_name))
        return None

    # --- skin materials: wire the exported baked textures onto the MetaHuman slots ---
    # A simplified skin (baked base color -> a lit opaque material), plus a masked "hidden"
    # material for the transparent overlay slots (eyeshell/eyelashes/lacrimal/M_Hide) so none
    # render as white. Not the full MetaHuman skin shader, but turns her from gray to skinned
    # and removes the white body-garment slab (all under the one Body slot).
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    MEL = unreal.MaterialEditingLibrary
    seo_mats = {}
    try:
        def imp_tex(name):
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", MH_DIR + "/tex/%s.tga" % name)
            t.set_editor_property("destination_path", "/Game/Seo/Tex")
            t.set_editor_property("automated", True); t.set_editor_property("save", True)
            t.set_editor_property("replace_existing", True)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
            return unreal.load_asset("/Game/Seo/Tex/%s" % name)
        # The exported MetaHuman baked textures are un-tintable neutral bakes (T_Head_BC is flat
        # gray 94,94,94 — meant to be tinted by the full MetaHuman skin shader we don't have), so
        # a plain texture sample renders gray. Use a CONSTANT skin-tone color instead: robust,
        # guaranteed non-gray, and a clean base to iterate the real skin material from later.
        M_Skin = tools.create_asset("M_SeoSkin", "/Game/Seo/Mat", unreal.Material, unreal.MaterialFactoryNew())
        cp = MEL.create_material_expression(M_Skin, unreal.MaterialExpressionVectorParameter, -380, 0)
        cp.set_editor_property("parameter_name", "BaseColor")
        cp.set_editor_property("default_value", unreal.LinearColor(0.80, 0.58, 0.50, 1.0))
        cok = MEL.connect_material_property(cp, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        if not cok:
            cok = MEL.connect_material_property(cp, "", unreal.MaterialProperty.MP_BASE_COLOR)
        w("  M_SeoSkin base-color connect ok=%s" % cok)
        rg = MEL.create_material_expression(M_Skin, unreal.MaterialExpressionConstant, -380, 250)
        rg.set_editor_property("r", 0.55)
        MEL.connect_material_property(rg, "", unreal.MaterialProperty.MP_ROUGHNESS)
        # *** THE gray-render root cause ***: a material created via MaterialFactoryNew has NO
        # usage flags. Applying it to a SkeletalMesh with the skeletal-mesh variant uncompiled
        # makes UE fall back to the DEFAULT (gray) material at runtime/cook (logs
        # "Material with missing usage flag was applied to skeletal mesh") — which is why every
        # prior attempt (component override, fresh SkeletalMaterial structs, textured, constant
        # color) rendered pixel-identical gray. Flag both usages so the SM variants get compiled
        # into the cook. Morph targets: the Face carries 849 (the viseme rig) so it needs it too.
        for _flag in ("used_with_skeletal_mesh", "used_with_morph_targets"):
            M_Skin.set_editor_property(_flag, True)
        MEL.recompile_material(M_Skin)
        w("  M_SeoSkin usage: skel=%s morph=%s" % (
            M_Skin.get_editor_property("used_with_skeletal_mesh"),
            M_Skin.get_editor_property("used_with_morph_targets")))

        # masked "hidden" material (opacity mask 0) for the transparent overlay slots
        M_Hidden = tools.create_asset("M_SeoHidden", "/Game/Seo/Mat", unreal.Material, unreal.MaterialFactoryNew())
        M_Hidden.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        z = MEL.create_material_expression(M_Hidden, unreal.MaterialExpressionConstant, -380, 0)
        z.set_editor_property("r", 0.0)
        MEL.connect_material_property(z, "", unreal.MaterialProperty.MP_OPACITY_MASK)
        for _flag in ("used_with_skeletal_mesh", "used_with_morph_targets"):
            M_Hidden.set_editor_property(_flag, True)
        MEL.recompile_material(M_Hidden)

        def mic(name, color):
            m = tools.create_asset(name, "/Game/Seo/Mat", unreal.MaterialInstanceConstant,
                                   unreal.MaterialInstanceConstantFactoryNew())
            m.set_editor_property("parent", M_Skin)
            MEL.set_material_instance_vector_parameter_value(m, "BaseColor", unreal.LinearColor(*color))
            return m
        seo_mats = {
            "head":  mic("MI_SeoHead",  (0.82, 0.60, 0.52, 1.0)),  # skin
            "body":  mic("MI_SeoBody",  (0.80, 0.58, 0.50, 1.0)),  # skin
            "teeth": mic("MI_SeoTeeth", (0.90, 0.88, 0.82, 1.0)),  # off-white
            "eye":   mic("MI_SeoEye",   (0.10, 0.08, 0.07, 1.0)),  # dark iris
            "hidden": M_Hidden,
            # Garment. Reusing M_SeoSkin as the parent is deliberate: it already carries the
            # skeletal-mesh/morph-target usage flags, without which the outfit would render as
            # the default gray exactly like the body did.
            "cloth_top": mic("MI_SeoClothTop", (0.30, 0.36, 0.40, 1.0)),  # slate shirt
            "cloth_leg": mic("MI_SeoClothLeg", (0.20, 0.23, 0.27, 1.0)),  # darker shorts
        }
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        w("skin materials built: %s" % list(seo_mats.keys()))
    except Exception as e:
        import traceback as _tb
        w("MATERIALS ERROR (mesh will stay gray): %s\n%s" % (e, _tb.format_exc()[:500]))

    # slot index -> material key, per the probed slot layout
    BODY_SLOTS = {0: "body"}
    FACE_SLOTS = {0: "head", 1: "teeth", 2: "hidden", 3: "eye", 4: "eye",
                  5: "hidden", 6: "hidden", 7: "hidden", 8: "hidden"}

    # The OUTFIT is not cosmetic — it is load-bearing geometry. MetaHuman removes the body mesh
    # that sits under clothing, so Body+Face alone render as head/neck + forearms + lower legs
    # with the torso, upper arms and thighs simply ABSENT (verified in the streamed frame; the
    # pale slab behind her there is a prop box, not a garment). The outfit fills that gap.
    # Its slot count/order isn't probed, so resolve by slot NAME rather than index: MetaHuman's
    # default garment slots are named ..._Shirt / ..._Short.
    def OUTFIT_SLOTS(idx, slot_name):
        return "cloth_leg" if "short" in str(slot_name).lower() else "cloth_top"

    seo_parts = [(MH_DIR + "/fbx/SKM_MHC_Seo_BodyMesh.fbx", "/Game/Seo/Body", BODY_SLOTS),
                 (MH_DIR + "/fbx/SKM_MHC_Seo_FaceMesh.fbx", "/Game/Seo/Face", FACE_SLOTS),
                 (MH_DIR + "/fbx/MHC_Seo_Outfits.fbx",      "/Game/Seo/Outfit", OUTFIT_SLOTS)]
    if nia_placed:
        seo_parts = []   # Nia is the resident; skip the Seo import entirely (saves ~2 min too)
    placed_seo = 0
    for fbx, dest, slotmap in seo_parts:
        m = import_skel(fbx, dest)
        if not m:
            w("resident: import FAIL %s" % fbx); continue
        # Assign materials by building FRESH SkeletalMaterial structs (mutating the array
        # returned by get_editor_property is a copy and does NOT persist — probed & confirmed
        # v16 stayed on defaults). Force-save the asset, then read back and log to verify.
        try:
            old = m.get_editor_property("materials")
            new = []
            for i, sm in enumerate(old):
                nm = unreal.SkeletalMaterial()
                slot_name = sm.get_editor_property("material_slot_name")
                nm.set_editor_property("material_slot_name", slot_name)
                key = slotmap(i, slot_name) if callable(slotmap) else slotmap.get(i)
                chosen = seo_mats.get(key)
                nm.set_editor_property("material_interface",
                                       chosen if chosen else sm.get_editor_property("material_interface"))
                new.append(nm)
            m.set_editor_property("materials", new)
            unreal.EditorAssetLibrary.save_loaded_asset(m)
            back = m.get_editor_property("materials")
            names = ",".join((s.get_editor_property("material_interface").get_name()
                              if s.get_editor_property("material_interface") else "NONE") for s in back)
            w("  %s materials now: %s" % (dest.split("/")[-1], names))
        except Exception as e:
            import traceback as _t
            w("  asset material assign warn: %s\n%s" % (e, _t.format_exc()[:400]))
        a = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=RES_YAW)
        a.set_actor_label("Seo_" + dest.split("/")[-1])
        a.skeletal_mesh_component.set_skeletal_mesh(m)
        placed_seo += 1
    if not nia_placed:
        w("resident: MetaHuman Seo placed (%d/%d parts, skinned+clothed) at %s yaw %.0f"
          % (placed_seo, len(seo_parts), RES_POS, RES_YAW))
    if placed_seo == 0 and not nia_placed:
        skm = unreal.load_asset('/MoverTests/Characters/Mannequins/Meshes/SKM_Manny')
        if skm:
            a = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=RES_YAW); a.set_actor_label("Resident")
            a.skeletal_mesh_component.set_skeletal_mesh(skm)
            w("resident: FELL BACK to SKM_Manny (Seo import failed)")

    # ---------- score + branding ----------
    # Both are authored into the MAP rather than driven from C++, because neither needs to react
    # to anything. An AmbientSound that loops and a sign that hangs on a wall have no runtime
    # decisions to make, and putting them in the level keeps them out of the cook-critical path.
    try:
        # Score: a 32s machine-room loop synthesized by Scripts/MakeAmbience.py. Mixed at about
        # -27dBFS so it sits UNDER Kokoro's speech; a bed loud enough to need ducking is a bed
        # mixed wrong, and ducking would mean C++ that has to know when she is talking.
        snd_task = unreal.AssetImportTask()
        snd_task.filename = AUDIO_DIR + "/machine_room.wav"
        snd_task.destination_path = "/Game/Audio"
        snd_task.automated = True; snd_task.save = True; snd_task.replace_existing = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([snd_task])
        amb = unreal.load_asset("/Game/Audio/machine_room")
        if amb:
            amb.set_editor_property("looping", True)
            unreal.EditorAssetLibrary.save_loaded_asset(amb)
            # Placed at the camera rather than in the room: this is a bed, not a point source, and
            # a positioned emitter would swing across the stereo field during the opening flythrough.
            snd_actor = spawn(unreal.AmbientSound, (330.0 - 160.0, 20.0, 170.0))
            snd_actor.set_actor_label("Ambience_MachineRoom")
            snd_actor.audio_component.set_sound(amb)
            snd_actor.audio_component.set_editor_property("volume_multiplier", 0.55)
            snd_actor.audio_component.set_editor_property("allow_spatialization", False)
            w("ambience: machine_room.wav looping, non-spatialized")
        else:
            w("ambience: import produced no SoundWave (continuing silent)")
    except Exception as e:
        w("ambience: skipped (%s)" % e)

    try:
        # Branding: a facility sign on the back wall, in the register of the era -- the reference
        # rooms all have the operator's name on the wall. TextRender needs no imported logo asset,
        # which matters because the cook tree carries no brand artwork.
        sign = spawn(unreal.TextRenderActor, (1748.0, 0.0, 330.0), yaw=180.0)
        sign.set_actor_label("Brand_Wordmark")
        trc = sign.text_render
        trc.set_text(unreal.Text("COMPANION INTELLIGENCE"))
        trc.set_editor_property("world_size", 26.0)
        trc.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        trc.set_editor_property("text_render_color", unreal.Color(236, 236, 232, 255))
        w("branding: wall wordmark placed")
    except Exception as e:
        w("branding: skipped (%s)" % e)


    # ---------- CRT vector displays + room tone ----------
    # Flat-colour screens read as painted plastic. A real scan-line trace is what makes a CRT look
    # POWERED, and it costs one texture. Emissive so it glows with GI off -- the whole set is lit
    # by explicit lights, so a screen that is merely bright-coloured would look dead beside them.
    try:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", GEN_DIR + "/T_CRT_Vector.png")
        t.set_editor_property("destination_path", "/Game/Diorama/Tex")
        t.set_editor_property("automated", True); t.set_editor_property("save", True)
        t.set_editor_property("replace_existing", True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
        crt_tex = unreal.load_asset("/Game/Diorama/Tex/T_CRT_Vector")
        if crt_tex:
            M_CRT = tools.create_asset("M_CRT", "/Game/Diorama/Mat", unreal.Material,
                                       unreal.MaterialFactoryNew())
            ts = MEL.create_material_expression(M_CRT, unreal.MaterialExpressionTextureSample, -420, 0)
            ts.set_editor_property("texture", crt_tex)
            MEL.connect_material_property(ts, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
            mul = MEL.create_material_expression(M_CRT, unreal.MaterialExpressionMultiply, -220, 180)
            MEL.connect_material_expressions(ts, "RGB", mul, "A")
            k = MEL.create_material_expression(M_CRT, unreal.MaterialExpressionConstant, -380, 240)
            k.set_editor_property("r", 6.0)
            MEL.connect_material_expressions(k, "", mul, "B")
            MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            for _f in ("used_with_static_lighting",):
                try: M_CRT.set_editor_property(_f, True)
                except Exception: pass
            MEL.recompile_material(M_CRT)
            # Face the screens down the aisle so the camera catches them on the fly-in.
            # COUNT the placements and report the count. The old line printed unconditionally, so
            # a run where /Engine/BasicShapes/Plane failed to load spawned three EMPTY actors and
            # still logged "CRT vector displays placed" -- the identical trap to the tuple-binding
            # bug that logged a working tape rank while "Tape reels spinning: 0" (see the 729
            # section above). A line that cannot say zero is not evidence of anything.
            pl = unreal.load_asset("/Engine/BasicShapes/Plane")
            placed_crt = 0
            for sx, sy, sz, syaw in ((520, -150, 120, 90), (520, 190, 120, -90), (760, -150, 120, 90)):
                if not pl:
                    break
                scr = spawn(unreal.StaticMeshActor, (sx, sy, sz), yaw=syaw)
                scr.static_mesh_component.set_static_mesh(pl)
                scr.set_actor_scale3d(unreal.Vector(0.34, 0.26, 1.0))
                scr.set_actor_rotation(unreal.Rotator(roll=90.0, pitch=0.0, yaw=syaw), False)
                scr.static_mesh_component.set_material(0, M_CRT)
                scr.set_actor_label("CRT_%d_%d" % (sx, sy))
                # Read the mesh BACK off the component. set_static_mesh on a Static-mobility
                # component is the step that can quietly do nothing, and it is the only one whose
                # failure leaves a correctly-placed, correctly-named, invisible actor behind.
                placed_crt += 1 if scr.static_mesh_component.static_mesh else 0
            w("CRT vector displays placed: %d of 3 (emissive scan-line trace)%s"
              % (placed_crt, "" if pl else " -- /Engine/BasicShapes/Plane did not load"))
        else:
            # SAY SO. The import failing produced no line at all before, so a build with dead
            # screens looked identical in the log to a build with lit ones, and the only hint was
            # a stray "There was nothing to import" among the engine's own noise.
            w("CRT displays: no texture imported from %s (screens left unlit)" % GEN_DIR)
    except Exception as e:
        w("CRT displays skipped: %s" % e)

    # Room tone. A machine hall is never silent, and the reference photographs SOUND like 60Hz
    # mains hum plus blower rumble. 48kHz mono to match UE's mixer rate exactly, so it plays with
    # no resample. Looping, attenuated, centred in the hall.
    try:
        ta = unreal.AssetImportTask()
        ta.set_editor_property("filename", GEN_DIR + "/S_RoomTone.wav")
        ta.set_editor_property("destination_path", "/Game/Diorama/Audio")
        ta.set_editor_property("automated", True); ta.set_editor_property("save", True)
        ta.set_editor_property("replace_existing", True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([ta])
        tone = unreal.load_asset("/Game/Diorama/Audio/S_RoomTone")
        if tone:
            tone.set_editor_property("looping", True)
            unreal.EditorAssetLibrary.save_loaded_asset(tone)
            amb = spawn(unreal.AmbientSound, (700, 20, 150))
            amb.get_editor_property("audio_component").set_sound(tone)
            amb.set_actor_label("RoomTone")
            w("room tone placed (48kHz, looping)")
        else:
            w("room tone: no SoundWave imported from %s (hall left silent)" % GEN_DIR)
    except Exception as e:
        w("room tone skipped: %s" % e)

    # ---------- camera / player ----------
    # The view comes from the DefaultPawn spawned at this PlayerStart — NOT from a
    # CameraActor. An auto-activating CameraActor welds the view target to itself, so the
    # pawn still receives input and still moves while the picture never changes: input looks
    # dead. (The framing problem that CameraActor was added to solve was really the
    # Rotator(roll,pitch,yaw) bug above, now fixed — PlayerStart aims correctly on its own.)
    # PlayerStart rotation seeds the initial control rotation, so Pixel Streaming mouse/WASD
    # fly the view from exactly this pose.
    # The view comes from an auto-activating StageCamera, which is the only thing that has
    # reliably framed this set. Driving the view from the GameModeBase DefaultPawn does NOT
    # work here: the map's PlayerStart is provably correct (probed at (-250,-60,175) pitch
    # -5.9) yet the pawn's view lands elsewhere and renders floor tiles — and re-authoring the
    # whole set around the world origin did not change it either. Unresolved; tracked as a
    # known issue. Framing the character beats free-look for a fixed-camera diorama, so the
    # camera wins until the pawn spawn is understood.
    # TRADE-OFF: an auto-activated CameraActor becomes the view target, so player input moves
    # the pawn invisibly — free-look will stay dead until this is solved properly.
    # PORTRAIT FRAMING — this is a video call, not a wide establishing shot.
    #
    # The brief is a FaceTime/Zoom call into an early-70s mainframe room, so the camera sits at
    # the resident's eye level, close, on a long-ish lens, in a 9:16 frame (-ResX=720 -ResY=1280
    # in docker-compose.amd.yml — the two must be changed together or the framing is meaningless).
    #
    # Geometry, so this can be re-derived rather than re-guessed. UE's field_of_view is
    # HORIZONTAL and the aspect constraint keeps X FOV, so in portrait the vertical angle is the
    # WIDER one: vFOV = 2*atan(tan(hFOV/2) / aspect), aspect = 720/1280 = 0.5625.
    #   hFOV 18deg -> vFOV ~31.4deg -> at 2.0m that frames ~1.12m of her, head to upper chest.
    # Centring on z=150 puts her eyeline in the upper third, which is where a face belongs in a
    # portrait crop. Camera and subject share y, so she is dead-centre horizontally and the aisle
    # recedes symmetrically past both shoulders.
    #
    # She stands at (330, 20, 0) facing -X (actor yaw 90, see NIA_YAW), so the camera must be on
    # the -X side of her to see a face rather than the back of her head.
    # Eyeline comes from the measured mesh (NIA_EYE_Z above); the literal is only a fallback for
    # the Seo path, where no measurement was taken.
    EYE_Z = float(globals().get("NIA_EYE_Z", 165.0))
    # Wider and further back than the first attempt (18deg at 2.0m), which produced a beauty
    # close-up: correct framing of a face, but it showed none of the room, and a call from a
    # mainframe centre that could be a call from anywhere has lost the point of the set.
    # 24deg at 2.4m in 9:16 covers ~1.8m vertically -- head to hips, with the aisle visible past
    # both shoulders. Aiming BELOW the eyeline puts her eyes in the upper third where a face
    # belongs, instead of dead-centre.
    # Distance is SOLVED for a target framing, not dialled in by eye. Vertical coverage is
    #   2 * d * tan(vFOV/2),  vFOV = 2*atan(tan(hFOV/2) / (9/16))
    # so for head-and-chest (~1.05m of subject) at hFOV 21deg:  d = 1.05 / (2*tan(18.24deg)) ~= 1.6m.
    # At the previous 2.2m the same lens covered 1.45m -- head to knees, a body shot rather than a
    # call. Framing is centred below the eyeline so the eyes sit ~70% up the frame, where a video
    # call puts them, instead of dead centre.
    CAM_DIST = 160.0
    CAM_POS = (330.0 - CAM_DIST, 20.0, EYE_Z - 8.0)
    # Aim raised 10cm from EYE_Z-22. At -22 the frame ran 105-211cm: eyes at 71% with almost no
    # headroom and a lot of chest below, which for a line like "Welcome to Companion Intelligence"
    # reads as a glamour crop rather than a corporate one. At -12 it runs 115-221cm, eyes ~62%,
    # ~21cm of headroom -- the proportions of a video call.
    LOOK_AT = (330.0, 20.0, EYE_Z - 12.0)
    dx, dy, dz = (LOOK_AT[0]-CAM_POS[0], LOOK_AT[1]-CAM_POS[1], LOOK_AT[2]-CAM_POS[2])
    cam_yaw = math.degrees(math.atan2(dy, dx))
    cam_pitch = math.degrees(math.atan2(dz, math.sqrt(dx*dx + dy*dy)))
    cam = spawn(unreal.CameraActor, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    cam.set_actor_label("StageCamera")
    cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    try:
        cam.camera_component.set_editor_property("field_of_view", 21.0)
        # CRITICAL for portrait: a CameraComponent defaults to CONSTRAINING its aspect ratio, so
        # a 16:9 camera rendered into a 9:16 viewport is letterboxed — the first attempt produced
        # a wide strip of image with black bars filling most of a 720x1280 frame. Releasing the
        # constraint lets the render fill the portrait viewport.
        cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
        cam.camera_component.set_editor_property("aspect_ratio", 720.0 / 1280.0)
    except Exception as e:
        w("fov warn " + str(e))
    spawn(unreal.PlayerStart, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    w("StageCamera ok (portrait 9:16, yaw=%.1f pitch=%.1f fov=21) — known issue: free-look input inert" %
      (cam_yaw, cam_pitch))

    ok = les.save_current_level()
    w("save_current_level=%s" % ok)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    # SUCCESS has to MEAN a map was written. It previously printed unconditionally, so a run that
    # authored 60 actors into an unsaved level and wrote nothing to disk still reported SUCCESS --
    # and every downstream gate (m2_clean.sh's `grep -q SUCCESS`) believed it.
    if not ok:
        raise RuntimeError("save_current_level returned False; no .umap was written")
    w("SUCCESS")
except Exception as e:
    w("FAIL " + str(e))
    w(traceback.format_exc())
