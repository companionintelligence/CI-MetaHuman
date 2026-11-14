# capture_resident.py -- runs INSIDE the game, on the authored CompanionStage map (capture_resident.ps1 starts it):
#
#   UnrealEditor-Cmd <uproject> /Game/Maps/CompanionStage -game -RenderOffScreen -ResX=1920 -ResY=1080
#       -ForceRes -UseFixedTimeStep -FPS=30 ... -ExecCmds="py <this file>"
#
# with MHLLM_OPENING_SHOT=0 (the stage director pins its camera on the call framing) and no voice service.
# Every frame is a real game frame through the player's view (HighResShot of the game viewport).
#
#   HOME     the stage director's call framing, untouched (430 cm, FOV 48), her idle playing
#   BACK     the same framing with her turned 180 degrees, as the opening shot starts her
#   3/4      the same framing with her turned 40 degrees each way
#   CLOSE    a face framing: 231 cm in front of her eyes, horizontal FOV 25.4 (80 mm), level
#   strip    the runtime's own SpeakLiteral (what the opening line calls): FVisemePlanner text estimate
#            -> ARKit profile -> SetMorphTarget, every other frame at 30 fps game time, at CLOSE, with
#            quiet frames on both sides; morph weights logged per frame
#   viseme   the face self-test, one frame at the peak of each of the 15 visemes, at CLOSE
#   blink    idle at CLOSE with blink and gaze on: frames at two full blinks, and the gaze curves of
#            both eyes every frame (crossing = both eyes "In" at once, wall-eye = both "Out")
#
# CAPTURE_MODE=stills stops after CLOSE (HOME, BACK, 3/4, CLOSE only), for a faceless resident.
# CAPTURE_MODE=blink (run at -FPS=120) only films blinks at CLOSE: a blink lasts 120 ms and peaks for
# one instant, so at 30 fps no frame lands on a full close.
#
# Writes <out>/capture_manifest.json and quits. Game time is fixed-step, so a frame's time is exact.
import unreal, os, json, time, math, traceback

OUT = (os.environ.get("CAPTURE_DIR") or os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "ResidentCapture")).replace("\\", "/")
LINE = os.environ.get("CAPTURE_LINE", "Welcome to Companion Intelligence, I'm Eliza. How can I help you?")
RES = (1920, 1080)
os.makedirs(OUT + "/strip", exist_ok=True)
os.makedirs(OUT + "/viseme", exist_ok=True)
VISEMES = ["Sil", "PP", "FF", "TH", "DD", "KK", "CH", "SS", "NN", "RR", "AA", "E", "IH", "OH", "OU"]
MORPHS = ["jawOpen", "mouthClose", "mouthFunnel", "mouthPucker", "mouthPressLeft", "mouthPressRight",
          "mouthSmileLeft", "mouthStretchLeft", "mouthLowerDownLeft", "mouthShrugUpper", "tongueOut",
          "eyeBlinkLeft", "eyeBlinkRight", "eyeLookOutLeft", "eyeLookInLeft", "eyeLookOutRight", "eyeLookInRight",
          "eyeLookUpLeft", "eyeLookUpRight", "eyeLookDownLeft", "eyeLookDownRight"]
MODE = os.environ.get("CAPTURE_MODE", "full")

M = {"out": OUT, "line": LINE, "shots": [], "strip": [], "visemes": [], "log": [], "ok": False}
G = {"gen": None, "handle": None, "world": None, "frame": 0, "t0": time.time()}


def log(s):
    s = str(s)
    M["log"].append(s)
    unreal.log("RESCAP: " + s)


def world():
    w = G["world"]
    if w is None:
        try:
            w = unreal.find_object(None, "/Game/Maps/CompanionStage.CompanionStage")
        except Exception:
            w = None
        if w is None:
            for cand in unreal.ObjectIterator(unreal.World):
                if cand.get_name() == "CompanionStage" and unreal.GameplayStatics.get_player_controller(cand, 0):
                    w = cand
                    break
        G["world"] = w
    return w


def in_world(cls):
    # USubsystemBlueprintLibrary is not exposed to Python in -game, so world subsystems are found by
    # class and world. The class default object has no world and is skipped.
    w = world()
    for o in unreal.ObjectIterator(cls):
        try:
            ow = o.get_world()
        except Exception:
            ow = None
        if ow is not None and w is not None and ow.get_path_name() == w.get_path_name():
            return o
    return None


def gtime():
    return float(unreal.GameplayStatics.get_time_seconds(world()))


def cmd(c):
    unreal.SystemLibrary.execute_console_command(world(), c, None)


def save_manifest():
    with open(OUT + "/capture_manifest.json", "w") as f:
        json.dump(M, f, indent=1, default=str)


def morphs(mesh):
    return {n: round(float(mesh.get_morph_target(n)), 4) for n in MORPHS}


def request_shot(path):
    if os.path.exists(path):
        os.remove(path)
    cmd("HighResShot %dx%d filename=%s" % (RES[0], RES[1], path))


def wait_file(path, timeout=90.0):
    t = time.time()
    last = -1
    while time.time() - t < timeout:
        if os.path.exists(path):
            size = os.path.getsize(path)
            if size > 0 and size == last:
                return True
            last = size
        yield
    return False


def frames(n):
    for _ in range(n):
        yield


def shot(label, settle=15, extra=None):
    yield from frames(settle)          # TAA history and motion blur settle after a cut
    path = "%s/%s.png" % (OUT, label)
    rec = {"label": label, "file": path, "game_time": round(gtime(), 4), "frame": G["frame"]}
    if extra:
        rec.update(extra)
    request_shot(path)
    ok = yield from wait_file(path)
    rec["written"] = bool(ok)
    M["shots"].append(rec)
    log("shot %s written=%s" % (label, ok))
    save_manifest()


def look_at(cam, pos, aim, fov):
    cam.set_actor_location(pos, False, False)
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos, aim), False)
    cc = cam.get_editor_property("camera_component")
    cc.set_field_of_view(fov)


def main():
    # ---- wait for the map, the director's camera and the bound face ----
    t = time.time()
    while True:
        w = world()
        if w is not None:
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            res = unreal.GameplayStatics.get_all_actors_with_tag(w, "CompanionFace")
            fs = in_world(unreal.CompanionFaceSubsystem)
            face = fs.get_face() if fs else None
            vt = pc.get_view_target() if pc else None
            if pc and res and face and isinstance(vt, unreal.CameraActor):
                break
        if time.time() - t > 600:
            raise RuntimeError("no world / resident / face / camera after 600 s")
        yield
    resident = res[0]
    mesh = resident.get_component_by_class(unreal.SkeletalMeshComponent)
    runtime = in_world(unreal.CompanionRuntimeSubsystem)
    if runtime is None:
        raise RuntimeError("no CompanionRuntimeSubsystem in the game world")
    cam = vt
    M["resident"] = {"name": resident.get_name(), "tags": [str(x) for x in resident.tags],
                     "location": str(resident.get_actor_location()), "yaw": resident.get_actor_rotation().yaw,
                     "mesh": mesh.get_skeletal_mesh_asset().get_name() if hasattr(mesh, "get_skeletal_mesh_asset") else str(mesh.skeletal_mesh),
                     "anim_mode": str(mesh.get_editor_property("animation_mode"))}
    M["face"] = {"resolved": face.get_resolved_curve_count(), "profile_curves": face.get_profile_curve_count()}
    M["home_camera"] = {"location": str(cam.get_actor_location()), "rotation": str(cam.get_actor_rotation()),
                        "fov": cam.get_editor_property("camera_component").field_of_view}
    head = mesh.get_socket_location("head")
    neck = mesh.get_socket_location("neck_01")
    M["bones"] = {"head": str(head), "neck_01": str(neck),
                  "foot_l": str(mesh.get_socket_location("foot_l")), "foot_r": str(mesh.get_socket_location("foot_r"))}
    log("ready after %.1f s real: resident %s, face %d/%d curves, camera %s"
        % (time.time() - G["t0"], resident.get_name(), M["face"]["resolved"], M["face"]["profile_curves"],
           M["home_camera"]))
    # Settle: let the idle run and the scene converge (fixed 30 fps game time).
    yield from frames(90)

    if MODE == "blink":
        # Run at -FPS=120: a blink lasts 120 ms and peaks for one instant, so at 30 fps no frame lands
        # on a full close (the best sample is about 0.74). A shot is requested on every frame of a
        # blink, and the weight it rendered with (the NEXT frame's) is read on the following tick.
        head = mesh.get_socket_location("head")
        eye = unreal.Vector(head.x - 7.0, head.y, head.z + 6.5)
        look_at(cam, unreal.Vector(eye.x - 231.0, eye.y, eye.z), unreal.Vector(eye.x, eye.y, eye.z - 10.0), 25.36)
        face.set_gaze_enabled(False)
        yield from frames(60)
        t_b = gtime()
        shots, prev, k, full = [], None, 0, 0
        while gtime() - t_b < 40.0 and full < 2:
            mm = morphs(mesh)
            if prev is not None:
                prev["rendered_blink"] = [mm["eyeBlinkLeft"], mm["eyeBlinkRight"]]
                if min(mm["eyeBlinkLeft"], mm["eyeBlinkRight"]) >= 0.95:
                    full += 1
                prev = None
            if max(mm["eyeBlinkLeft"], mm["eyeBlinkRight"]) > 0.25:
                path = "%s/blink_%03d.png" % (OUT, k)
                k += 1
                request_shot(path)
                prev = {"file": path, "game_time": round(gtime(), 4), "requested_at_blink": [mm["eyeBlinkLeft"], mm["eyeBlinkRight"]]}
                shots.append(prev)
            yield
        for r in shots:
            yield from wait_file(r["file"], 60.0)
        M["blinks"] = shots
        log("blink: %d frames requested, %d rendered at a full close (both lids >= 0.95)" % (len(shots), full))
        M["ok"] = True
        save_manifest()
        return

    # ---- HOME, and the same framing with her turned ----
    yield from shot("home", extra={"morphs": morphs(mesh)})
    base = resident.get_actor_rotation()
    for label, dyaw in (("home_back", 180.0), ("home_turn_p40", 40.0), ("home_turn_m40", -40.0)):
        resident.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=base.yaw + dyaw), False)
        yield from shot(label, extra={"resident_yaw": base.yaw + dyaw})
    resident.set_actor_rotation(base, False)

    # ---- CLOSE: 231 cm in front of her eyes, 80 mm (25.4 deg horizontal), level, eyes in the upper third ----
    head = mesh.get_socket_location("head")
    # She faces -X (actor yaw 90 on a mesh whose front is +Y); the eyes sit about 7 cm in front of and
    # 6.5 cm above the head joint (Eliza: joint 152.2, eyes 158.3 = 0.93 x stature; Manny alike).
    eye = unreal.Vector(head.x - 7.0, head.y, head.z + 6.5)
    close_pos = unreal.Vector(eye.x - 231.0, eye.y, eye.z)
    close_aim = unreal.Vector(eye.x, eye.y, eye.z - 10.0)
    look_at(cam, close_pos, close_aim, 25.36)
    M["close_camera"] = {"location": str(close_pos), "aim": str(close_aim), "fov": 25.36, "eye": str(eye)}
    yield from shot("close", extra={"morphs": morphs(mesh)})
    if MODE == "stills":
        M["ok"] = True
        save_manifest()
        return

    # ---- lip-sync strip at CLOSE: the runtime's own SpeakLiteral, no voice service ----
    yield from frames(30)
    t_req = gtime()
    ok = runtime.speak_literal(LINE)
    log("speak_literal(%r) -> %s at game time %.3f" % (LINE, ok, t_req))
    M["speech"] = {"requested_at": round(t_req, 4), "speak_literal": bool(ok)}
    # Pre-roll: the speech window starts when the face reports speaking.
    pending = []
    started, ended = None, None
    k = 0
    pre = []
    # 1.5 s of quiet BEFORE speaking is captured in a separate pass below; here: speech + 2.0 s tail.
    while True:
        speaking = face.is_speaking()
        now = gtime()
        if speaking and started is None:
            started = now
            M["speech"]["started_at"] = round(now, 4)
        if started is not None and not speaking and ended is None:
            ended = now
            M["speech"]["ended_at"] = round(now, 4)
        if started is not None and (ended is None or now - ended < 2.0):
            if G["frame"] % 2 == 0:
                path = "%s/strip/f_%04d.png" % (OUT, k)
                rec = {"i": k, "file": path, "game_time": round(now, 4), "rendered_at": round(now + 1.0 / 30.0, 4),
                       "speaking": bool(speaking), "morphs": morphs(mesh)}
                request_shot(path)
                pending.append(path)
                M["strip"].append(rec)
                k += 1
        if ended is not None and now - ended >= 2.0:
            break
        if now - t_req > 30.0:
            log("speech did not end within 30 s of game time")
            break
        yield
    for p in pending:
        yield from wait_file(p, 60.0)
    log("strip: %d frames, speech %.3f -> %s" % (k, started or -1, ended))
    save_manifest()

    # Quiet baseline at the same framing (idle only, no speech), as many frames as the tail.
    for j in range(45):
        yield
        if G["frame"] % 2 == 0:
            path = "%s/strip/q_%04d.png" % (OUT, j)
            M["strip"].append({"i": j, "file": path, "game_time": round(gtime(), 4), "speaking": False,
                               "quiet_pass": True, "morphs": morphs(mesh)})
            request_shot(path)
            pending.append(path)
    for p in pending:
        yield from wait_file(p, 60.0)

    # ---- self-test: one frame at each viseme's peak (sin envelope peaks mid-slot) ----
    face.set_blink_enabled(False)
    face.set_gaze_enabled(False)
    yield from frames(10)
    face.start_self_test(0.4)
    t_st = gtime()
    M["selftest_started_at"] = round(t_st, 4)
    vpending = []
    for i, name in enumerate(VISEMES):
        # Requested without waiting for the file: game time keeps stepping while a PNG is written,
        # and a wait here would land the next request past its slot.
        target = t_st + 0.4 * i + 0.2 - 1.0 / 30.0     # the shot renders on the next frame
        while gtime() < target - 1e-4:
            yield
        path = "%s/viseme/v%02d_%s.png" % (OUT, i, name)
        rec = {"viseme": name, "file": path, "game_time": round(gtime(), 4),
               "selftest_elapsed_rendered": round(gtime() - t_st + 1.0 / 30.0, 4), "morphs": morphs(mesh)}
        request_shot(path)
        vpending.append(path)
        M["visemes"].append(rec)
        yield
    for p in vpending:
        yield from wait_file(p, 60.0)
    face.stop_self_test()
    face.set_blink_enabled(True)
    face.set_gaze_enabled(True)

    # ---- blink and gaze: idle at CLOSE, up to 25 s of game time or two full blinks ----
    yield from frames(15)
    t_b = gtime()
    blinks, last_blink, gz = [], -10.0, {"both_in_max": 0.0, "both_out_max": 0.0, "lr_max": 0.0, "frames": 0}
    bpending = []
    widest = None
    while gtime() - t_b < 25.0 and len(blinks) < 2:
        mm = morphs(mesh)
        now = gtime()
        gz["frames"] += 1
        gz["both_in_max"] = max(gz["both_in_max"], min(mm["eyeLookInLeft"], mm["eyeLookInRight"]))
        gz["both_out_max"] = max(gz["both_out_max"], min(mm["eyeLookOutLeft"], mm["eyeLookOutRight"]))
        # Looking to her left drives eyeLookOutLeft and eyeLookInRight together (and the reverse).
        lr = max(min(mm["eyeLookOutLeft"], mm["eyeLookInRight"]), min(mm["eyeLookInLeft"], mm["eyeLookOutRight"]))
        if lr > gz["lr_max"]:
            gz["lr_max"] = lr
            widest = (now, mm)
        if mm["eyeBlinkLeft"] >= 0.9 and mm["eyeBlinkRight"] >= 0.9 and now - last_blink > 0.6:
            last_blink = now
            path = "%s/blink_%d.png" % (OUT, len(blinks))
            request_shot(path)
            bpending.append(path)
            blinks.append({"file": path, "game_time": round(now, 4), "morphs": mm})
        yield
    for p in bpending:
        yield from wait_file(p, 60.0)
    M["blinks"] = blinks
    M["gaze"] = gz
    if widest:
        M["gaze"]["widest"] = {"game_time": round(widest[0], 4), "morphs": widest[1]}
    log("blink: %d full blinks captured; gaze: both-in max %.3f, both-out max %.3f, conjugate max %.3f over %d frames"
        % (len(blinks), gz["both_in_max"], gz["both_out_max"], gz["lr_max"], gz["frames"]))

    # ---- HOME again with the speech done, for a before/after on the same framing ----
    M["ok"] = True
    log("done after %.1f s real" % (time.time() - G["t0"]))
    save_manifest()


def tick(dt):
    G["frame"] += 1
    try:
        if G["gen"] is None:
            G["gen"] = main()
        next(G["gen"])
    except StopIteration:
        finish()
    except Exception:
        log("ERROR " + traceback.format_exc())
        finish()


def finish():
    if G["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(G["handle"])
        G["handle"] = None
    save_manifest()
    with open(OUT + "/capture_done.txt", "w") as f:
        f.write("ok\n" if M["ok"] else "failed\n")
    try:
        w = world()
        unreal.SystemLibrary.quit_game(w, unreal.GameplayStatics.get_player_controller(w, 0),
                                       unreal.QuitPreference.QUIT, False)
    except Exception:
        cmd("quit")


G["handle"] = unreal.register_slate_post_tick_callback(tick)
log("capture script armed; frames -> %s" % OUT)
