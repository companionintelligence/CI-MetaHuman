# Headless UE authoring — M2: the 1970s research-datacenter diorama.
# Imports the 12 prop FBXs (textures alongside), builds CompanionStage v2 with the same
# layout as the web DatacenterDiorama (web meters -> UE cm: x_ue=-100*z_w, y_ue=100*x_w,
# z_ue=100*y_w; yaw_ue=-deg(rotY_w)), era lighting per the GDD ("diegetic warm, chrome
# cool"), desks under the desk-height artifacts, blockout resident, PlayerStart framing.
# Writes /out/AUTHOR_RESULT.txt with progress + SUCCESS/FAIL.
import unreal, traceback, math

RESULT = "/out/AUTHOR_RESULT.txt"
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
PROPS = [
    # name,        fbx,             pos (x,y,z cm),      yaw, target_cm
    ("ramac305",   "ramac305.fbx",  ( 460, -360, 0),  -20, 170),
    ("ibm1401",    "ibm1401.fbx",   ( 490, -160, 0),   -7, 170),
    ("ibm1401cpu", "ibm1401cpu.fbx",( 490,   30, 0),    0, 170),
    ("ibm3350",    "ibm3350.fbx",   ( 490,  210, 0),    7, 150),
    ("ibm2401",    "ibm2401.fbx",   ( 460,  370, 0),   20, 183),
    ("kenbak1",    "kenbak1.fbx",   ( 110, -330, 74), -29,  48),
    ("mouse63",    "mouse63.fbx",   (  85, -285, 74), -11,  15),
    ("plugboard",  "plugboard.fbx", ( 155, -355, 74), -34,  90),
    ("xeroxalto",  "xeroxalto.fbx", ( 260,  270, 0),   40, 120),
    ("pet",        "pet.fbx",       ( 170,  335, 0),   49,  50),
    ("ibm3178",    "ibm3178.fbx",   (  60,  370, 0),   60,  50),
    ("cp500",      "cp500.fbx",     ( -50,  355, 0),   69,  55),
]

try:
    open(RESULT, "w").close()
    w("start")
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # ---------- import all 12 props ----------
    def import_prop(name, fbx):
        ui = unreal.FbxImportUI()
        ui.import_mesh = True
        ui.import_as_skeletal = False
        ui.import_animations = False
        ui.import_materials = True
        ui.import_textures = True
        ui.static_mesh_import_data.combine_meshes = True
        ui.static_mesh_import_data.generate_lightmap_u_vs = False
        ui.static_mesh_import_data.auto_generate_collision = False
        task = unreal.AssetImportTask()
        task.filename = "/props/" + fbx
        task.destination_path = "/Game/Diorama/" + name
        task.automated = True
        task.save = True
        task.replace_existing = True
        task.options = ui
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        # find the imported StaticMesh
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        assets = ar.get_assets_by_path("/Game/Diorama/" + name, recursive=True)
        for a in assets:
            if str(a.asset_class_path.asset_name) == "StaticMesh":
                return unreal.load_asset(str(a.package_name))
        return None

    imported = {}
    for name, fbx, pos, yaw, target_cm in PROPS:
        sm = import_prop(name, fbx)
        imported[name] = sm
        w("import %s -> %s" % (name, "OK" if sm else "NO_STATICMESH"))

    # ---------- fresh level ----------
    les.new_level("/Game/Maps/CompanionStage")
    w("new_level ok")

    # NOTE: unreal.Rotator's Python constructor is (roll, pitch, yaw) — NOT pitch-first like
    # C++ FRotator. Passing positionally silently set pitch where yaw was meant, which tilted
    # every prop onto its face and rolled the camera horizon. Always pass by keyword.
    def spawn(cls, loc=(0,0,0), yaw=0.0, pitch=0.0, roll=0.0):
        return eas.spawn_actor_from_class(
            cls, unreal.Vector(*loc), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))

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
    point(( 50,  260, 220), (0.91, 0.64, 0.29), 20000)   # warm tungsten over terminal row
    point(( 450, -300, 300), (0.0, 0.66, 0.65), 15000)   # CI aqua rim on big iron (left)
    point(( 450,  300, 300), (0.35, 0.92, 0.75), 10000)  # mint accent (right)
    point((-150, 0, 250), (1.0, 0.85, 0.70), 45000)      # camera-side warm fill (reads the resident)
    point(( 250, -150, 260), (1.0, 0.88, 0.72), 12000)   # mid-room fill (no GI to carry it)
    point(( 100, -320, 160), (0.95, 0.70, 0.35), 8000, 900.0)  # desk-lamp practical
    sky = spawn(unreal.SkyLight, (0,0,300))
    skyc = sky.get_component_by_class(unreal.SkyLightComponent)
    skyc.set_editor_property("mobility", MOVABLE)
    # A captured SkyLight contributes nothing with sky-capture disabled; drive it from a
    # constant color so it acts as a cheap ambient floor instead of dead weight.
    skyc.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    skyc.set_editor_property("lower_hemisphere_is_black", False)
    skyc.set_intensity(1.0)
    # Auto exposure (histogram) with tight bounds: self-corrects if the direct-light budget
    # drifts, so a lighting tweak can never ship a black stream again. Bounds keep it stable
    # for a fixed camera (no visible ramping).
    ppv = spawn(unreal.PostProcessVolume, (0,0,0))
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_method", True)
    s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    s.set_editor_property("override_auto_exposure_min_brightness", True)
    s.set_editor_property("auto_exposure_min_brightness", 0.25)
    s.set_editor_property("override_auto_exposure_max_brightness", True)
    s.set_editor_property("auto_exposure_max_brightness", 0.55)
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", 1.0)
    ppv.set_editor_property("settings", s)
    w("lights ok (no-GI budget: dir=8lux, 6 points, auto-exposure 0.25-0.55)")

    # ---------- room shell ----------
    plane = unreal.load_asset('/Engine/BasicShapes/Plane')
    cube  = unreal.load_asset('/Engine/BasicShapes/Cube')
    floor = spawn(unreal.StaticMeshActor, (100,0,0))
    floor.static_mesh_component.set_static_mesh(plane)
    floor.set_actor_scale3d(unreal.Vector(16,16,1))
    back = spawn(unreal.StaticMeshActor, (600,0,250))     # back wall
    back.static_mesh_component.set_static_mesh(cube)
    back.set_actor_scale3d(unreal.Vector(0.2,16,5))
    left = spawn(unreal.StaticMeshActor, (100,-500,250))  # left wall
    left.static_mesh_component.set_static_mesh(cube)
    left.set_actor_scale3d(unreal.Vector(14,0.2,5))
    w("shell ok")

    # ---------- desk under the small artifacts ----------
    desk = spawn(unreal.StaticMeshActor, (115,-320,66))
    desk.static_mesh_component.set_static_mesh(cube)
    desk.set_actor_scale3d(unreal.Vector(1.6,1.4,0.12))   # 160x140cm top at ~72cm
    w("desk ok")

    # ---------- the 12 props: measure -> normalize scale -> ground -> place ----------
    placed = 0
    for name, fbx, pos, yaw, target_cm in PROPS:
        sm = imported.get(name)
        if not sm:
            w("SKIP place %s (no mesh)" % name); continue
        a = spawn(unreal.StaticMeshActor, pos, yaw=yaw)
        a.static_mesh_component.set_static_mesh(sm)
        a.set_actor_label("prop_" + name)

        # normalize from MEASURED bounds so the source file's unit scale can't matter
        box = sm.get_bounding_box()
        size = box.max - box.min
        max_dim = max(size.x, size.y, size.z)
        s = (target_cm / max_dim) if max_dim > 0.001 else 1.0
        a.set_actor_scale3d(unreal.Vector(s, s, s))
        # sit the mesh's base exactly on its placement height (no float/sink)
        a.set_actor_location(unreal.Vector(pos[0], pos[1], pos[2] - box.min.z * s), False, False)
        w("place %-11s raw_max=%7.1f -> x%.4f -> %.0fcm" % (name, max_dim, s, max_dim * s))
        placed += 1
    w("placed %d/12 props" % placed)

    # ---------- blockout resident (until the MetaHuman lands) ----------
    cyl = unreal.load_asset('/Engine/BasicShapes/Cylinder')
    sph = unreal.load_asset('/Engine/BasicShapes/Sphere')
    body = spawn(unreal.StaticMeshActor, (60, -40, 90))
    body.static_mesh_component.set_static_mesh(cyl)
    body.set_actor_scale3d(unreal.Vector(0.85, 0.5, 1.8))
    head = spawn(unreal.StaticMeshActor, (60, -40, 205))
    head.static_mesh_component.set_static_mesh(sph)
    head.set_actor_scale3d(unreal.Vector(0.55, 0.55, 0.6))
    w("resident blockout ok")

    # ---------- camera ----------
    spawn(unreal.PlayerStart, (-320, 0, 165), pitch=-6.0)
    w("playerstart ok")

    ok = les.save_current_level()
    w("save_current_level=%s" % ok)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    w("SUCCESS")
except Exception as e:
    w("FAIL " + str(e))
    w(traceback.format_exc())
