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
# Props are imported as GLB, not FBX. Blender's FBX exporter only writes textures wired to a
# Principled BSDF base color, so most of these Sketchfab materials exported with no maps at
# all and every machine rendered as a blank white block. The GLB export carries materials +
# textures intact (it is the same asset set the web build renders correctly), and UE 5.5's
# Interchange glTF importer brings Texture2D + MaterialInstanceConstant across with the mesh.
PROPS = [
    # name,        glb,                    pos (x,y,z cm),      yaw, target_cm
    ("ramac305",   "ibm-ramac-305.glb",     ( 460, -360, 0),  -20, 170),
    ("ibm1401",    "ibm-1401-mainframe.glb",( 490, -160, 0),   -7, 170),
    ("ibm1401cpu", "ibm-1401-cpu.glb",      ( 490,   30, 0),    0, 170),
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
        task.filename = "/glb/" + glb
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
    les.new_level("/Game/Maps/CompanionStage")
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
    for gx in (-100, 150, 400):
        for gy in (-320, 0, 320):
            point((gx, gy, 430), (1.0, 0.90, 0.78), 2500, 2600.0)
    # Accents carry the GDD's "diegetic warm, chrome cool": tungsten pools on the hardware,
    # CI aqua/mint rim off the machine faces, with real falloff between them.
    point(( 50,  260, 240), (0.95, 0.62, 0.26), 26000, 1200.0)  # warm tungsten, terminal row
    point(( 450, -300, 300), (0.0, 0.66, 0.65), 22000, 1500.0)  # CI aqua rim, big iron
    point(( 450,  300, 300), (0.35, 0.92, 0.75), 14000, 1300.0) # mint accent
    point((-150, 0, 250), (1.0, 0.85, 0.70), 9000, 1300.0)      # camera-side fill on the resident
    point(( 100, -320, 160), (0.95, 0.70, 0.35), 9000, 700.0)   # desk-lamp practical
    point(( 300, -60, 250), (1.0, 0.86, 0.70), 8000, 1100.0)    # soft key on the resident

    # Auto exposure with WIDE bounds. The camera that ships is fixed at the PlayerStart, so
    # adaptation settles once and stays put; narrow bounds only clamped adaptation below the
    # scene's real luminance and blew the image out. Wide bounds let it find the right stop
    # for whatever the light budget happens to be — a lighting tweak can't ship a black or
    # white stream again.
    ppv = spawn(unreal.PostProcessVolume, (0,0,0))
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_method", True)
    s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    s.set_editor_property("override_auto_exposure_min_brightness", True)
    s.set_editor_property("auto_exposure_min_brightness", 0.03)
    s.set_editor_property("override_auto_exposure_max_brightness", True)
    s.set_editor_property("auto_exposure_max_brightness", 8.0)
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", 0.0)
    ppv.set_editor_property("settings", s)
    w("lights ok (no-GI: dir=8lux, 9-light ceiling grid + 5 accents, auto-exposure 0.03-8.0)")

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
    right = spawn(unreal.StaticMeshActor, (100,500,250))  # right wall (closes the set)
    right.static_mesh_component.set_static_mesh(cube)
    right.set_actor_scale3d(unreal.Vector(14,0.2,5))
    # Ceiling: r.SkyAtmosphere is disabled on the stream, so anything above the walls renders
    # as dead black sky. A real ceiling both closes the room and bounces nothing (no GI) —
    # it is there to frame the shot, not to light it.
    ceil = spawn(unreal.StaticMeshActor, (100,0,500))
    ceil.static_mesh_component.set_static_mesh(cube)
    ceil.set_actor_scale3d(unreal.Vector(14,11,0.2))
    w("shell ok (walls + ceiling)")

    # ---------- desk under the small artifacts ----------
    desk = spawn(unreal.StaticMeshActor, (115,-320,66))
    desk.static_mesh_component.set_static_mesh(cube)
    desk.set_actor_scale3d(unreal.Vector(1.6,1.4,0.12))   # 160x140cm top at ~72cm
    w("desk ok")

    # ---------- the 12 props: measure -> normalize scale -> ground -> place ----------
    placed = 0
    for name, glb, pos, yaw, target_cm in PROPS:
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

    # ---------- the resident: MetaHuman "Seo" ----------
    # Converted from mhc_seo.mhpkg (5.6 EditableCharacter): assembled in a local 5.8 editor,
    # exported Face+Body+Outfit to FBX (version-agnostic), re-imported HERE into 5.5 as
    # skeletal meshes. Face carries 849 morph targets = the MetaHuman facial-blendshape rig
    # (the future viseme source). Placed at the camera's look target; default (gray) materials
    # for this first pass — skin textures + the DNA/RigLogic face are the next iteration.
    # RES_YAW=-90: a re-imported MetaHuman FBX has its visual front 90deg off the actor's
    # forward axis, so yaw 180 rendered her in profile. -90 turns her visual front to -X =
    # toward the StageCamera. (Verified by probing the mesh forward vector vs the framed view.)
    RES_POS, RES_YAW = (330, 20, 0), -90.0
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
            t.set_editor_property("filename", "/mh/tex/%s.tga" % name)
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

    seo_parts = [("/mh/fbx/SKM_MHC_Seo_BodyMesh.fbx", "/Game/Seo/Body", BODY_SLOTS),
                 ("/mh/fbx/SKM_MHC_Seo_FaceMesh.fbx", "/Game/Seo/Face", FACE_SLOTS)]
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
                nm.set_editor_property("material_slot_name", sm.get_editor_property("material_slot_name"))
                chosen = seo_mats.get(slotmap.get(i))
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
    w("resident: MetaHuman Seo placed (%d/2 parts, skinned) at %s yaw %.0f" % (placed_seo, RES_POS, RES_YAW))
    if placed_seo == 0:
        skm = unreal.load_asset('/MoverTests/Characters/Mannequins/Meshes/SKM_Manny')
        if skm:
            a = spawn(unreal.SkeletalMeshActor, RES_POS, yaw=RES_YAW); a.set_actor_label("Resident")
            a.skeletal_mesh_component.set_skeletal_mesh(skm)
            w("resident: FELL BACK to SKM_Manny (Seo import failed)")

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
    CAM_POS = (-250.0, -60.0, 175.0)
    LOOK_AT = (330.0, 20.0, 115.0)   # the resident, big-iron row behind them
    dx, dy, dz = (LOOK_AT[0]-CAM_POS[0], LOOK_AT[1]-CAM_POS[1], LOOK_AT[2]-CAM_POS[2])
    cam_yaw = math.degrees(math.atan2(dy, dx))
    cam_pitch = math.degrees(math.atan2(dz, math.sqrt(dx*dx + dy*dy)))
    cam = spawn(unreal.CameraActor, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    cam.set_actor_label("StageCamera")
    cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
    try:
        cam.camera_component.set_editor_property("field_of_view", 70.0)
    except Exception as e:
        w("fov warn " + str(e))
    spawn(unreal.PlayerStart, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    w("StageCamera ok (yaw=%.1f pitch=%.1f fov=70) — known issue: free-look input inert" %
      (cam_yaw, cam_pitch))

    ok = les.save_current_level()
    w("save_current_level=%s" % ok)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    w("SUCCESS")
except Exception as e:
    w("FAIL " + str(e))
    w(traceback.format_exc())
