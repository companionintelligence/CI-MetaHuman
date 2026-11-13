"""The stage layout's camera and beat data, written into Config/DefaultGame.ini for the C++ director.

Deployment/stage_layout.json is the ONE source of the set: author_diorama.py builds the map from it,
and this module turns its shots and beats into the [/Script/CICompanion.CompanionStageSettings]
section that UCompanionStageSettings (a UDeveloperSettings, Config=Game) loads at startup. The
director lives in plugin C++ because the map is authored headlessly in a content-only project that
cannot carry a Sequencer asset or reference plugin classes (Docs/HANDOFF.md, Lesson 6), so the
camera's data has to reach it through config, not through the map.

author_diorama.py calls write_ini() for the project it authors (the native build). The container
authors in a different, content-only project, so the committed Config/DefaultGame.ini is what the
cook uses there: keep it in step with the layout and let `--check` prove it.

    python Deployment/stage_layout.py            # rewrite the section in Config/DefaultGame.ini
    python Deployment/stage_layout.py --check    # exit 1 if the committed section is stale

Standard library only: it runs inside Unreal's Python, on core-2 and in CI alike.
"""
import argparse
import hashlib
import json
import os
import sys

SECTION = "[/Script/CICompanion.CompanionStageSettings]"
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LAYOUT = os.path.join(HERE, "stage_layout.json")
DEFAULT_INI = os.path.join(os.path.dirname(HERE), "Config", "DefaultGame.ini")
REFERENCE_STATURE = 170.0


def _num(v):
    """A float as Unreal's config parser reads it: shortest exact form, always with a decimal point."""
    s = repr(float(v))
    return "0.0" if s in ("-0.0", "0.0") else s


def _vec2(x, y):
    return "(X=%s,Y=%s)" % (_num(x), _num(y))


def _vec3(v):
    return "(X=%s,Y=%s,Z=%s)" % (_num(v[0]), _num(v[1]), _num(v[2]))


def _shot_line(shot):
    frame = "World" if str(shot.get("frame") or "").lower().startswith("world") else "Resident"
    fb = shot.get("filmbackMm") or [36.0, 20.25]
    parts = ['Name="%s"' % shot["name"], "Frame=%s" % frame, "FocalMm=%s" % _num(shot.get("focalMm") or 50.0),
             "FilmbackMm=%s" % _vec2(fb[0], fb[1]), "Aperture=%s" % _num(shot.get("aperture") or 4.0),
             "Seconds=%s" % _num(shot.get("seconds") or 0.0)]
    if str(shot.get("easing") or "").lower().startswith("scurve"):
        parts.append("AccelFrac=%s" % _num(shot.get("accelFrac", 0.3)))
        parts.append("DecelFrac=%s" % _num(shot.get("decelFrac", 0.5)))
    if shot.get("maxRangeCm"):
        parts.append("MaxRangeCm=%s" % _num(shot["maxRangeCm"]))
    if shot.get("shiftMm"):
        parts.append("ShiftMm=%s" % _vec2(shot["shiftMm"][0], shot["shiftMm"][1]))
    keys = []
    base_screen = shot.get("screen") or [0.5, 0.5]
    for k in shot.get("keys") or []:
        if frame == "Resident":
            rr = k["residentRelative"]
            sc = k.get("screen") or base_screen
            keys.append("(RangeCm=%s,ThetaDeg=%s,DzCm=%s,Screen=%s)" % (
                _num(rr["rCm"]), _num(rr["thetaDeg"]), _num(rr.get("dzCm", 0.0)), _vec2(sc[0], sc[1])))
        else:
            keys.append("(Location=%s,LookAt=%s)" % (_vec3(k["pos"]), _vec3(k["lookAt"])))
    parts.append("Keys=(%s)" % ",".join(keys))
    return "+Shots=(%s)" % ",".join(parts)


def _cue(layout, beat, key, default=None):
    for b in layout.get("beats") or []:
        if b.get("name") == beat and key in (b.get("cue") or {}):
            return b["cue"][key]
    if default is None:
        raise KeyError("stage layout: beat %r has no cue %r" % (beat, key))
    return default


def ini_section(layout, layout_path=None):
    """The [/Script/CICompanion.CompanionStageSettings] section for this layout, as text."""
    meta = layout.get("meta") or {}
    res = layout["resident"]
    shots = [s for s in layout.get("shots") or [] if not s.get("reference") and s.get("keys")]
    names = [s["name"] for s in shots]
    for need in ("VITRINE", "ARRIVAL", "HOME"):
        if need not in names:
            raise KeyError("stage layout has no %s shot" % need)
    arrival = next(s for s in shots if s["name"] == "ARRIVAL")
    comfort = arrival.get("comfort") or {}
    digest = ""
    if layout_path and os.path.isfile(layout_path):
        with open(layout_path, "rb") as f:
            digest = hashlib.sha256(f.read().replace(b"\r\n", b"\n")).hexdigest()[:12]
    lines = [
        SECTION,
        "; GENERATED from Deployment/stage_layout.json by Deployment/stage_layout.py (author_diorama.py runs it).",
        "; Edit the layout and regenerate; never edit this section by hand. --check fails CI on a stale copy.",
        'LayoutSource="Deployment/stage_layout.json%s"' % ((" sha256:" + digest) if digest else ""),
        "ReferenceStatureCm=%s" % _num(res.get("stature_cm", REFERENCE_STATURE)),
        "EyeForwardCm=%s" % _num(meta.get("eyeForwardCm", 9.0)),
        "EyeFraction=0.93",
        "HomeFacingYaw=%s" % _num(res.get("homeFacingYaw", 180.0)),
        "LobbyShot=VITRINE",
        "OpeningShot=ARRIVAL",
        "HomeShot=HOME",
        "CloseShot=CLOSE",
        "HoldSeconds=%s" % _num(_cue(layout, "Lobby", "holdS", 3.0)),
        'GreetingLine="%s"' % str(_cue(layout, "Distance salutation", "line", "")).replace('"', "'"),
        "GreetingAtSeconds=%s" % _num(_cue(layout, "Distance salutation", "atMoveS", -1.0)),
        "LookUpDistanceCm=%s" % _num(_cue(layout, "Look up", "distanceCm")),
        "TurnDelaySeconds=%s" % _num(_cue(layout, "Look up", "turnDelayS")),
        "TurnSeconds=%s" % _num(_cue(layout, "Look up", "turnS")),
        "WorkTurnDegrees=%s" % _num(_cue(layout, "Look up", "turnDeg")),
        "SettleSpeedCmPerSecond=%s" % _num(_cue(layout, "Settle", "speedCmS")),
        "SpeakAfterSettleSeconds=%s" % _num(_cue(layout, "Turn-yield line", "afterSettleS")),
        "ReducedMotionGreetingSeconds=%s" % _num(_cue(layout, "Reduced motion", "greetingAtS", 0.8)),
        "ReducedMotionSpeakSeconds=%s" % _num(_cue(layout, "Reduced motion", "speakAtS", 2.0)),
        "MaxSpeedMetersPerSecond=%s" % _num(comfort.get("vmaxMs", 0.8)),
        "MinMoveSeconds=%s" % _num(comfort.get("minSeconds", 5.0)),
        "MaxMoveSeconds=%s" % _num(comfort.get("maxSeconds", 8.0)),
        "MinTimeToContactSeconds=%s" % _num(comfort.get("minTimeToContactS", 4.2)),
    ]
    lines += [_shot_line(s) for s in shots]
    return "\n".join(lines) + "\n"


def splice(ini_text, section_text):
    """ini_text with the stage section replaced by section_text (appended when absent)."""
    out, skipping = [], False
    for line in ini_text.replace("\r\n", "\n").split("\n"):
        if line.strip() == SECTION:
            skipping = True
            continue
        if skipping and line.startswith("["):
            skipping = False
        if not skipping:
            out.append(line)
    while out and out[-1] == "":
        out.pop()
    return "\n".join(out) + "\n\n" + section_text


def current_section(ini_text):
    lines, inside = [], False
    for line in ini_text.replace("\r\n", "\n").split("\n"):
        if line.strip() == SECTION:
            inside = True
        elif inside and line.startswith("["):
            break
        if inside:
            lines.append(line)
    while lines and lines[-1] == "":
        lines.pop()
    return "\n".join(lines) + "\n" if lines else ""


def load(layout_path=DEFAULT_LAYOUT):
    with open(layout_path, encoding="utf-8") as f:
        return json.load(f)


def write_ini(ini_path=DEFAULT_INI, layout_path=DEFAULT_LAYOUT, layout=None):
    """Rewrites the stage section of ini_path from the layout. Returns True if the file changed."""
    layout = layout if layout is not None else load(layout_path)
    section = ini_section(layout, layout_path)
    text = ""
    if os.path.isfile(ini_path):
        with open(ini_path, encoding="utf-8") as f:
            text = f.read()
    if current_section(text) == section:
        return False
    with open(ini_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(splice(text, section))
    return True


def check_ini(ini_path=DEFAULT_INI, layout_path=DEFAULT_LAYOUT):
    with open(ini_path, encoding="utf-8") as f:
        return current_section(f.read()) == ini_section(load(layout_path), layout_path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--layout", default=DEFAULT_LAYOUT)
    ap.add_argument("--ini", default=DEFAULT_INI)
    ap.add_argument("--check", action="store_true", help="exit 1 if the ini section is not what the layout gives")
    a = ap.parse_args(argv)
    if a.check:
        ok = check_ini(a.ini, a.layout)
        print("%s: stage section %s" % (a.ini, "matches the layout" if ok else "is STALE; run Deployment/stage_layout.py"))
        return 0 if ok else 1
    changed = write_ini(a.ini, a.layout)
    print("%s: stage section %s" % (a.ini, "rewritten" if changed else "already current"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
