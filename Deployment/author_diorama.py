# Headless UE authoring — M2: the 1970s research-datacenter diorama.
# Imports the 12 prop FBXs (textures alongside), builds CompanionStage v2 with the same
# layout as the web DatacenterDiorama (web meters -> UE cm: x_ue=-100*z_w, y_ue=100*x_w,
# z_ue=100*y_w; yaw_ue=-deg(rotY_w)), era lighting per the GDD ("diegetic warm, chrome
# cool"), desks under the desk-height artifacts, blockout resident, PlayerStart framing.
# Writes /out/AUTHOR_RESULT.txt with progress + SUCCESS/FAIL, and the same lines to
# Content/AUTHOR_RESULT.txt beside the map (see THE RESULT TRAVELS WITH THE CONTENT below).
import unreal, traceback, math, os, hashlib

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
MH_DIR    = _dir("MHLLM_MH_DIR",    "/mh")       # MetaHuman "Seo" FBX export (opt-in: MHLLM_RESIDENT=metahuman)
AUDIO_DIR = _dir("MHLLM_AUDIO_DIR", "/audio")    # Deployment/audio — the machine-room score
BRAND_DIR = _dir("MHLLM_BRAND_DIR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "generated"))
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

# WHICH RESIDENT. Four choices. The default and "eliza" are the ones whose licences clear this use.
#   "mannequin" (the default) places Epic's UE mannequin, SKM_Manny from the engine's MoverTests
#       plugin, playing its MM_Idle loop. Content installed with Unreal Engine is Licensed
#       Technology under the Unreal Engine EULA (section 1), and the current EULA carries no AI
#       restriction. It is a PLACEHOLDER: it has no facial morph targets, so the face driver binds
#       and resolves 0 curves (no lip-sync, no expression, no eye movement).
#   "eliza" places Eliza, the resident Companion Intelligence made for this product, from the content
#       staged under Content/Eliza (never committed; BuildNative.ps1 -Resident Eliza or the core-2 run
#       copies it in). Content/Eliza/eliza_manifest.json says which variant is staged: "mpfb" (MPFB 2,
#       CC0 1.0 assets, ARKit morph targets) or "metahuman" (MetaHuman Creator, Unreal Engine EULA,
#       driven over Live Link). MHLLM_ELIZA_VARIANT, when set, must match it. NOTICE.md has the licences.
#   "metahuman" places the MetaHuman "Seo" export read from MH_DIR.
#   "nia" places the Reallusion Character Creator "Nia" content staged under
#       /Game/CC_ControlRig_Nia, exactly as every build did before this switch existed, including
#       the fall-back to Seo if she fails to load.
#
# Seo and Nia are OPT-IN because of their licences, not their looks. Both are Fab content under the
# Standard License with "Allows usage with AI: No" (the NoAI tag): Nia is "Character Creator: Nia"
# (Reallusion Inc), and mhc_seo.mhpkg is "Seo - Editable MetaHuman Character" (seller Groomchenko).
# This product is an AI companion. Epic's MetaHuman terms do not clear Seo: the Unreal Engine EULA
# (section 1) says assets downloaded from Fab "are not Licensed Technology", so the seller's Fab
# licence, NoAI tag included, is what governs her. README.md "Licensing" has the citations.
#
# Anything else is refused rather than guessed: authoring a different resident than the one asked
# for is the silent-wrong-artifact failure every gate in this pipeline exists to stop.
RESIDENT  = os.environ.get("MHLLM_RESIDENT", "").strip().lower() or "mannequin"
RESIDENTS = ("mannequin", "eliza", "metahuman", "nia")

# THE RESULT TRAVELS WITH THE CONTENT. Both Dockerfiles, and BuildNative.ps1 -SkipAuthoring, cook a
# map this run is not there to vouch for, and the bytes of a .umap do not say who stands in it or
# which props it placed: a map authored before the resident switch (Nia) or before the NoAI prop gate
# below (the three Sketchfab models) passes every size and stub check. So every line of the result
# also goes to Content/AUTHOR_RESULT.txt, beside the map, and the run ends it with "map sha256" (the
# hash of the map it saved) and SUCCESS. Whoever copies the authored Content/ into a build context
# copies its provenance with it, and the gates (CHECK_MAP in Deployment/Dockerfile and
# Dockerfile.amd, BuildNative.ps1) refuse an authored map whose result is missing, failed, placed a
# different resident, predates the NoAI gate, or names another map. A run that fails leaves a copy
# that says FAIL, never the previous run's SUCCESS.
def _content_dir():
    # Ask the ENGINE where Content/ is, not this file's location: on core-2, run_author_m2.sh runs a
    # COPY of this script from /proj/, two directories above nothing (see the map check at the end).
    try:
        return unreal.SystemLibrary.get_project_content_directory()
    except Exception:
        return os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Content")
CONTENT_DIR = _content_dir()
PROVENANCE  = os.path.join(CONTENT_DIR, "AUTHOR_RESULT.txt")
RESULT_FILES = [RESULT]
if os.path.normcase(os.path.abspath(PROVENANCE)) != os.path.normcase(os.path.abspath(RESULT)):
    RESULT_FILES.append(PROVENANCE)

def w(s):
    for path in RESULT_FILES:
        try:
            with open(path, "a") as f: f.write(str(s) + "\n")
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
    ("kenbak1",    "kenbak-1.glb",          ( 110, -330, 74), -29,  48),
    ("mouse63",    "engelbart-mouse.glb",   (  85, -285, 74), -11,  15),
    ("plugboard",  "plugboard.glb",         ( 155, -355, 74), -34,  90),
    ("xeroxalto",  "xerox-alto.glb",        ( 260,  270, 0),   40, 120),
    ("pet",        "commodore-pet.glb",     ( 170,  335, 0),   49,  50),
    ("ibm3178",    "ibm-3178-terminal.glb", (  60,  370, 0),   60,  50),
    ("cp500",      "prologica-cp500.glb",   ( -50,  355, 0),   69,  55),
]

# SLOTS WHOSE SKETCHFAB MODEL IS NoAI-TAGGED. These three files were maxdragon's CC BY 4.0 models,
# and all three Sketchfab pages carry the NoAI tag. Sketchfab's Terms of Use bar NoAI content from
# use "in datasets for, in the development of, or as inputs to generative AI programs" (Sketchfab
# Help Center article 16152133), and this product is an AI companion, so that is a licence issue.
# A file in one of these slots is imported only when its glTF asset block names Companion
# Intelligence as the copyright holder, which is what Deployment/blender/make_period_props.py
# writes into the replacements it builds under the same names. Anything else, such as the old
# Sketchfab download or a file of unknown origin, is REFUSED and the slot stays empty, so a stale
# prop directory cannot put those models back into a build.
CI_AUTHORED_ONLY = ("ibm-1401-cpu.glb", "ibm-3178-terminal.glb", "ibm-3350-disk.glb")
CI_COPYRIGHT_MARK = "Companion Intelligence"

def glb_copyright(path):
    """asset.copyright of a binary glTF file, "" when it has none, None when it cannot be read."""
    import json, struct
    try:
        with open(path, "rb") as f:
            head = f.read(20)
            if len(head) < 20:
                return None
            magic, _version, _length, chunk_len, chunk_type = struct.unpack("<4sIII4s", head)
            if magic != b"glTF" or chunk_type != b"JSON":
                return None
            doc = json.loads(f.read(chunk_len).decode("utf-8"))
        return str((doc.get("asset") or {}).get("copyright") or "")
    except Exception:
        return None

try:
    # Truncate both, so no line of an earlier run survives into this one. If the copy beside the map
    # cannot be written, fail here: an authored map without it is refused by every gate downstream.
    for path in RESULT_FILES:
        open(path, "w").close()
    w("start")
    if RESIDENT not in RESIDENTS:
        raise RuntimeError("MHLLM_RESIDENT=%r is not one of %s" % (RESIDENT, "/".join(RESIDENTS)))
    w("resident requested: %s (MHLLM_RESIDENT)" % RESIDENT)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # ---------- import all 11 props (GLB via Interchange; materials+textures come with) ----------
    def import_prop(name, glb, mesh_name=None):
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
            asset_name = str(a.asset_name)
            if k == "StaticMesh" and mesh is None and (mesh_name is None or asset_name == mesh_name):
                mesh = unreal.load_asset(str(a.package_name))
            elif k == "Texture2D":
                tex += 1
        return mesh, tex

    imported = {}
    refused = []
    for name, glb, pos, yaw, target_cm in PROPS:
        if glb in CI_AUTHORED_ONLY and os.path.isfile(GLB_DIR + "/" + glb):
            mark = glb_copyright(GLB_DIR + "/" + glb)
            if not mark or CI_COPYRIGHT_MARK not in mark:
                # Not imported at all, so nothing of it reaches /Game, the map or the cook.
                # place_prop() then logs "SKIP place" for every slot that would have used it.
                imported[name] = None
                refused.append(name)
                w("prop REFUSED %-10s %s is not CI-authored (asset.copyright=%r); the Sketchfab model "
                  "that filled this slot is NoAI-tagged. Build the replacement with "
                  "Deployment/blender/make_period_props.py" % (name, glb, mark))
                continue
        sm, tex = import_prop(name, glb)
        imported[name] = sm
        w("import %-11s -> %s (%d tex)" % (name, "OK" if sm else "NO_STATICMESH", tex))
    w("NoAI prop slots: %d of %d refused%s" % (len(refused), len(CI_AUTHORED_ONLY),
      (" (" + ", ".join(refused) + " left empty)") if refused else ""))

    # ---------- fresh level ----------
    # THE empty-stage root cause. new_level REFUSES to overwrite ("Failed to validate the
    # destination. An asset already exists at this location") and returns False, and everything
    # after it then authors into the UNTITLED transient level -- which save_current_level cannot
    # write ("Can't save the level because it doesn't have a filename"). The authoring project
    # must start without this package (the content-only/docker path removes it before Unreal
    # launches). EditorAssetLibrary.delete_asset reports success while the package remains on disk
    # when that map is the editor's current level, so fail before touching it instead of claiming
    # that it was removed.
    MAP_PATH = "/Game/Maps/CompanionStage"
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        raise RuntimeError("%s already exists; remove it before starting the authoring editor" % MAP_PATH)
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
    # Current CI-Common palette: phthalo #0a6358, sage #c5e8dc, navy #041620, mint #3ae7ba.
    # The retro set keeps its IBM orange as the historical room finish and uses CI colors for
    # the resident, machine indicators, signage, and cool accents.
    def srgb(h):
        h = h.lstrip("#")
        f = [int(h[i:i+2], 16) / 255.0 for i in (0, 2, 4)]
        # approximate sRGB -> linear so UE does not wash the brand colours out
        return tuple((c / 12.92) if c <= 0.04045 else (((c + 0.055) / 1.055) ** 2.4) for c in f)
    CI_TEAL   = srgb("#0a6358")
    CI_TEAL_L = srgb("#c5e8dc")
    CI_NAVY   = srgb("#041620")
    CI_MINT   = srgb("#3ae7ba")

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
    # Back at 0.90 after trying 0.35. The reasoning for 0.35 was sound -- at 0.90 this slab is lit
    # from below by the troffer grid and clips to white, outshining the very fittings the design
    # note below wants reading as "light SOURCES, not a pale surface" -- but the fix does not work,
    # because the problem is not the albedo. This ceiling is ONE PAINTED SLAB with emissive
    # rectangles stuck to its underside. Darkening it swapped a blown white band for a flat grey
    # void with two pale trapezoids floating in it, which reads as unfinished geometry: worse.
    # Neither value is right; recessed fittings need actual geometry (a plenum recess plus a tile
    # normal or texture), which is a modelling pass, not a number. Left bright until then, because
    # a too-bright ceiling at least reads AS a ceiling.
    paint(ceil, "M_SetCeiling", (0.90, 0.90, 0.88), 0.8)                  # see note: needs geometry
    w("shell ok (walls + ceiling)")

    # ---------- glass frontage / IBM storefront entrance ----------
    # The opening camera starts on a small exterior apron and passes through this curtain wall.
    # Keep a central double-width entry; the glass stays lightly tinted so the first frame reads
    # as a datacenter behind a storefront instead of a dark reflection.
    glass_path = "/Game/Diorama/SetMats/M_FrontGlass_Tint"
    glass_mat = (unreal.EditorAssetLibrary.load_asset(glass_path)
                 if unreal.EditorAssetLibrary.does_asset_exist(glass_path) else None)
    if not glass_mat:
        glass_mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_FrontGlass_Tint", "/Game/Diorama/SetMats", unreal.Material, unreal.MaterialFactoryNew())
        glass_mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        glass_col = unreal.MaterialEditingLibrary.create_material_expression(
            glass_mat, unreal.MaterialExpressionVectorParameter, -400, 0)
        glass_col.set_editor_property("parameter_name", "Tint")
        glass_col.set_editor_property("default_value", unreal.LinearColor(0.28, 0.075, 0.025, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(
            glass_col, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        glass_opacity = unreal.MaterialEditingLibrary.create_material_expression(
            glass_mat, unreal.MaterialExpressionConstant, -400, 180)
        glass_opacity.set_editor_property("r", 0.10)
        unreal.MaterialEditingLibrary.connect_material_property(
            glass_opacity, "", unreal.MaterialProperty.MP_OPACITY)
        glass_roughness = unreal.MaterialEditingLibrary.create_material_expression(
            glass_mat, unreal.MaterialExpressionConstant, -400, 340)
        glass_roughness.set_editor_property("r", 0.12)
        unreal.MaterialEditingLibrary.connect_material_property(
            glass_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.recompile_material(glass_mat)

    # Plane and cube dimensions use Unreal's 100cm base scale. The apron supports the exterior
    # establishing key; the entry opening is left clear in the centre of the front wall.
    forecourt = spawn(unreal.StaticMeshActor, (-1250, 0, -2))
    forecourt.static_mesh_component.set_static_mesh(plane)
    forecourt.set_actor_scale3d(unreal.Vector(5.0, 14.0, 1.0))
    paint(forecourt, "M_Forecourt", (0.09, 0.065, 0.05), 0.82)
    sill = spawn(unreal.StaticMeshActor, (-990, 0, 42))
    sill.static_mesh_component.set_static_mesh(cube)
    sill.set_actor_scale3d(unreal.Vector(0.18, 14.0, 0.84))
    paint(sill, "M_FrontSill_FlameRed", (0.42, 0.075, 0.032), 0.68)
    header = spawn(unreal.StaticMeshActor, (-990, 0, 478))
    header.static_mesh_component.set_static_mesh(cube)
    header.set_actor_scale3d(unreal.Vector(0.18, 14.0, 0.44))
    paint(header, "M_FrontHeader", (0.10, 0.035, 0.025), 0.78)
    for y in (-600, -400, -200, 200, 400, 600):
        pane = spawn(unreal.StaticMeshActor, (-990, y, 250))
        pane.static_mesh_component.set_static_mesh(cube)
        pane.set_actor_scale3d(unreal.Vector(0.035, 1.8, 4.2))
        pane.static_mesh_component.set_material(0, glass_mat)
    for y in range(-700, 701, 200):
        mullion = spawn(unreal.StaticMeshActor, (-982, y, 250))
        mullion.static_mesh_component.set_static_mesh(cube)
        mullion.set_actor_scale3d(unreal.Vector(0.10, 0.075, 4.35))
        paint(mullion, "M_FacadeMullion_%d" % (y + 700), (0.12, 0.055, 0.035), 0.42)
    w("glass storefront frontage placed (central entry clear)")

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
    console.set_actor_scale3d(unreal.Vector(0.70, 1.10, 0.08))  # 140x220cm
    paint(console, "M_ConsoleTop", (0.88, 0.87, 0.84), 0.30)   # white console laminate
    for i, dy in enumerate((-65, 65)):
        ped = spawn(unreal.StaticMeshActor, (250, 20 + dy, 33))
        ped.static_mesh_component.set_static_mesh(cube)
        ped.set_actor_scale3d(unreal.Vector(0.55, 0.24, 0.54))
        paint(ped, "M_ConsolePed_%d" % i, (0.30, 0.30, 0.31), 0.55)
    # Compact vintage operator seating beside the desk; these stay outside the camera aisle.
    for i, dy in enumerate((-145, 145)):
        seat = spawn(unreal.StaticMeshActor, (275, 20 + dy, 44), yaw=90 if dy < 0 else -90)
        seat.static_mesh_component.set_static_mesh(cube)
        seat.set_actor_scale3d(unreal.Vector(0.21, 0.21, 0.05))  # 42cm square seat
        paint(seat, "M_OperatorChair_Seat_%d" % i, (0.22, 0.045, 0.032), 0.38)
        back = spawn(unreal.StaticMeshActor, (235, 20 + dy, 78), yaw=90 if dy < 0 else -90)
        back.static_mesh_component.set_static_mesh(cube)
        back.set_actor_scale3d(unreal.Vector(0.06, 0.22, 0.38))  # 44cm wide, 76cm high
        paint(back, "M_OperatorChair_Back_%d" % i, (0.22, 0.045, 0.032), 0.38)
        for leg_i, (dx, ddy) in enumerate(((-18,-18),(-18,18),(18,-18),(18,18))):
            leg = spawn(unreal.StaticMeshActor, (275 + dx, 20 + dy + ddy, 21))
            leg.static_mesh_component.set_static_mesh(cube)
            leg.set_actor_scale3d(unreal.Vector(0.018, 0.018, 0.21))
            paint(leg, "M_OperatorChair_Leg_%d_%d" % (i, leg_i), (0.40, 0.42, 0.41), 0.3)
    w("operator console placed")

    # CI-Common wordmark, rendered from the approved transparent white banner on a navy plaque.
    # The image stays in Deployment/generated and is mounted at /brand for the container author.
    try:
        logo_file = BRAND_DIR + "/ci-logo-white-banner.png"
        logo_task = unreal.AssetImportTask()
        logo_task.filename = logo_file
        logo_task.destination_path = "/Game/Diorama/Brand"
        logo_task.automated = True
        logo_task.save = True
        logo_task.replace_existing = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([logo_task])
        logo_tex = unreal.load_asset("/Game/Diorama/Brand/ci-logo-white-banner")
        if not logo_tex:
            raise RuntimeError("CI-Common logo texture was not imported from " + logo_file)
        logo_mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_CI_Logo", "/Game/Diorama/Brand", unreal.Material, unreal.MaterialFactoryNew())
        logo_mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        logo_mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        logo_sample = unreal.MaterialEditingLibrary.create_material_expression(
            logo_mat, unreal.MaterialExpressionTextureSample, -400, 0)
        logo_sample.set_editor_property("texture", logo_tex)
        unreal.MaterialEditingLibrary.connect_material_property(
            logo_sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(
            logo_sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        unreal.MaterialEditingLibrary.recompile_material(logo_mat)
        plaque = spawn(unreal.StaticMeshActor, (1710, 0, 390))
        plaque.static_mesh_component.set_static_mesh(cube)
        plaque.set_actor_scale3d(unreal.Vector(0.16, 2.4, 0.82))
        paint(plaque, "M_CI_BrandPlaque", CI_NAVY, 0.5)
        logo = spawn(unreal.StaticMeshActor, (1692, 0, 390), pitch=-90.0)
        logo.static_mesh_component.set_static_mesh(plane)
        # A plane rotated upright uses local Y for horizontal width and local X for height.
        logo.set_actor_scale3d(unreal.Vector(0.67, 2.1, 1.0))
        logo.static_mesh_component.set_material(0, logo_mat)
        logo.set_actor_label("Companion Intelligence · CI-Common wordmark")
        w("CI-Common logo plaque placed")
    except Exception as e:
        w("CI logo warn: %s" % e)

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
    desk.set_actor_scale3d(unreal.Vector(0.8,0.7,0.12))   # 160x140cm top at ~72cm
    for i, (dx, dy) in enumerate(((-70,-60),(-70,60),(70,-60),(70,60))):
        leg = spawn(unreal.StaticMeshActor, (115 + dx, -320 + dy, 30))
        leg.static_mesh_component.set_static_mesh(cube)
        leg.set_actor_scale3d(unreal.Vector(0.045,0.045,0.60))
        paint(leg, "M_ArtifactDeskLeg_%d" % i, (0.28,0.30,0.31), 0.42)
    w("desk ok")

    # ---------- the 11 props: measure -> normalize scale -> ground -> place ----------
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
    # Against the list, not a literal: this said "/12" long after the twelfth slot (the IBM 2401) left
    # PROPS for the 729 below, so a complete run read "placed 11/12" -- a count that looks one short.
    w("placed %d/%d hero props" % (placed, len(PROPS)))

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
    # The live NVIDIA stream is 1920x1080. Keep the near banks within the camera's horizontal
    # coverage so their machine faces read behind the resident; the second rank adds depth.
    CAM_DIST = 430.0
    HALL = []
    for i, x in enumerate((420, 600, 780, 980, 1200, 1440)):
        HALL.append((("ibm2401", "ibm3350", "ibm1401cpu")[i % 3], x, -105,  90, (183, 150, 170)[i % 3]))
        HALL.append((("ibm1401", "ibm2401", "ibm3350")[i % 3],    x,  110, -90, (170, 183, 150)[i % 3]))
    for i, x in enumerate((520, 760, 1020, 1300)):
        # Second rank behind the first, giving depth past the near cabinets.
        HALL.append((("ibm2401", "ibm3350")[i % 2], x, -300,  90, (183, 150)[i % 2]))
        HALL.append((("ibm3350", "ibm2401")[i % 2], x,  310, -90, (150, 183)[i % 2]))
    # A far rank across the end of the hall, closing the vista.
    HALL.append(("ramac305", 1690, -120, 180, 170))
    HALL.append(("ibm1401",  1690,  160, 180, 170))

    # Small staffed terminals run along both side walls, filling the room beyond the mainframe
    # ranks without narrowing the center aisle. Alternating PET and IBM 3178 silhouettes keep the
    # perimeter legible as a working computer room rather than a solid cabinet wall.
    WALL_COMPUTERS = []
    for i, x in enumerate((120, 350, 580, 810, 1040, 1270, 1500)):
        WALL_COMPUTERS.append(("pet" if i % 2 == 0 else "ibm3178", x, -535, 90, 58))
        WALL_COMPUTERS.append(("ibm3178" if i % 2 == 0 else "pet", x, 535, -90, 58))

    hall = 0
    for name, hx, hy, hyaw, tgt in HALL + WALL_COMPUTERS:
        # The 729 tape units are the custom, animated replacement for these legacy IBM 2401
        # placeholders. Do not stack both assets at the same station: that made the reels look
        # detached from their cabinets and produced overlapping silhouettes in the streamed shot.
        if name == "ibm2401":
            continue
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
    # The 729 replaces the old IBM 2401 placeholder at each tape-machine station in HALL. This
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
        # This GLB imports three StaticMeshes. The registry's first entry is not guaranteed to
        # be the cabinet body (on the staged build it was Reel_Takeup), so select the body node
        # explicitly instead of treating the first mesh as the cabinet.
        t_body, _t_body_tex = import_prop("ibm729", "ibm-729-tape.glb", mesh_name="IBM729_Body")
        reel_meshes = []
        for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(
                "/Game/Diorama/ibm729", recursive=True):
            if str(a.asset_class_path.asset_name) == "StaticMesh" and "Reel" in str(a.asset_name):
                reel_meshes.append(unreal.load_asset(str(a.package_name)))
        w("729: body=%s reels=%d" % (t_body is not None, len(reel_meshes)))

        # The Blender model's open bay faces -Y. In the live Unreal capture, aiming that side at
        # the camera showed the solid back, so the imported mesh's visible bay is Unreal-local +Y.
        # Keep both reel hubs on that front face so the StaticMeshActor pivot stays on the hub for
        # TapeReelSpinner.
        reel_offsets = {
            "Reel_Supply": (-16.0, 25.0, 126.0),
            "Reel_Takeup": (17.0, 25.0, 126.0),
        }

        placed729 = 0
        placed_reels = 0
        for name, bx, bank_y, face, _tgt in HALL:
            if name != "ibm2401":
                continue
            # The legacy hall placeholder's yaw points along the aisle. After GLTF axis conversion
            # a 729's open bay is on Unreal-local +Y, so aim +Y at the live camera on each station.
            camera_x, camera_y = 330.0 - CAM_DIST, 20.0
            tape_yaw = math.degrees(math.atan2(bx - camera_x, camera_y - bank_y))
            if t_body:
                a = spawn(unreal.StaticMeshActor, (bx, bank_y, 0), yaw=tape_yaw)
                a.static_mesh_component.set_static_mesh(t_body)
                a.set_actor_label("IBM729_%s_%d" % ("L" if bank_y < 0 else "R", bx))
                placed729 += 1
            # Restore each node's hub offset relative to the corresponding 729 cabinet.
            for rm in reel_meshes:
                offset = reel_offsets.get(rm.get_name())
                if offset is None:
                    w("729 reel has no placement metadata: %s" % rm.get_name())
                    continue
                ox, oy, oz = offset
                angle = math.radians(tape_yaw)
                rx = ox * math.cos(angle) - oy * math.sin(angle)
                ry = ox * math.sin(angle) + oy * math.cos(angle)
                r = spawn(unreal.StaticMeshActor, (bx + rx, bank_y + ry, oz), yaw=tape_yaw)
                # Movable must be set before assigning the mesh; the subsystem rotates this actor
                # on every frame, and Static mobility would leave the reel frozen in the cook.
                r.static_mesh_component.set_editor_property("mobility", MOVABLE)
                r.static_mesh_component.set_static_mesh(rm)
                r.tags = ["TapeReel"]
                r.set_actor_label("Reel_%s_%d_%s" % ("L" if bank_y < 0 else "R", bx, rm.get_name()))
                placed_reels += 1
        w("729 tape units placed: %d bodies, %d reel actors; replaced IBM 2401 stations"
          % (placed729, placed_reels))
    except Exception as e:
        import traceback as _t
        w("729 PLACEMENT WARN: %s" % e)

    # ---------- the resident ----------
    # Default (MHLLM_RESIDENT=mannequin): Epic's UE mannequin, placed further down. Opt-in: the
    # MetaHuman "Seo" (metahuman) or the Reallusion "Nia" (nia); see WHICH RESIDENT at the top.
    #
    # Seo: converted from mhc_seo.mhpkg (5.6 EditableCharacter): assembled in a local 5.8 editor,
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

    # ---------- opt-in resident (MHLLM_RESIDENT=nia): Reallusion Character Creator "Nia" ----------
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
        # Not even LOADED unless asked for. A checkout that once built with -Resident Nia keeps her
        # content staged in Content/CC_ControlRig_Nia (it is gitignored, not deleted), and the old
        # unconditional load would have quietly made her the resident of every later build.
        nia = unreal.load_asset(NIA_SK) if RESIDENT == "nia" else None
        if nia:
            a = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=NIA_YAW)
            a.set_actor_label("Nia")
            # TAG, not label: actor labels are EDITOR-ONLY and do not survive the cook, so a
            # label-based lookup works in the editor and silently finds nothing in the shipped
            # build. UCompanionFaceSubsystem finds the resident by this tag at BeginPlay and
            # attaches the face driver — which is how behaviour reaches a map that is authored
            # in a content-only project and therefore cannot reference plugin C++ or Blueprints.
            # FaceProfile=CC declares her rig (Reallusion V_* morphs). It is what the C++ assumed for
            # every map before the tag existed, so this changes nothing for her; it exists so the
            # MetaHuman resident can declare ARKit instead of inheriting a CC default.
            a.set_editor_property("tags", ["CompanionFace", "FaceProfile=CC"])
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
        elif RESIDENT == "nia":
            w("Nia: %s not found — falling back to the MetaHuman Seo path" % NIA_SK)
        else:
            w("resident: Nia not requested (MHLLM_RESIDENT=%s) -- placing the %s resident" % (RESIDENT, RESIDENT))
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
    # Only a build that may place Seo needs her materials: the metahuman resident, and nia, whose
    # fall-back is Seo and whose runs have always built them. A mannequin build creates nothing
    # under /Game/Seo.
    if RESIDENT in ("metahuman", "nia"):
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
    if nia_placed or RESIDENT in ("mannequin", "eliza"):
        # Nia is the resident, or the default mannequin or Eliza is: skip the Seo import entirely (it
        # also saves ~2 min). Those builds never read MH_DIR, so Seo cannot reach their map or cook.
        seo_parts = []
    placed_seo = 0
    seo_actors = {}      # part name ("Body"/"Face"/"Outfit") -> placed actor
    for fbx, dest, slotmap in seo_parts:
        if not os.path.isfile(fbx):
            w("resident: optional MetaHuman export unavailable, skipping %s" % fbx)
            continue
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
        seo_actors[dest.split("/")[-1]] = a
        placed_seo += 1

    # ONE resident, not three loose actors. Seo arrives as three skeletal meshes (Face, Body, Outfit)
    # that only line up because they share a transform, and the runtime treats the TAGGED actor as
    # the whole character: UCompanionStageDirector turns it to meet the camera during the opening
    # shot and measures its bounds for the eyeline, and UCompanionFaceSubsystem drives the morph
    # targets of its skeletal mesh. So:
    #   - the FACE carries the tags, because it is the mesh with the facial morph targets (849);
    #   - Body and Outfit are ATTACHED to it, so turning the tagged actor turns the whole figure
    #     instead of spinning a head above a still body;
    #   - FaceProfile=ARKit declares the rig, so the C++ stops assuming Nia's CC names.
    # Untagged, the face driver used to reach Seo only through its "most morph targets" fallback
    # (with a warning), and the stage director found no resident at all.
    #
    # NOT VERIFIED against a live Seo export: whether the FBX-exported face carries ARKit-named
    # morph targets. The runtime logs "Face profile 'ARKit': ..." with the resolved count; if it
    # says 0, the mapping needs a MetaHuman-specific profile (Deployment/metahuman/09_probe_face_rig.py
    # lists a mesh's morph names).
    seo_face = seo_actors.get("Face")
    seo_attached = 0
    if seo_face:
        seo_face.set_editor_property("tags", ["CompanionFace", "FaceProfile=ARKit"])
        for part in ("Body", "Outfit"):
            child = seo_actors.get(part)
            if not child:
                continue
            try:
                child.attach_to_actor(seo_face, "", unreal.AttachmentRule.KEEP_WORLD,
                                      unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                      False)
                # Read the attachment BACK: a call that quietly does nothing would leave a body that
                # stays behind when the face turns, and nothing else here would say so.
                if child.get_attach_parent_actor() == seo_face:
                    seo_attached += 1
                else:
                    w("resident: Seo_%s did not attach to Seo_Face" % part)
            except Exception as e:
                w("resident: Seo_%s attach FAILED: %s" % (part, e))
    if not nia_placed and RESIDENT in ("metahuman", "nia"):
        # The gate line. BuildNative.ps1 and m2_clean.sh require "resident: METAHUMAN placed" for the
        # MetaHuman resident, and it is printed ONLY when every part imported, the face is tagged
        # and both other parts follow it -- "placed 0 of 3" must never read as success.
        if seo_parts and placed_seo == len(seo_parts) and seo_face and seo_attached == len(seo_parts) - 1:
            w("resident: METAHUMAN placed (Seo %d/%d parts, face tagged CompanionFace + FaceProfile=ARKit, "
              "body and outfit attached) at %s yaw %.0f" % (placed_seo, len(seo_parts), RES_POS, RES_YAW))
        else:
            w("resident: METAHUMAN INCOMPLETE (Seo %d/%d parts placed, face %s, %d of %d attached; export dir %s)"
              % (placed_seo, len(seo_parts), "tagged" if seo_face else "MISSING", seo_attached,
                 max(len(seo_parts) - 1, 0), MH_DIR))

    # ---------- opt-in resident (MHLLM_RESIDENT=eliza): Eliza ----------
    # Made by Companion Intelligence; see WHICH RESIDENT at the top. The STAGED content decides the
    # variant, through Content/Eliza/eliza_manifest.json, so a build cannot place one variant while
    # vouching for the other. Placed where every resident stands, at the yaw that faces the camera:
    # both variants carry their front on +Y, like SKM_Manny. The MPFB export was checked for that from
    # its bounds (feet and face forward of the origin on +Y); confirm it from a captured frame (find
    # her EYES) before changing it, as for every resident before her.
    eliza_placed = False
    if RESIDENT == "eliza":
        try:
            import json as _json
            emf = os.path.join(CONTENT_DIR, "Eliza", "eliza_manifest.json")
            if not os.path.isfile(emf):
                raise RuntimeError("Content/Eliza/eliza_manifest.json is missing: her content is staged, not "
                                   "committed (BuildNative.ps1 -Resident Eliza -ElizaContent <dir>)")
            with open(emf) as f:
                eman = _json.load(f)
            evar = str(eman.get("variant", "")).strip().lower()
            ewant = os.environ.get("MHLLM_ELIZA_VARIANT", "").strip().lower()
            if eman.get("id") != "eliza" or evar not in ("mpfb", "metahuman"):
                raise RuntimeError("eliza_manifest.json names id=%r variant=%r" % (eman.get("id"), evar))
            if ewant and ewant != evar:
                raise RuntimeError("MHLLM_ELIZA_VARIANT=%s but the staged manifest is variant %s" % (ewant, evar))
            eassets = eman.get("assets") or {}
            eface = eman.get("face") or {}
            w("Eliza: manifest variant=%s version=%s engine=%s stature=%scm"
              % (evar, eman.get("version"), eman.get("engine"), eman.get("stature_cm")))
            etags = ["CompanionFace", "FaceProfile=%s" % eface.get("profile", "ARKit"),
                     "FaceGain=%s" % eface.get("gain", 1.0), "FacePoseCap=%s" % eface.get("pose_cap", 1.0)]
            def _idle(comp, path):
                # Single-node idle, animation_data set as a WHOLE struct (mutating the returned copy
                # does not persist), then read back.
                anim = unreal.load_asset(path) if path else None
                if not anim:
                    return None
                comp.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                ad = comp.get_editor_property("animation_data")
                ad.set_editor_property("anim_to_play", anim)
                ad.set_editor_property("saved_looping", True)
                ad.set_editor_property("saved_playing", True)
                comp.set_editor_property("animation_data", ad)
                back = comp.get_editor_property("animation_data").get_editor_property("anim_to_play")
                return back.get_name() if back else None
            def _name_it(actor):
                # The face driver logs the resident by OBJECT name (labels do not survive a cook).
                actor.set_actor_label("Eliza")
                try:
                    actor.rename("Eliza")
                except Exception:
                    pass
            if evar == "mpfb":
                emesh = unreal.load_asset(eassets.get("mesh") or "")
                if not emesh:
                    raise RuntimeError("mesh %s did not load" % eassets.get("mesh"))
                a = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=RES_YAW)
                _name_it(a)
                a.set_editor_property("tags", etags)
                comp = a.skeletal_mesh_component
                comp.set_skeletal_mesh(emesh)
                eidle = _idle(comp, eassets.get("idle"))
                nmorph = len(emesh.get_editor_property("morph_targets"))
                ELIZA_EYE_Z = float(eman.get("stature_cm") or 170.0) * 0.93
                w("Eliza: %s, %d morph targets, idle=%s, eyeline z=%.0fcm" % (emesh.get_name(), nmorph, eidle, ELIZA_EYE_Z))
                if eidle:
                    eliza_placed = True
                    w("resident: ELIZA placed (variant=mpfb, %s, tagged CompanionFace + FaceProfile=%s, idle=%s) at %s yaw %.0f"
                      % (emesh.get_name(), eface.get("profile", "ARKit"), eidle, RES_POS, RES_YAW))
                else:
                    w("resident: ELIZA INCOMPLETE (variant=mpfb, idle %s did not load)" % eassets.get("idle"))
            else:
                # MetaHuman variant: the assembled blueprint, its Body playing the retargeted idle, its
                # Face on ABP_MH_LiveLink (driven by the C++ Live Link sink), and the three CC0-derived
                # garments attached and leader-posed to the Body at BeginPlay (LeaderPose=Body tag).
                bp = unreal.load_asset(eassets.get("blueprint") or "")
                bpc = bp.generated_class() if bp else None
                if not bpc:
                    raise RuntimeError("blueprint %s did not load" % eassets.get("blueprint"))
                a = spawn(bpc, RES_POS, yaw=RES_YAW)
                _name_it(a)
                etags += ["FaceSink=%s" % eface.get("sink", "LiveLink"), "LiveLinkSubject=%s" % eface.get("subject", "Eliza")]
                a.set_editor_property("tags", etags)
                comps = {c.get_name(): c for c in a.get_components_by_class(unreal.SkeletalMeshComponent)}
                body, face = comps.get("Body"), comps.get("Face")
                eidle = _idle(body, eassets.get("idle")) if body else None
                fcls = unreal.load_class(None, "/MetaHumanCharacter/Animation/ABP_MH_LiveLink.ABP_MH_LiveLink_C")
                if face and fcls:
                    face.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
                    face.set_editor_property("anim_class", fcls)
                garments = list(eassets.get("garments") or [])
                attached = 0
                for gpath in garments:
                    gm = unreal.load_asset(gpath)
                    if not gm:
                        w("Eliza: garment %s did not load" % gpath)
                        continue
                    g = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=RES_YAW)
                    g.set_actor_label("Eliza_" + gm.get_name())
                    g.set_editor_property("tags", ["LeaderPose=Body"])
                    g.skeletal_mesh_component.set_skeletal_mesh(gm)
                    g.attach_to_actor(a, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                      unreal.AttachmentRule.KEEP_WORLD, False)
                    if g.get_attach_parent_actor() == a:
                        attached += 1
                ELIZA_EYE_Z = float(eman.get("stature_cm") or 170.0) * 0.93
                ok = bool(body and face and fcls and eidle) and garments and attached == len(garments)
                if ok:
                    eliza_placed = True
                    w("resident: ELIZA placed (variant=metahuman, %s, tagged CompanionFace + FaceProfile=%s, idle=%s, "
                      "garments %d/%d attached) at %s yaw %.0f"
                      % (bp.get_name(), eface.get("profile", "ARKitMH"), eidle, attached, len(garments), RES_POS, RES_YAW))
                else:
                    w("resident: ELIZA INCOMPLETE (variant=metahuman, body %s, face %s, face anim %s, idle %s, garments %d/%d attached)"
                      % (bool(body), bool(face), bool(fcls), eidle, attached, len(garments)))
        except Exception as e:
            w("resident: ELIZA INCOMPLETE (%s)" % e)

    # ---------- the default resident (MHLLM_RESIDENT=mannequin): Epic's UE mannequin ----------
    # SKM_Manny and its MM_Idle loop ship inside the engine's MoverTests plugin (enabled in the
    # .uproject; its Water dependency is why DefaultEngine.ini carries WaterBodyCollision). That makes
    # them Licensed Technology under the Unreal Engine EULA, which covers "related Epic-provided
    # content that you access when you download, use, or install Unreal Engine" (section 1), with no
    # AI restriction in the current EULA. It is also the resident the core-2 stream first went live
    # with, so the cook path is proven.
    #
    # It is a placeholder, and the log says so: no facial morph targets, so the face driver binds
    # and resolves 0 profile curves. The same code is the last resort when a requested Seo import
    # placed nothing, exactly as before; that fall-back still fails the requested resident's gate.
    MANNY_SK   = "/MoverTests/Characters/Mannequins/Meshes/SKM_Manny"
    MANNY_IDLE = "/MoverTests/Characters/Mannequins/Animations/Manny/MM_Idle"
    if RESIDENT == "mannequin" or (placed_seo == 0 and not nia_placed and RESIDENT != "eliza"):
        skm = unreal.load_asset(MANNY_SK)
        if skm:
            a = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=RES_YAW); a.set_actor_label("Resident")
            # Actor labels do not survive a cook. The tag is what the runtime camera director and the
            # face subsystem find the resident by, as for Nia. No FaceProfile= tag: there is no rig.
            a.set_editor_property("tags", ["CompanionFace"])
            comp = a.skeletal_mesh_component
            comp.set_skeletal_mesh(skm)
            if RESIDENT == "mannequin":
                try:
                    # Measured like Nia's, for the authored fallback camera below.
                    MANNY_EYE_Z = (skm.get_bounds().box_extent.z * 2.0) * 0.93
                except Exception:
                    pass
                # Same single-node idle Nia uses; set animation_data as a WHOLE struct, because
                # mutating the struct get_editor_property returns does not persist.
                idle = unreal.load_asset(MANNY_IDLE)
                if idle:
                    comp.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                    ad = comp.get_editor_property("animation_data")
                    ad.set_editor_property("anim_to_play", idle)
                    ad.set_editor_property("saved_looping", True)
                    ad.set_editor_property("saved_playing", True)
                    comp.set_editor_property("animation_data", ad)
                back = comp.get_editor_property("animation_data").get_editor_property("anim_to_play")
                # The gate line for the default resident (BuildNative.ps1, m2_clean.sh).
                w("resident: MANNEQUIN placed (SKM_Manny, engine MoverTests content, tagged CompanionFace, "
                  "idle=%s, no facial rig) at %s yaw %.0f"
                  % (back.get_name() if back else "NONE (bind pose)", RES_POS, RES_YAW))
            else:
                w("resident: FELL BACK to SKM_Manny (Seo import failed)")
        elif RESIDENT == "mannequin":
            w("resident: MANNEQUIN NOT placed (%s did not load; is the MoverTests plugin enabled?)" % MANNY_SK)

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
    # The runtime StageDirector owns the moving camera. This authored camera is only a fallback
    # and intentionally does not auto-activate, which previously overrode the tour during stream.
    # Keep its landscape framing consistent with the director's 430cm / 48-degree landing shot.
    #
    # She stands at (330, 20, 0) facing -X (actor yaw 90, see NIA_YAW), so the camera must be on
    # the -X side of her to see a face rather than the back of her head. Eyeline comes from the
    # measured mesh (NIA_EYE_Z, or MANNY_EYE_Z for the default mannequin); the literal is only a
    # fallback for the Seo path.
    EYE_Z = float(globals().get("ELIZA_EYE_Z", globals().get("NIA_EYE_Z", globals().get("MANNY_EYE_Z", 165.0))))
    CAM_POS = (330.0 - CAM_DIST, 20.0, EYE_Z - 8.0)
    # Camera sits just under eye level, with a slight lift to centre the face.
    LOOK_AT = (330.0, 20.0, EYE_Z - 6.5)
    dx, dy, dz = (LOOK_AT[0]-CAM_POS[0], LOOK_AT[1]-CAM_POS[1], LOOK_AT[2]-CAM_POS[2])
    cam_yaw = math.degrees(math.atan2(dy, dx))
    cam_pitch = math.degrees(math.atan2(dz, math.sqrt(dx*dx + dy*dy)))
    cam = spawn(unreal.CameraActor, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    cam.set_actor_label("StageCamera")
    # The runtime StageDirector owns the view target and animates its own camera along the tour.
    # Auto-activating this static editor camera overrode the director in Pixel Streaming, so the
    # flythrough ran in logs while viewers stayed on the unmoving authored camera.
    try:
        cam.camera_component.set_editor_property("field_of_view", 48.0)
        # Let the camera fill the configured 16:9 stream without letterboxing.
        cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
        cam.camera_component.set_editor_property("aspect_ratio", 1920.0 / 1080.0)
    except Exception as e:
        w("fov warn " + str(e))
    spawn(unreal.PlayerStart, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    # The FOV is read back off the component: this line used to print a literal "fov=65" beside the 48
    # set just above.
    w("StageCamera ok (landscape 16:9, yaw=%.1f pitch=%.1f fov=%.0f) — known issue: free-look input inert" %
      (cam_yaw, cam_pitch, cam.camera_component.get_editor_property("field_of_view")))

    ok = les.save_current_level()
    w("save_current_level=%s" % ok)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    # SUCCESS has to MEAN a map was written. It previously printed unconditionally, so a run that
    # authored 60 actors into an unsaved level and wrote nothing to disk still reported SUCCESS --
    # and every downstream gate (m2_clean.sh's `grep -q SUCCESS`) believed it.
    if not ok:
        raise RuntimeError("save_current_level returned False; no .umap was written")
    # CONTENT_DIR comes from the ENGINE rather than from this file's location. The old derivation
    # (two directories up from __file__) is right for Deployment/author_diorama.py in a checkout,
    # and wrong on core-2, where run_author_m2.sh copies the script to /proj/ and runs it from
    # there: it resolved to /Content/Maps/CompanionStage.umap, which never exists, so every
    # container run that reached this check would have reported FAIL after writing a good map.
    map_file = os.path.join(CONTENT_DIR, "Maps", "CompanionStage.umap")
    if not os.path.isfile(map_file) or os.path.getsize(map_file) < 200_000:
        size = os.path.getsize(map_file) if os.path.isfile(map_file) else 0
        raise RuntimeError("saved map artifact missing or incomplete (%d bytes): %s" % (size, map_file))
    w("map artifact verified: %d bytes" % os.path.getsize(map_file))
    # Binds this result to the map it describes (see THE RESULT TRAVELS WITH THE CONTENT, above).
    with open(map_file, "rb") as f:
        w("map sha256: %s" % hashlib.sha256(f.read()).hexdigest())
    expected729 = sum(1 for entry in HALL if entry[0] == "ibm2401")
    expected_reels = expected729 * 2
    if placed729 != expected729 or placed_reels != expected_reels:
        raise RuntimeError("tape asset placement incomplete: bodies %d/%d, reels %d/%d"
                           % (placed729, expected729, placed_reels, expected_reels))
    w("SUCCESS")
except Exception as e:
    w("FAIL " + str(e))
    w(traceback.format_exc())
