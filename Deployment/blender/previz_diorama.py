#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 LifeScope INC, DBA Companion Intelligence (https://ci.computer)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This script imports Blender's Python API (bpy), so it is licensed under the GNU General Public
# License v3.0 or later (LICENSES/GPL-3.0-or-later.txt), not under the PolyForm Noncommercial
# licence that covers the rest of this repository. NOTICE.md explains why. The GPL does not
# extend to the images and reports the script writes.
"""previz_diorama.py - headless Blender previz of the stage set in Deployment/stage_layout.json.

author_diorama.py builds the Unreal map from the same file, and stage_layout.py hands its shots and beats
to the stage camera, so this is the cheap check of a layout change before an editor run: it builds the set
(props from the GLBs, blockouts, lights, a stand-in resident), flies every shot, renders a plan, the
opening frame and a contact sheet per shot, and reports box overlaps, camera clipping and comfort.

Run (Blender 5.1, no UI; never an interactive Blender session):
  blender --background --factory-startup --python Deployment/blender/previz_diorama.py -- ^
      [--layout Deployment/stage_layout.json] [--glb-dir GLB_DIR] --out OUT_DIR [options]
  --layout defaults to the stage_layout.json beside this folder. --glb-dir defaults to $MHLLM_GLB_DIR,
  the directory author_diorama.py loads the props from; a missing GLB renders as a magenta placeholder.
  Options: ... -- --help.   Compare sampling: ... --out OUT_DIR/arclength --only sheets --sampling arclength
  Older layouts in the previz schema (resident yaw/facingYaw, material values in the notes) still render.

Optional layout fields, all opt-in per shot or per object:
  props[].clipLocal {"x":[lo,hi],"y":[..],"z":[..]}   glTF-space metres kept (drops e.g. the RAMAC's loose
                                 platter). author_diorama.py places such props whole: the clip previews a
                                 one-time Blender edit of the GLB.
  shots[].filmbackMm [w, h]       the shot's own filmback (e.g. [20.25, 36] portrait); focalMm is on that back
  shots[].hud "lobby"|"arrival"|"live"|"portrait"   the final DOM UI zones (captions-first)
  shots[].census true             ray-cast census on every contact-sheet frame: wall-only share right of the
                                  captions (u > 0.40, or above the portrait UI band), emissive samples within
                                  +-0.10 W/H of the eyes, object edges within 0.03 W of the head silhouette,
                                  first surface behind the head, near-white share (luma > 0.8)
  blockouts[].census / props[].census   explicit census category (wall, doorway, machine, signage, ...)
  shots[].path "centripetal"     centripetal Catmull-Rom (alpha 0.5) through keys[].pos, phantom end points,
                                 parameterised by arc length (keys[].t ignored for timing)
  shots[].easing "scurve"        velocity ramps up with smootherstep over accelFrac (0.3), cruises, ramps down
                                 over decelFrac (0.5); C2 in time; peak speed = 1.667 x mean
  shots[].level true             pitch 0 and roll 0; yaw aims horizontally at the look target
  shots[].aim [x,y,z]            composition anchor (the resident's eye anchor). With "screen" the lens shift
  shots[].screen [u,v]           is solved per frame so the anchor lands at (u,v) of the frame (fractions from
  keys[].screen [u,v]            the top-left); keys[].screen eases the target along the move
  keys[].focalMm                 focal length per key, eased with the same progress curve
  shots[].shiftMm [h,v]          a fixed sensor offset instead (Blender sign: +v = rising front)
  shots[].residentHead           [[t, yawRelDeg, pitchDeg], ...] head pose on the stand-in (pitch < 0 = down)
  shots[].hud                    "opening" | "live" | "portrait" | "none": reserved UI zones drawn on the tiles
  shots[].res                    "WxH" render size for this shot (e.g. "720x1280" for the 9:16 home shot)
  shots[].sheetTimes             explicit contact-sheet times (s) instead of --frames equal steps
  props[].materialColors         {glTF material name: sRGB hex} recolour (e.g. the CI 1401 to Deep Charcoal)
  blockouts[].partOf             parts of one assembly are not tested against each other
  blockouts[].shape              "box" (default) | "cylinder" (axis from "axis": x|y|z, diameter = size on the
                                 two other axes)
  blockouts[].text               a text label extruded on the box's -X face (signage, letters, captions)
  meta.guides                    [{name, points:[[x,y],...], color}] lines drawn on top.png (sightlines, lanes)
Every frame caption also reports where the eye anchor lands (u, v), face height in px and headroom, and
report.txt adds a comfort-budget table per moving shot (speed, acceleration, yaw rate vs HFOV/7, pitch,
clearance while moving, distance to the eyes while moving, time to contact, subject growth, zoom rate).

Writes to OUT_DIR:
  top.png              orthographic plan: set, resident, lights, every shot's path with its keys,
                       look-at targets and contact-sheet frame positions, prop overlaps, camera events
  opening.png          first frame of the first shot at full resolution, caption strip underneath
  sheet_<shot>.png     one contact sheet per shot, --frames (9) frames sampled along its path
  report.json / .txt   bounding-box overlaps, camera-inside / pass-through / clearance events,
                       per-shot kinematics, exposure, and the data behind every frame
  paths/<shot>.csv     each path sampled at --report-hz;  frames/  the individual tiles

Layout JSON (Unreal centimetres and degrees, Z up, X forward, Y right; yaw turns +X toward +Y):
  props[]     {name, glb, x, y, z, yaw, scale, mesh?, partOf?}  x,y,z = actor location (the GLB scene
              origin); mesh = the single glTF node Unreal placed (null or absent = whole file).
  blockouts[] {name, kind, x, y, z, yaw, size:[x,y,z], color, rough?, emissive?, opacity?}  box centre
              and full extents along the box's own axes before yaw; color = sRGB hex. kind floor|wall|
              ceiling|facade|glass|mullion|troffer is room shell (never tested against other shell), except
              wall-mounted dressing (census "signage"), which is tested against everything.
              With rough / emissive / opacity present the material follows author_diorama.py: opacity
              (or kind glass) = glass, emissive > 0 = emissive, else paint with rough. Without them the
              notes "emissive N", "opacity N", "rough N" and "unlit" are honoured (panel|crt|troffer emit).
              emissive is cd/m2 per unit luminance of the colour, which is a DISPLAY colour (as it reads
              after meta.whiteBalanceK), exactly as author_diorama.py hands it to Unreal (--emissive nits,
              the default). tileCm / groutCm / groutColor / groutRough draw a tile grid from world position.
  resident    {x, y, z, homeFacingYaw?}  the direction the face points at home: homeFacingYaw (the
              stage_layout.json field), else facingYaw, else a "facing": "visual yaw N" note, else yaw.
  lights[]    {name, type point|spot|rect|directional, x, y, z, yaw, pitch, intensity (candela;
              lux for directional), srgb? | linear? [r, g, b], radiusCm?, shadows?, kelvin, notes, coneDeg?,
              sizeCm?, sourceRadiusCm?}  srgb (sRGB-encoded, decoded here), linear, radiusCm and shadows are
              read as author_diorama.py reads them; without them the notes "linear RGB (r, g, b)" and
              "attenuation radius N cm" (or attenuationRadiusCm), then kelvin, give the colour and reach.
              The frame's white balance (meta.whiteBalanceK, Unreal's daylight-locus white) is applied to
              the light colours, which for diffuse surfaces is the same as balancing the frame.
  shots[]     {name, purpose, focalMm, aperture, seconds, easing, keys:[{t, pos, lookAt}],
              tangentScale? (0.5 = CompanionStageDirector), timing? ("knot" | "arclength"),
              residentYaw? (number or [[t, yaw], ...]), sensorWidthMm? (36), fovDeg?, fovAxis?}
              keys[].t is seconds into the move BEFORE easing (the knot times).
  beats[]     {name, trigger, seconds, shot, resident, lights, ui, residentYaw?}

Camera maths. Shots with path "centripetal" and easing "scurve" (the stage_layout.json shots) follow the
scheme FCompanionStagePath implements (Plugins/CICompanion/.../CompanionStagePath.cpp): centripetal
Catmull-Rom, arc-length parameterisation, S-curve velocity. Shots without them (older layouts, such as
the hard-coded opening before the data-driven camera) use the old director's maths, ported line for line
from CompanionStageDirector.cpp at 0c78440, StageEase (lines 35-39), SampleSpline (274-305) and
ApplyCameraAt (307-320):
  T = StageEase(t / seconds) with StageEase = smoothstep 3T^2 - 2T^3, applied once to the move.
  Position and look-at are separate piecewise cubic Hermite splines over the keys (knots
  keys[i].t / seconds). Segment i (key i-1 -> i), FMath::CubicInterp(A, ST, B, ET, alpha):
    ST = (B - P[i-2]) / (t[i] - t[i-2]) * span * 0.5     first segment: (B - A) / span * span * 0.5
    ET = (P[i+1] - A) / (t[i+1] - t[i-1]) * span * 0.5   last segment:  (B - A) / span * span * 0.5
  rotation = (look - pos).Rotation(), roll 0. The 0.5 is --tangent-scale (1.0 = Catmull-Rom).
  --sampling chooses the contact-sheet frames: time = equal steps of shot time through that
  maths (default); arclength = equal steps of distance along the same position spline, look-at
  taken at the same spline parameter; param = equal steps of the raw parameter (no ease).
  --timing arclength re-times the move to constant speed along the same curve (eased by the
  shot's easing) so the two can be compared; the default keeps each shot's own timing.

Coordinate mapping, checked against Deployment/author_diorama.py at 0c78440, whose header
  documented web metres -> UE cm as x_ue = -100 z_w, y_ue = 100 x_w,
  z_ue = 100 y_w, yaw_ue = -deg(rotY_w). The web set is a three.js/glTF scene (Y up), and
  Blender's glTF importer maps that frame to Blender (x, -z, y). Inverting the documented mapping
  and composing:   Blender (x, y, z) m = (y_ue, x_ue, z_ue) / 100,   rotation_z = -yaw_ue.
  UE +X (glass -> back wall) is Blender +Y, UE +Y is Blender +X. The swap is a mirror, which is
  exactly what turns left-handed Unreal into right-handed Blender, so frames are not mirrored.
  The check: positions and yaw hold, but meshes do not follow that mapping in Unreal. Interchange
  maps glTF (gx, gy, gz) to actor-local (gx, gz, gy) * 100 (GLTFCore ConversionUtilities.h),
  which under the mapping above is the Blender-imported mesh turned +90 deg about Z.
  --gltf-axis unreal (default) applies that turn, so props face the way Unreal renders them;
  --gltf-axis web omits it and shows the facing the copied web yaws intended.
Lights: candela -> Blender W = 4*pi*cd/683 (point, spot), pi*cd/683 (rect); lux/683 W/m^2 (sun).
  Every light gets a custom influence distance (EEVEE's default threshold culls lights at these
  power levels). Measured: a Blender pixel is then luminance/683, so Blender exposure
  = log2(683/1.2) - EV100 = 9.15 - EV100 before the view transform.
Value structure: VITRINE and CUTAWAY_HUB report the luminous ceiling against the floor on the rendered
  frame (ray-cast samples of the "lum_" panels and "floor_tile"), and report.json keeps the samples so
  the same pixels can be read in an Unreal capture of the same camera.
This is geometry and composition previz. It is not Unreal's no-GI lighting or tonemapper (AgX here).
"""
import argparse
import csv
import json
import math
import os
import re
import sys
import textwrap
import time

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

DEFAULT_LAYOUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "stage_layout.json"))
DEFAULT_GLB_DIR = os.environ.get("MHLLM_GLB_DIR") or None   # the variable author_diorama.py loads props through
KINDA_SMALL = 1.0e-4                      # UE KINDA_SMALL_NUMBER
SHELL_KINDS = {"floor", "wall", "ceiling", "facade", "glass", "mullion", "troffer"}
FLOOR_KINDS = {"floor"}
EMISSIVE_KINDS = {"troffer", "panel", "crt"}
TOP_HIDE_KINDS = {"ceiling", "troffer"}
NEAR_PLANE_CM = 10.0                      # UE default near clip
LM_PER_W = 683.0
EV100_OFFSET = math.log2(LM_PER_W / 1.2)  # Blender exposure = EV100_OFFSET - EV100
SHOT_COLOURS = ["#e4572e", "#1f6fb4", "#2a9d3a", "#8e44ad", "#d18a00", "#0f9fb0"]
PLAN_Z = 7.0                              # overlays float here (m), above any set geometry


def log(*a):
    print("[previz]", *a, flush=True)


# ------------------------------------------------------------------------------------------ args
def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="previz_diorama.py", description="Headless Blender previz of a stage layout JSON")
    ap.add_argument("--layout", default=None,
                    help="layout JSON (Unreal cm / degrees); default: Deployment/stage_layout.json (%s)" % DEFAULT_LAYOUT)
    ap.add_argument("layout_positional", nargs="?", default=None, metavar="LAYOUT",
                    help="the layout may also be given positionally (older invocations)")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--glb-dir", default=DEFAULT_GLB_DIR,
                    help="directory holding props[].glb (default: $MHLLM_GLB_DIR)")
    ap.add_argument("--gltf-axis", choices=["unreal", "web"], default="unreal",
                    help="unreal: GLB front faces actor-local +Y as Interchange imports it; web: the web/three.js facing")
    ap.add_argument("--res", default="1280x720", help="frame size, e.g. 1280x720 or 720x1280")
    ap.add_argument("--tile-scale", type=float, default=0.5, help="contact-sheet tile size as a fraction of --res")
    ap.add_argument("--samples", type=int, default=16, help="EEVEE TAA samples for tiles (opening uses 2x)")
    ap.add_argument("--frames", type=int, default=9, help="frames per contact sheet")
    ap.add_argument("--sampling", choices=["time", "arclength", "param"], default="time",
                    help="time = equal shot-time steps through the director maths; arclength = equal distance; param = equal raw T")
    ap.add_argument("--timing", choices=["shot", "knot", "arclength"], default="shot",
                    help="how shot time maps onto the spline: the shot's own (default), the director's knot timing, or constant speed")
    ap.add_argument("--tangent-scale", type=float, default=None, help="override tangent scale (director 0.5, Catmull-Rom 1.0)")
    ap.add_argument("--ease", choices=["smoothstep", "smootherstep", "linear"], default=None, help="override shot easing")
    ap.add_argument("--figure-height", type=float, default=1.70, help="stand-in resident height in metres")
    ap.add_argument("--resident-yaw", action="append", default=[], metavar="SHOT=DEG",
                    help="facing yaw of the resident during a shot (UE degrees), overrides the layout")
    ap.add_argument("--exposure", default="auto",
                    help="auto (solve mean 120/255 on --exposure-ref, the project's method) | ev100:N | a Blender exposure value")
    ap.add_argument("--exposure-ref", default=None, help="SHOT or SHOT@seconds for the auto solve (default: last shot, last frame)")
    ap.add_argument("--emissive", choices=["display", "nits"], default="nits",
                    help="nits (default): a blockout's emissive is cd/m2 per unit luminance of its colour, as Unreal reads it; "
                         "display: emissive surfaces forced to read as lit at the solved exposure (the old previz look, which "
                         "hid that the ceiling rendered darker than the floor in Unreal)")
    ap.add_argument("--report-hz", type=float, default=120.0, help="path sampling rate for the report")
    ap.add_argument("--clearance-warn", type=float, default=30.0, help="report camera clearances below this (cm)")
    ap.add_argument("--overlap-tol", type=float, default=0.5, help="ignore box overlaps shallower than this (cm)")
    ap.add_argument("--only", default="top,opening,sheets", help="comma list of renders: top,opening,sheets")
    ap.add_argument("--top-width", type=int, default=2800, help="top.png width in pixels")
    ap.add_argument("--no-render", action="store_true", help="build the scene and write the report only")
    ap.add_argument("--save-blend", default=None, help="also save the built scene to this .blend")
    ap.add_argument("--reflections", action="store_true",
                    help="EEVEE screen-space ray tracing on (the intended SSR / baked-cubemap look; Unreal ships with reflections off today)")
    ap.add_argument("--shots", default="", help="comma list of shot names to render as sheets (default all)")
    ap.add_argument("--no-overlays", action="store_true", help="no HUD zones, thirds or eye markers on the tiles")
    ap.add_argument("--tag", default="", help="text added to every caption (variant label)")
    args = ap.parse_args(argv)
    if args.layout and args.layout_positional and os.path.abspath(args.layout) != os.path.abspath(args.layout_positional):
        ap.error("give the layout once: --layout %s or %s" % (args.layout, args.layout_positional))
    args.layout = args.layout or args.layout_positional or DEFAULT_LAYOUT
    if not args.glb_dir:
        ap.error("--glb-dir is required when MHLLM_GLB_DIR is not set")
    if not os.path.isdir(args.glb_dir):
        ap.error("--glb-dir %s is not a directory" % args.glb_dir)
    return args


# ------------------------------------------------------------------------------------- mapping
def ue2b(p):
    """UE cm (x fwd, y right, z up) -> Blender m. Mapping documented by author_diorama.py, inverted."""
    return Vector((p[1] / 100.0, p[0] / 100.0, p[2] / 100.0))


def b2ue(v):
    return (v[1] * 100.0, v[0] * 100.0, v[2] * 100.0)


def yaw2bz(yaw):
    return math.radians(-(yaw or 0.0))


def ue_dir(yaw, pitch):
    y, p = math.radians(yaw or 0.0), math.radians(pitch or 0.0)
    return (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p))


def actor_matrix(x, y, z, yaw, s=1.0):
    return Matrix.Translation(ue2b((x, y, z))) @ Matrix.Rotation(yaw2bz(yaw), 4, 'Z') @ Matrix.Diagonal((s, s, s, 1.0))


AXIS_FIX = {"unreal": Matrix.Rotation(math.radians(90.0), 4, 'Z'), "web": Matrix.Identity(4)}


def rotation_of(d):
    """FVector::Rotation(): yaw, pitch in degrees (roll 0)."""
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    return yaw, pitch


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


def rl(v, nd=1):
    return [round(float(c), nd) for c in v]


# -------------------------------------------------------------------------------------- colour
def srgb_to_lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_lin(h, default="#808080"):
    s = (h or default).strip().lstrip("#")
    if len(s) == 3:
        s = "".join(ch * 2 for ch in s)
    if len(s) != 6 or any(ch not in "0123456789abcdefABCDEF" for ch in s):
        s = default.lstrip("#")
    return tuple(srgb_to_lin(int(s[i:i + 2], 16) / 255.0) for i in (0, 2, 4))


_MATS = {}
EMISSIVE_MATS = []


def principled(m):
    if m.node_tree is None:
        m.use_nodes = True
    return next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")


def make_mat(name, rgb, rough=0.6, ue_emissive=0.0, alpha=1.0):
    key = (name, tuple(round(c, 4) for c in rgb), round(rough, 3), round(ue_emissive, 3), round(alpha, 3))
    m = _MATS.get(key)
    if m is not None:
        return m
    m = bpy.data.materials.new("pv_" + name)
    bsdf = principled(m)
    bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    if alpha < 1.0:
        bsdf.inputs["Alpha"].default_value = alpha
        try:
            m.surface_render_method = 'BLENDED'
        except Exception:
            pass
    if ue_emissive > 0.0:
        bsdf.inputs["Emission Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
        m["ue_emissive"] = float(ue_emissive)
        EMISSIVE_MATS.append(m)
    m.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
    _MATS[key] = m
    return m


def flat_mat(name, hexcol):
    return make_mat("ov_" + name, hex_lin(hexcol), rough=1.0)


def apply_emissive(exposure, mode):
    for m in EMISSIVE_MATS:
        e = float(m["ue_emissive"])
        if mode == "nits":
            s = e / LM_PER_W
        else:
            s = 0.9 * min(max(e / 4.0, 0.25), 2.0) * 2.0 ** (-exposure)
        principled(m).inputs["Emission Strength"].default_value = s


def note_num(note, key):
    m = re.search(key + r"\s*([0-9]+(?:\.[0-9]+)?)", note or "", re.I)
    return float(m.group(1)) if m else None


# ------------------------------------------------------------------------------------ images
def load_png(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return np.flipud(a.reshape(h, w, 4)).copy()


def save_png(arr, path):
    h, w = arr.shape[:2]
    img = bpy.data.images.new("previz_out", w, h, alpha=False)
    img.pixels.foreach_set(np.ascontiguousarray(np.flipud(arr)).astype(np.float32).ravel())
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


class TextCard:
    """Text strips rendered with the render stamp of an empty Workbench scene."""

    def __init__(self, tmpdir):
        self.tmp = tmpdir
        self.n = 0
        sc = bpy.data.scenes.new("previz_textcard")
        self.sc = sc
        cam = bpy.data.objects.new("textcard_cam", bpy.data.cameras.new("textcard_cam"))
        sc.collection.objects.link(cam)
        sc.camera = cam
        cam.location = (0.0, 0.0, -1000.0)
        w = bpy.data.worlds.new("textcard_world")
        w.color = (0.045, 0.048, 0.055)
        sc.world = w
        r = sc.render
        r.engine = 'BLENDER_WORKBENCH'
        r.resolution_percentage = 100
        r.image_settings.file_format = 'PNG'
        r.image_settings.color_mode = 'RGB'
        sc.view_settings.view_transform = 'Standard'
        sc.view_settings.look = 'None'
        r.use_stamp = True
        for a in dir(r):
            if a.startswith("use_stamp_") and a != "use_stamp_note":
                try:
                    setattr(r, a, False)
                except Exception:
                    pass
        r.use_stamp_note = True
        r.stamp_foreground = (0.94, 0.94, 0.92, 1.0)
        r.stamp_background = (0.0, 0.0, 0.0, 0.0)
        self.blank = {}

    def _line(self, text, w, font, fg):
        # One line per render: the stamp note is capped at 512 characters and wraps at the frame
        # width on its own, so lines are wrapped here and rendered one at a time.
        h = int(font * 1.28 + 4)
        if not text.strip():
            key = (w, h)
            if key not in self.blank:
                self.blank[key] = self._render(" ", w, h, font, FG_TEXT)
            return self.blank[key].copy()
        return self._render(text, w, h, font, fg)

    def _render(self, text, w, h, font, fg):
        r = self.sc.render
        r.resolution_x, r.resolution_y = int(w), int(h)
        r.stamp_font_size = int(font)
        r.stamp_foreground = (fg[0], fg[1], fg[2], 1.0)
        r.stamp_note_text = text
        self.n += 1
        path = os.path.join(self.tmp, "_card_%04d.png" % self.n)
        r.filepath = path
        bpy.ops.render.render(write_still=True, scene=self.sc.name)
        a = load_png(path)
        try:
            os.remove(path)
        except OSError:
            pass
        return a

    @staticmethod
    def wrap(lines, w, font):
        cpl = max(12, int((w - 12) / (0.675 * font)))
        rows = []
        for item in lines:
            text, fg = item if isinstance(item, tuple) else (item, FG_TEXT)
            segs = textwrap.wrap(text, cpl, break_long_words=True, break_on_hyphens=False) or [""]
            rows += [(s, fg) for s in segs]
        return rows

    def block(self, lines, w, font, min_rows=0):
        """lines: str or (str, rgb). Word-wrapped to w, one card per row, padded to min_rows."""
        rows = self.wrap(lines, w, font)
        rows += [("", FG_TEXT)] * max(0, min_rows - len(rows))
        return np.concatenate([self._line(t, w, font, fg) for t, fg in rows], axis=0)


FG_TEXT = (0.94, 0.94, 0.92)
FG_WARN = (1.0, 0.70, 0.18)
FG_DIM = (0.70, 0.72, 0.76)


def portrait_guides(arr, frac, colour=(1.0, 0.85, 0.2), alpha=0.5, dash=7):
    """Dashed lines at the central strip a 9:16 window keeps when the vertical FOV is held (MaintainYFOV)."""
    h, w = arr.shape[:2]
    if frac >= 0.999:
        return
    rows = (np.arange(h) // dash) % 2 == 0
    for x in (int(round(w * (1 - frac) / 2)), int(round(w * (1 + frac) / 2)) - 1):
        for xx in (x, x + 1):
            if 0 <= xx < w:
                arr[rows, xx, :3] = arr[rows, xx, :3] * (1 - alpha) + np.array(colour) * alpha


# ------------------------------------------------------------------------------- scene basics
def reset_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras, bpy.data.curves):
        for b in list(block):
            if b.users == 0:
                block.remove(b)


def new_coll(name):
    c = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(c)
    return c


def layer_coll(coll):
    return bpy.context.view_layer.layer_collection.children[coll.name]


class GlbLib:
    """Imports each GLB once (glTF node transforms kept) and hands out linked duplicates."""

    def __init__(self, glb_dir, coll):
        self.dir = glb_dir
        self.coll = coll
        self.cache = {}

    def get(self, fname):
        if fname in self.cache:
            return self.cache[fname]
        path = os.path.join(self.dir, fname)
        if not os.path.exists(path):
            log("MISSING GLB", path)
            self.cache[fname] = None
            return None
        before = set(bpy.data.objects)
        t0 = time.time()
        bpy.ops.import_scene.gltf(filepath=path)
        bpy.context.view_layer.update()
        new = [o for o in bpy.data.objects if o not in before]
        stem = os.path.splitext(fname)[0]
        entries = []
        for o in new:
            node = o.name
            mw = o.matrix_world.copy()
            for c in list(o.users_collection):
                c.objects.unlink(o)
            self.coll.objects.link(o)
            o.name = stem + "|" + node
            if o.type == 'MESH':
                entries.append({"node": node, "mesh": o.data.name, "obj": o, "mw": mw})
        for e in entries:   # Workbench shows diffuse_color for untextured glTF materials
            for m in e["obj"].data.materials:
                if m is None or m.node_tree is None:
                    continue
                b = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
                if b is not None and not b.inputs["Base Color"].is_linked:
                    m.diffuse_color = tuple(b.inputs["Base Color"].default_value)
        log("imported %-26s %3d mesh nodes in %.1fs" % (fname, len(entries), time.time() - t0))
        self.cache[fname] = entries
        return entries


def strip_suffix(n):
    return re.sub(r"\.\d{3}$", "", n)


def unit_cube_mesh():
    me = bpy.data.meshes.new("pv_unit_cube")
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(make_mat("default", (0.5, 0.5, 0.5)))
    return me


# --------------------------------------------------------------------------------------- props
def clipped_mesh(e, clip):
    """Copy of a GLB node's mesh with its node transform baked (Blender import space) and every vertex outside the
    glTF-space box dropped (with its faces). glTF (gx, gy, gz) = Blender import (bx, bz, -by)."""
    me = e["obj"].data.copy()
    me.transform(e["mw"])
    bm = bmesh.new()
    bm.from_mesh(me)
    lim = {k: (float(v[0]), float(v[1])) for k, v in clip.items()}
    bad = []
    for v in bm.verts:
        g = {"x": v.co.x, "y": v.co.z, "z": -v.co.y}
        if any(not (lo <= g[k] <= hi) for k, (lo, hi) in lim.items()):
            bad.append(v)
    bmesh.ops.delete(bm, geom=bad, context='VERTS')
    bm.to_mesh(me)
    bm.free()
    return me


def place_prop(p, lib, axis_fix, coll, cube_me):
    name = str(p.get("name"))
    entries = lib.get(p.get("glb")) if p.get("glb") else None
    sel, status = [], "ok"
    if entries is None:
        status = "missing-glb"
    elif p.get("mesh"):
        want = p["mesh"]
        sel = [e for e in entries if e["node"] == want] or \
              [e for e in entries if strip_suffix(e["node"]) == want] or \
              [e for e in entries if strip_suffix(e["mesh"]) == want]
        if not sel:
            status = "missing-mesh"
    else:
        sel = entries
    objs = []
    if sel:
        A = actor_matrix(p["x"], p["y"], p["z"], p.get("yaw", 0.0), float(p.get("scale") or 1.0)) @ axis_fix
        recolour = {str(k): v for k, v in (p.get("materialColors") or {}).items()}
        clip = p.get("clipLocal")
        for e in sel:
            if clip:
                o = bpy.data.objects.new("prop|" + name + "|" + e["node"], clipped_mesh(e, clip))
                coll.objects.link(o)
                o.matrix_world = A
            else:
                o = bpy.data.objects.new("prop|" + name + "|" + e["node"], e["obj"].data)
                coll.objects.link(o)
                o.matrix_world = A @ e["mw"]
            if recolour:
                for slot in o.material_slots:
                    m = slot.material
                    if m is None:
                        continue
                    key = strip_suffix(m.name)
                    if key not in recolour:
                        continue
                    m2 = m.copy()
                    m2.name = "rc_" + name + "_" + key
                    b = next((n for n in m2.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None) if m2.node_tree else None
                    rgb = hex_lin(recolour[key])
                    if b is not None:
                        for lk in list(b.inputs["Base Color"].links):
                            m2.node_tree.links.remove(lk)
                        b.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
                        ec = b.inputs.get("Emission Color")
                        es = b.inputs.get("Emission Strength")
                        if ec is not None and es is not None and es.default_value > 0 and max(ec.default_value[:3]) > 0:
                            ec.default_value = (rgb[0], rgb[1], rgb[2], 1.0)
                    m2.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
                    slot.link = 'OBJECT'
                    slot.material = m2
            objs.append(o)
    else:
        ext = p.get("extentCm") or [50.0, 50.0, 50.0]
        o = bpy.data.objects.new("prop|" + name + "|PLACEHOLDER", cube_me)
        coll.objects.link(o)
        o.matrix_world = (Matrix.Translation(ue2b((p["x"], p["y"], p["z"] + ext[2] / 2.0)))
                          @ Matrix.Rotation(yaw2bz(p.get("yaw", 0.0)), 4, 'Z')
                          @ Matrix.Diagonal((max(ext[1], 1) / 100.0, max(ext[0], 1) / 100.0, max(ext[2], 1) / 100.0, 1.0)))
        o.material_slots[0].link = 'OBJECT'
        o.material_slots[0].material = make_mat("placeholder", hex_lin("#ff00ff"))
        objs.append(o)
        log("PLACEHOLDER for %s (%s: glb=%s mesh=%s)" % (name, status, p.get("glb"), p.get("mesh")))
    return objs, status


def tile_mat(b, rgb):
    """Square tile with grout lines from world position, as author_diorama.py's M_StageTile draws it."""
    key = ("tile", b.get("color"), b.get("tileCm"), b.get("groutCm"), b.get("groutColor"))
    m = _MATS.get(key)
    if m is not None:
        return m
    m = bpy.data.materials.new("pv_tile_" + str(b.get("name")))
    bsdf = principled(m)
    nt = m.node_tree
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    brick = nt.nodes.new("ShaderNodeTexBrick")
    brick.offset = 0.0
    brick.squash = 1.0
    tile_m = float(b["tileCm"]) / 100.0
    brick.inputs["Scale"].default_value = 1.0
    brick.inputs["Brick Width"].default_value = tile_m
    brick.inputs["Row Height"].default_value = tile_m
    brick.inputs["Mortar Size"].default_value = float(b.get("groutCm", 0.6)) / 100.0 / 2.0
    brick.inputs["Mortar Smooth"].default_value = 0.1
    g = hex_lin(b.get("groutColor") or "#BDB7AA")
    brick.inputs["Color1"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    brick.inputs["Color2"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    brick.inputs["Mortar"].default_value = (g[0], g[1], g[2], 1.0)
    nt.links.new(geo.outputs["Position"], brick.inputs["Vector"])
    nt.links.new(brick.outputs["Color"], bsdf.inputs["Base Color"])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = 'FLOAT'
    mix.inputs["A"].default_value = float(b.get("rough", 0.2))
    mix.inputs["B"].default_value = float(b.get("groutRough", 0.75))
    nt.links.new(brick.outputs["Fac"], mix.inputs["Factor"])
    nt.links.new(mix.outputs["Result"], bsdf.inputs["Roughness"])
    m.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
    _MATS[key] = m
    return m


def blockout_material(b):
    kind = (b.get("kind") or "").lower()
    note = b.get("note") or ""
    rgb = hex_lin(b.get("color"))
    if b.get("tileCm"):
        return tile_mat(b, rgb)
    if any(k in b for k in ("rough", "emissive", "opacity")):
        # stage_layout.json states the material values; pick the material as author_diorama.py does
        if kind == "glass" or "opacity" in b:
            return make_mat("glass", rgb, rough=float(b.get("rough", 0.05)), alpha=float(b.get("opacity", 0.15)))
        if b.get("emissive"):
            return make_mat("emit_" + kind, rgb, rough=float(b.get("rough", 0.4)), ue_emissive=float(b["emissive"]))
        return make_mat("bk_" + kind, rgb, rough=float(b.get("rough", 0.6)))
    if kind == "glass":
        return make_mat("glass", rgb, rough=0.05, alpha=note_num(note, r"opacity") or 0.15)
    emis = note_num(note, r"emissive")
    if emis is None and (kind in EMISSIVE_KINDS or "unlit" in note.lower()):
        emis = 4.0
    if emis:
        return make_mat("emit_" + kind, rgb, rough=0.4, ue_emissive=emis)
    return make_mat("bk_" + kind, rgb, rough=note_num(note, r"rough(?:ness)?") or 0.6)


_CYL = {}


def unit_cylinder_mesh(axis):
    """Unit cylinder (diameter 1, length 1) along Blender local X|Y|Z for UE local axis y|x|z."""
    if axis in _CYL:
        return _CYL[axis]
    me = bpy.data.meshes.new("pv_unit_cyl_" + axis)
    bm = bmesh.new()
    rot = {"z": Matrix.Identity(4), "x": Matrix.Rotation(math.radians(90.0), 4, 'X'),   # UE x = Blender y
           "y": Matrix.Rotation(math.radians(90.0), 4, 'Y')}[axis]                       # UE y = Blender x
    bmesh.ops.create_cone(bm, cap_ends=True, segments=28, radius1=0.5, radius2=0.5, depth=1.0, matrix=rot)
    for f in bm.faces:
        f.smooth = len(f.verts) == 4
    bm.to_mesh(me)
    bm.free()
    me.materials.append(make_mat("default", (0.5, 0.5, 0.5)))
    _CYL[axis] = me
    return me


def place_blockout(b, coll, cube_me):
    sx, sy, sz = [float(v) for v in (b.get("size") or [10, 10, 10])]
    shape = (b.get("shape") or "box").lower()
    me = unit_cylinder_mesh((b.get("axis") or "z").lower()) if shape.startswith("cyl") else cube_me
    o = bpy.data.objects.new("bk|" + str(b.get("name")), me)
    coll.objects.link(o)
    # UE local X extent -> Blender local Y, UE local Y extent -> Blender local X
    o.matrix_world = (Matrix.Translation(ue2b((b["x"], b["y"], b["z"])))
                      @ Matrix.Rotation(yaw2bz(b.get("yaw", 0.0)), 4, 'Z')
                      @ Matrix.Diagonal((max(sy, 0.2) / 100.0, max(sx, 0.2) / 100.0, max(sz, 0.2) / 100.0, 1.0)))
    o.material_slots[0].link = 'OBJECT'
    o.material_slots[0].material = blockout_material(b)
    if b.get("castShadow") is False:
        try:
            o.visible_shadow = False
        except Exception:
            pass
    txt = b.get("text")
    if txt:
        place_text(b, coll)
    return o


def place_text(b, coll):
    """Text on the box's local -X face (the face a wall sign shows to a room on its -X side), centred,
    cap height = textSize (cm, default 70% of the box height), extruded 0.6 cm, colour textColor."""
    sx, sy, sz = [float(v) for v in (b.get("size") or [10, 10, 10])]
    cu = bpy.data.curves.new("pv_sign", 'FONT')
    cu.body = str(b["text"])
    cu.size = float(b.get("textSize") or 0.7 * sz) / 100.0 / 0.72
    cu.align_x = 'CENTER'
    cu.align_y = 'CENTER'
    cu.extrude = 0.003
    try:
        cu.space_character = float(b.get("textSpacing") or 1.0)
    except Exception:
        pass
    o = bpy.data.objects.new("bk|" + str(b.get("name")) + "|text", cu)
    coll.objects.link(o)
    face = -1.0 if (b.get("textFace") or "-x") == "-x" else 1.0
    # text curves lie in Blender XY facing +Z; stand them up facing UE -X (Blender -Y) on the box face
    M = (Matrix.Translation(ue2b((b["x"], b["y"], b["z"])))
         @ Matrix.Rotation(yaw2bz(b.get("yaw", 0.0)), 4, 'Z')
         @ Matrix.Translation((0.0, face * (sx / 200.0 + 0.004), 0.0))
         @ Matrix.Rotation(math.radians(90.0), 4, 'X')
         @ (Matrix.Rotation(math.radians(180.0), 4, 'Y') if face > 0 else Matrix.Identity(4)))
    o.matrix_world = M
    col = hex_lin(b.get("textColor") or "#111111")
    emis = note_num(b.get("textNote") or "", r"emissive")
    o.active_material = make_mat("txt_" + str(b.get("textColor")), col, rough=0.5, ue_emissive=emis or 0.0)
    return o


# ---------------------------------------------------------------------------------------- grid
def build_grid(blockouts, coll):
    floors = [b for b in blockouts if (b.get("kind") or "").lower() in FLOOR_KINDS]
    if not floors:
        floors = [{"x": 0, "y": 0, "z": 0, "size": [2000, 2000, 0]}]
    mats = (make_mat("grid_minor", (0.33, 0.33, 0.33), rough=0.8), make_mat("grid_major", (0.12, 0.12, 0.13), rough=0.8))
    for fi, f in enumerate(floors):
        sx, sy, sz = [float(v) for v in f["size"]]
        x0, x1 = f["x"] - sx / 2.0, f["x"] + sx / 2.0
        y0, y1 = f["y"] - sy / 2.0, f["y"] + sy / 2.0
        z = f["z"] + max(sz, 0.2) / 2.0 + 0.12
        for major in (False, True):
            bm = bmesh.new()
            w = 2.6 if major else 1.1
            for x in range(int(math.ceil(x0 / 100.0)) * 100, int(math.floor(x1 / 100.0)) * 100 + 1, 100):
                if (x % 500 == 0) == major:
                    add_ue_quad(bm, (x, y0), (x, y1), w, z)
            for y in range(int(math.ceil(y0 / 100.0)) * 100, int(math.floor(y1 / 100.0)) * 100 + 1, 100):
                if (y % 500 == 0) == major:
                    add_ue_quad(bm, (x0, y), (x1, y), w, z)
            me = bpy.data.meshes.new("pv_grid")
            bm.to_mesh(me)
            bm.free()
            o = bpy.data.objects.new("grid|%d|%s" % (fi, "major" if major else "minor"), me)
            coll.objects.link(o)
            o.active_material = mats[1 if major else 0]


def add_ue_quad(bm, p0, p1, w, z):
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    L = math.hypot(dx, dy)
    if L < 1e-6:
        return
    nx, ny = -dy / L * w / 2.0, dx / L * w / 2.0
    pts = [(p0[0] + nx, p0[1] + ny), (p1[0] + nx, p1[1] + ny), (p1[0] - nx, p1[1] - ny), (p0[0] - nx, p0[1] - ny)]
    f = bm.faces.new([bm.verts.new(ue2b((x, y, z))) for x, y in pts])
    f.normal_update()
    if f.normal.z < 0:
        f.normal_flip()


# ------------------------------------------------------------------------------------ resident
NECK_Z = 145.0          # head pivot height on the 1.70 m stand-in (cm, scaled with the figure)
EYE_LOCAL = (9.0, 0.0, 158.1)      # eye midpoint, stand-in local (x fwd, y right, z), 1.70 m figure
CROWN_LOCAL = (-1.0, 0.0, 170.6)   # top of the hair
CHIN_LOCAL = (8.0, 0.0, 147.6)


def build_figure(height_m, coll):
    """1.70 m (default) stand-in in period dress (blue-grey fitted dress, knee length, one CI-teal accent at
    the collar), built facing UE-local +X. The head hangs on its own pivot so shots can key a look-down /
    look-up (residentHead); the eye anchor the shots aim at is the eye midpoint with the head level."""
    k = height_m * 100.0 / 170.0
    root = bpy.data.objects.new("resident_root", None)
    coll.objects.link(root)
    head = bpy.data.objects.new("resident_head", None)
    coll.objects.link(head)
    head.parent = root
    head.location = Vector((0.0, 0.0, NECK_Z)) * (k / 100.0)
    skin = make_mat("fig_skin", hex_lin("#c99a80"), 0.55)
    cloth = make_mat("fig_cloth", hex_lin("#5f7186"), 0.8)       # blue-grey dress
    collar = make_mat("fig_collar", hex_lin("#ece8df"), 0.7)      # white collar
    accent = make_mat("fig_accent", hex_lin("#0a6358"), 0.5)      # CI phthalo brooch / scarf: the one brand tell
    legs = make_mat("fig_legs", hex_lin("#b88f7a"), 0.6)          # stockings
    shoe = make_mat("fig_shoe", hex_lin("#1c1c20"), 0.5)
    hair = make_mat("fig_hair", hex_lin("#2c1d14"), 0.7)
    eye = make_mat("fig_eye", (0.005, 0.005, 0.005), 0.3)
    parts = [  # kind, centre (x fwd, y right, z) cm, radii (x, y, z) cm, material, on-head
        ("cyl", (0, -8.5, 26), (5.2, 5.2, 26), legs, 0), ("cyl", (0, 8.5, 26), (5.2, 5.2, 26), legs, 0),
        ("sph", (4, -8.5, 3.2), (11, 4.6, 3.5), shoe, 0), ("sph", (4, 8.5, 3.2), (11, 4.6, 3.5), shoe, 0),
        ("cone", (0, 0, 74), (14.5, 21, 24), cloth, 0),            # skirt, waist 98 -> knee 50
        ("cyl", (0, 0, 115), (11, 16.5, 22), cloth, 0), ("sph", (0, 0, 137), (11, 21, 7), cloth, 0),
        ("cyl", (0, -22.5, 109), (4.3, 4.3, 29), cloth, 0), ("cyl", (0, 22.5, 109), (4.3, 4.3, 29), cloth, 0),
        ("sph", (1, -22.5, 77), (3.5, 2.5, 5), skin, 0), ("sph", (1, 22.5, 77), (3.5, 2.5, 5), skin, 0),
        ("sph", (4.5, 0, 140.5), (7.5, 11, 2.6), collar, 0),
        ("sph", (10.6, -5.5, 137.5), (1.2, 2.2, 2.2), accent, 0),
        ("cyl", (0, 0, 145), (5.2, 5.2, 7), skin, 1),
        ("sph", (0, 0, 158.5), (10.0, 8.6, 11.5), skin, 1),
        ("sph", (-2.2, 0, 160.6), (11.0, 9.7, 11.9), hair, 1),          # a period bob: hairline, sides, back
        ("sph", (9.0, -3.3, 158.1), (1.3, 1.3, 1.3), eye, 1), ("sph", (9.0, 3.3, 158.1), (1.3, 1.3, 1.3), eye, 1),
        ("sph", (9.9, 0, 154.5), (1.6, 1.2, 2.0), skin, 1),
    ]
    for i, (kind, c, r, mat, on_head) in enumerate(parts):
        bm = bmesh.new()
        if kind == "sph":
            bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=14, radius=1.0)
        elif kind == "cone":
            bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=1.0, radius2=0.62, depth=2.0)
        else:
            bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=1.0, radius2=1.0, depth=2.0)
        for f in bm.faces:
            f.smooth = True
        me = bpy.data.meshes.new("pv_fig_%d" % i)
        bm.to_mesh(me)
        bm.free()
        o = bpy.data.objects.new("resident|part%d" % i, me)
        coll.objects.link(o)
        loc = Vector((c[1], c[0], c[2])) * (k / 100.0)
        if on_head:
            o.parent = head
            o.location = loc - Vector((0.0, 0.0, NECK_Z * k / 100.0))
        else:
            o.parent = root
            o.location = loc
        o.scale = (r[1] * k / 100.0, r[0] * k / 100.0, r[2] * k / 100.0)
        o.active_material = mat
    root["k"] = k
    return root


def set_figure(root, mark, yaw, head=None):
    root.location = ue2b(mark)
    root.rotation_euler = (0.0, 0.0, yaw2bz(yaw))
    hd = next((c for c in root.children if c.name.startswith("resident_head")), None)
    if hd is not None:
        hy, hp = (head or (0.0, 0.0))
        # Blender local: x = UE right, y = UE forward. R = Rz(-yaw) . Rx(pitch): nod about the right axis,
        # then turn about the vertical (pitch < 0 = nose down, yaw > 0 = toward her right, UE sense).
        hd.rotation_mode = 'XYZ'
        hd.rotation_euler = (math.radians(hp), 0.0, math.radians(-hy))


def figure_point(mark, yaw, k, local, head=None):
    """World UE position of a stand-in landmark (local x fwd, y right, z up, 1.70 m figure) for a body yaw
    and head pose (yawRel, pitch); the same rotation set_figure gives the head pivot."""
    x, y, z = [c * k for c in local]
    if head is not None and z > NECK_Z * k - 1.0:
        hy, hp = head
        pz = NECK_Z * k
        dz = z - pz
        a = math.radians(hp)
        x, dz = x * math.cos(a) - dz * math.sin(a), x * math.sin(a) + dz * math.cos(a)
        b = math.radians(hy)
        x, y = x * math.cos(b) - y * math.sin(b), x * math.sin(b) + y * math.cos(b)
        z = pz + dz
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (mark[0] + x * c - y * s, mark[1] + x * s + y * c, mark[2] + z)


def resident_base_facing(res):
    if isinstance(res.get("homeFacingYaw"), (int, float)):
        return float(res["homeFacingYaw"]), "resident.homeFacingYaw"
    if isinstance(res.get("facingYaw"), (int, float)):
        return float(res["facingYaw"]), "resident.facingYaw"
    f = res.get("facing")
    if isinstance(f, (int, float)):
        return float(f), "resident.facing"
    if isinstance(f, str):
        m = re.search(r"visual yaw\s*(-?\d+(?:\.\d+)?)", f)
        if m:
            return float(m.group(1)), "resident.facing text '%s'" % m.group(0)
    return float(res.get("yaw") or 0.0), "resident.yaw"


DIR_YAW = {("+", "x"): 0.0, ("-", "x"): 180.0, ("+", "y"): 90.0, ("-", "y"): -90.0}


def resolve_facing(shot, beats, base, overrides):
    name = shot.get("name")
    if name in overrides:
        return overrides[name], "--resident-yaw"
    if shot.get("residentYaw") is not None:
        return shot["residentYaw"], "shot.residentYaw"
    for b in beats:
        if b.get("shot") == name and b.get("residentYaw") is not None:
            return b["residentYaw"], "beat '%s' residentYaw" % b.get("name")
    for b in beats:
        txt = b.get("resident")
        if b.get("shot") == name and isinstance(txt, str):
            m = re.search(r"\bfac(?:es|ing)\s+([+-])\s*([XYxy])\b", txt)
            if m:
                return DIR_YAW[(m.group(1), m.group(2).lower())], "beat '%s' text '%s'" % (b.get("name"), m.group(0))
    return base, "resident default"


def _sstep(u):
    u = min(max(u, 0.0), 1.0)
    return u * u * (3.0 - 2.0 * u)


def facing_at(f, t):
    """Body yaw at shot time t: a number, or [[t, yaw], ...] with each segment eased (smoothstep)."""
    if isinstance(f, (int, float)):
        return float(f)
    ks = sorted([(float(a), float(b)) for a, b in f])
    if t <= ks[0][0]:
        return ks[0][1]
    for (t0, y0), (t1, y1) in zip(ks[:-1], ks[1:]):
        if t <= t1:
            u = _sstep((t - t0) / max(t1 - t0, 1e-6))
            return y0 + wrap180(y1 - y0) * u
    return ks[-1][1]


def head_at(shot, t):
    """(yawRel, pitch) of the stand-in's head at shot time t from shot.residentHead [[t, yaw, pitch], ...]."""
    ks = shot.get("residentHead") if isinstance(shot, dict) else None
    if not ks:
        return (0.0, 0.0)
    ks = sorted([(float(a), float(b), float(c)) for a, b, c in ks])
    if t <= ks[0][0]:
        return (ks[0][1], ks[0][2])
    for (t0, y0, p0), (t1, y1, p1) in zip(ks[:-1], ks[1:]):
        if t <= t1:
            u = _sstep((t - t0) / max(t1 - t0, 1e-6))
            return (y0 + (y1 - y0) * u, p0 + (p1 - p0) * u)
    return (ks[-1][1], ks[-1][2])


# -------------------------------------------------------------------------------------- lights
def parse_rgb(notes):
    m = re.search(r"linear RGB \(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\)", notes or "")
    return tuple(max(0.0, float(m.group(i))) for i in (1, 2, 3)) if m else None


def parse_radius_cm(l):
    if isinstance(l.get("radiusCm"), (int, float)):
        return float(l["radiusCm"])
    if isinstance(l.get("attenuationRadiusCm"), (int, float)):
        return float(l["attenuationRadiusCm"])
    m = re.search(r"attenuation radius\s*([\d.]+)\s*cm", l.get("notes") or "")
    return float(m.group(1)) if m else None


WB_WHITE = (1.0, 1.0, 1.0)     # set from meta.whiteBalanceK in main()


def white_balance_white(kelvin):
    """Linear sRGB (largest channel 1) of the white Unreal's white balance neutralises at WhiteTemp = kelvin, tint 0
    (TonemapCommon.ush: the daylight locus from 4000 K, Planckian below). author_diorama.py has the same function."""
    T = float(kelvin)
    if T >= 4000.0:
        T *= 1.4388 / 1.438
        t = 1.0 / T
        x = (0.244063 + (0.09911e3 + (2.9678e6 - 4.6070e9 * t) * t) * t) if T <= 7000.0 else \
            (0.237040 + (0.24748e3 + (1.9018e6 - 2.0064e9 * t) * t) * t)
        y = -3.0 * x * x + 2.87 * x - 0.275
    else:
        u = (0.860117757 + 1.54118254e-4 * T + 1.28641212e-7 * T * T) / (1.0 + 8.42420235e-4 * T + 7.08145163e-7 * T * T)
        v = (0.317398726 + 4.22806245e-5 * T + 4.20481691e-8 * T * T) / (1.0 - 2.89741816e-5 * T + 1.61456053e-7 * T * T)
        x = 3.0 * u / (2.0 * u - 8.0 * v + 4.0)
        y = 2.0 * v / (2.0 * u - 8.0 * v + 4.0)
    X, Z = x / y, (1.0 - x - y) / y
    rgb = (3.2404542 * X - 1.5371385 - 0.4985314 * Z, -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
           0.0556434 * X - 0.2040259 + 1.0572252 * Z)
    m = max(rgb)
    return tuple(c / m for c in rgb)


def balanced(rgb):
    """A scene-linear light colour after the white balance: divided by the balanced white, times that white's
    luminance (von Kries in linear sRGB; the white keeps its luminance, as in Unreal's chromatic adaptation)."""
    k = 0.2126 * WB_WHITE[0] + 0.7152 * WB_WHITE[1] + 0.0722 * WB_WHITE[2]
    return tuple(c / w * k for c, w in zip(rgb, WB_WHITE))


def place_light(l, coll):
    typ = (l.get("type") or "point").lower()
    I = float(l.get("intensity") or 0.0)
    name = "light|" + str(l.get("name"))
    if typ in ("directional", "sun"):
        ld = bpy.data.lights.new(name, 'SUN')
        ld.energy = I / LM_PER_W
        ld.angle = math.radians(float(l.get("angleDeg") or 0.5))
    elif typ == "spot":
        ld = bpy.data.lights.new(name, 'SPOT')
        ld.energy = 4.0 * math.pi * I / LM_PER_W
        ld.spot_size = math.radians(float(l.get("coneDeg") or 60.0))
        ld.spot_blend = 0.3
    elif typ in ("rect", "area"):
        ld = bpy.data.lights.new(name, 'AREA')
        ld.shape = 'RECTANGLE'
        sz = l.get("sizeCm") or [100.0, 100.0]
        ld.size, ld.size_y = sz[0] / 100.0, sz[1] / 100.0
        ld.energy = math.pi * I / LM_PER_W
    else:
        ld = bpy.data.lights.new(name, 'POINT')
        ld.energy = 4.0 * math.pi * I / LM_PER_W
    if ld.type != 'SUN':
        ld.shadow_soft_size = float(l.get("sourceRadiusCm") or 0.0) / 100.0
        try:
            ld.use_soft_falloff = False
        except Exception:
            pass
        r = parse_radius_cm(l)
        ld.use_custom_distance = True
        ld.cutoff_distance = (r / 100.0) if r else 60.0
    if "shadows" in l:
        ld.use_shadow = bool(l["shadows"])
    # srgb: the colour sRGB-encoded (author_diorama.py decodes it the same way before set_light_color); linear:
    # already linear. The frame's white balance (meta.whiteBalanceK) is applied here, on the lights; emissive
    # colours are display colours and need none.
    enc, lin = l.get("srgb"), l.get("linear")
    if isinstance(enc, (list, tuple)) and len(enc) >= 3:
        rgb = tuple(srgb_to_lin(max(0.0, float(c))) for c in enc[:3])
    elif isinstance(lin, (list, tuple)) and len(lin) >= 3:
        rgb = tuple(max(0.0, float(c)) for c in lin[:3])
    else:
        rgb = parse_rgb(l.get("notes"))
    if rgb:
        ld.color = balanced(rgb)
    elif l.get("kelvin"):
        try:
            ld.use_temperature = True
            ld.temperature = float(l["kelvin"])
        except Exception:
            pass
    ob = bpy.data.objects.new(name, ld)
    coll.objects.link(ob)
    ob.location = ue2b((l.get("x") or 0.0, l.get("y") or 0.0, l.get("z") or 0.0))
    if ld.type in ('SUN', 'SPOT', 'AREA'):
        d = ue2b(ue_dir(l.get("yaw") or 0.0, l.get("pitch") or 0.0))
        ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return ob


# -------------------------------------------------------------------------------------- camera
def lens_for(shot):
    sw = float(shot.get("sensorWidthMm") or 36.0)
    sh = sw * 9.0 / 16.0                      # focalMm is across the long side of a 16:9 frame
    fov = shot.get("fovDeg")
    if fov:
        if (shot.get("fovAxis") or "vertical").lower().startswith("v"):
            lens = (sh / 2.0) / math.tan(math.radians(float(fov)) / 2.0)
        else:
            lens = (sw / 2.0) / math.tan(math.radians(float(fov)) / 2.0)
    else:
        lens = float(shot.get("focalMm") or 22.7)
        fb = shot.get("filmbackMm")
        if fb:                                # a portrait (or other) back: hold ITS vertical angle on the 20.25 mm render back
            lens = lens * sh / float(fb[1])
    vfov = math.degrees(2.0 * math.atan((sh / 2.0) / lens))
    return lens, sw, sh, vfov


def lens_label(shot, f_eff):
    """Human label for a frame's lens: the focal on the shot's own filmback."""
    fb = shot.get("filmbackMm")
    if fb:
        return "%.0f mm on a %.2f x %.0f mm back" % (f_eff * float(fb[1]) / 20.25, float(fb[0]), float(fb[1]))
    return "%.0f mm" % f_eff


def apply_lens(cam, shot, focal=None, shift=(0.0, 0.0)):
    lens, sw, sh, vfov = lens_for(shot)
    if focal:
        lens = float(focal)
        vfov = math.degrees(2.0 * math.atan((sh / 2.0) / lens))
    cd = cam.data
    cd.sensor_fit = 'VERTICAL'               # vertical angle held across aspects (UE MaintainYFOV)
    cd.sensor_width, cd.sensor_height = sw, sh
    cd.lens = lens
    # Blender shift is in units of the fitted (vertical) frame size; + = window right / up
    cd.shift_x = float(shift[0]) / sh
    cd.shift_y = float(shift[1]) / sh
    ap = shot.get("aperture")
    cd.dof.use_dof = bool(ap)
    if ap:
        cd.dof.aperture_fstop = float(ap)
    return lens, vfov


def pose_camera(cam, pos, look, level=False, focus=None):
    pb, lb = ue2b(pos), ue2b(look)
    cam.location = pb
    d = lb - pb
    if level:
        d = Vector((d.x, d.y, 0.0))
    if d.length < 1e-6:
        d = Vector((0.0, 1.0, 0.0))
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    if cam.data.dof.use_dof:
        cam.data.dof.focus_distance = max((focus if focus else (lb - pb).length), 0.1)


def draw_overlays(img, hud, eye_uv, target_uv):
    """Reserved UI zones (tinted), rule-of-thirds ticks and the eye anchor marker, drawn on a tile in place."""
    h, w = img.shape[:2]

    def tint(x0, y0, x1, y1, col, a):
        xs, xe = int(max(0, x0 * w)), int(min(w, x1 * w))
        ys, ye = int(max(0, y0 * h)), int(min(h, y1 * h))
        if xe > xs and ye > ys:
            img[ys:ye, xs:xe, :3] = img[ys:ye, xs:xe, :3] * (1 - a) + np.array(col) * a

    def outline(x0, y0, x1, y1, col, th=2):
        tint(x0, y0, x1, y0 + th / h, col, 0.9)
        tint(x0, y1 - th / h, x1, y1, col, 0.9)
        tint(x0, y0, x0 + th / w, y1, col, 0.9)
        tint(x1 - th / w, y0, x1, y1, col, 0.9)
    navy = (0.016, 0.086, 0.125)
    cyan = (0.45, 0.86, 0.83)
    if hud == "live":            # DOM, captions first: her line on a dark plate, the visitor's last line, input row
        tint(0.03, 0.52, 0.40, 0.86, navy, 0.45)
        outline(0.03, 0.52, 0.40, 0.86, cyan)
        tint(0.03, 0.88, 0.55, 0.96, navy, 0.55)
        outline(0.03, 0.88, 0.55, 0.96, cyan)
    elif hud in ("opening", "arrival"):   # arrival: her caption only ("Oh, hello."), no input row yet
        tint(0.03, 0.70, 0.40, 0.86, navy, 0.35)
        outline(0.03, 0.70, 0.40, 0.86, cyan)
    elif hud == "lobby":         # lobby copy and the Step inside button in the dark canopy band
        tint(0.06, 0.10, 0.48, 0.24, navy, 0.40)
        outline(0.06, 0.10, 0.48, 0.24, cyan)
        tint(0.06, 0.255, 0.24, 0.305, (0.85, 0.85, 0.82), 0.55)
        outline(0.06, 0.255, 0.24, 0.305, cyan)
    elif hud == "lobby_portrait":  # portrait lobby: copy and button in the soffit band
        tint(0.08, 0.17, 0.92, 0.33, navy, 0.40)
        outline(0.08, 0.17, 0.92, 0.33, cyan)
        tint(0.08, 0.345, 0.50, 0.39, (0.85, 0.85, 0.82), 0.55)
        outline(0.08, 0.345, 0.50, 0.39, cyan)
    elif hud == "portrait":
        tint(0.05, 0.70, 0.95, 0.86, navy, 0.45)
        outline(0.05, 0.70, 0.95, 0.86, cyan)
        tint(0.05, 0.88, 0.95, 0.95, navy, 0.55)
        outline(0.05, 0.88, 0.95, 0.95, cyan)
    if hud != "none":
        for fx in (1 / 3.0, 2 / 3.0):           # thirds ticks at the frame edges only
            tint(fx - 0.6 / w, 0.0, fx + 0.6 / w, 0.03, (1, 1, 1), 0.7)
            tint(fx - 0.6 / w, 0.97, fx + 0.6 / w, 1.0, (1, 1, 1), 0.7)
        for fy in (1 / 3.0, 2 / 3.0):
            tint(0.0, fy - 0.6 / h, 0.02, fy + 0.6 / h, (1, 1, 1), 0.7)
            tint(0.98, fy - 0.6 / h, 1.0, fy + 0.6 / h, (1, 1, 1), 0.7)
    for uv, col in ((target_uv, (0.2, 1.0, 0.6)), (eye_uv, (1.0, 0.85, 0.1))):
        if uv is None or not (0.0 <= uv[0] <= 1.0 and 0.0 <= uv[1] <= 1.0):
            continue
        r = 7.0
        tint(uv[0] - r / w, uv[1] - 0.6 / h, uv[0] + r / w, uv[1] + 0.6 / h, col, 0.95)
        tint(uv[0] - 0.6 / w, uv[1] - r / h, uv[0] + 0.6 / w, uv[1] + r / h, col, 0.95)


# --------------------------------------------------------------------------------- camera path
def ease_fn(name):
    if name == "linear":
        return lambda u: min(max(u, 0.0), 1.0)
    if name == "smootherstep":
        return lambda u: (lambda x: x * x * x * (x * (x * 6.0 - 15.0) + 10.0))(min(max(u, 0.0), 1.0))
    return lambda u: (lambda x: x * x * (3.0 - 2.0 * x))(min(max(u, 0.0), 1.0))   # StageEase


def ease_name(shot, override):
    if override:
        return override
    e = (shot.get("easing") or "smoothstep").lower()
    if "smootherstep" in e:
        return "smootherstep"
    if "smoothstep" in e or "stageease" in e:
        return "smoothstep"
    if "linear" in e or e.startswith("none"):
        return "linear"
    return "smoothstep"


def cubic_interp(p0, t0, p1, t1, a):
    a2 = a * a
    a3 = a2 * a
    return p0 * (2 * a3 - 3 * a2 + 1) + t0 * (a3 - 2 * a2 + a) + t1 * (a3 - a2) + p1 * (-2 * a3 + 3 * a2)


def scurve_table(a=0.3, d=0.5, n=4001):
    """Normalised distance E(u), u in [0,1], for a velocity profile that rises with smootherstep over the
    first a, cruises, and falls with smootherstep over the last d. Velocity and acceleration are zero at
    both ends and continuous throughout (C2). Peak speed = mean speed / (1 - a/2 - d/2)."""
    us = np.linspace(0.0, 1.0, n)

    def sm(x):
        x = np.clip(x, 0.0, 1.0)
        return x * x * x * (x * (x * 6.0 - 15.0) + 10.0)
    vel = np.ones(n)
    if a > 0:
        vel = np.where(us < a, sm(us / a), vel)
    if d > 0:
        vel = np.where(us > 1.0 - d, np.minimum(vel, sm((1.0 - us) / d)), vel)
    E = np.concatenate([[0.0], np.cumsum((vel[1:] + vel[:-1]) * 0.5 * np.diff(us))])
    return us, E / E[-1]


def catmull_centripetal(P, U, alpha=0.5):
    """Centripetal Catmull-Rom through P (n x 3) at parameter U in [0, n-1]; phantom end points mirror the
    first and last chords (no overshoot at the ends)."""
    n = len(P)
    if n == 1:
        return P[0].copy()
    if n == 2:
        u = min(max(U, 0.0), 1.0)
        return P[0] + (P[1] - P[0]) * u
    ext = [2 * P[0] - P[1]] + list(P) + [2 * P[-1] - P[-2]]
    i = int(min(max(math.floor(U), 0), n - 2))
    u = min(max(U - i, 0.0), 1.0)
    p0, p1, p2, p3 = ext[i], ext[i + 1], ext[i + 2], ext[i + 3]

    def tj(ti, a, b):
        return ti + max(float(np.linalg.norm(b - a)), 1e-6) ** alpha
    t0 = 0.0
    t1 = tj(t0, p0, p1)
    t2 = tj(t1, p1, p2)
    t3 = tj(t2, p2, p3)
    t = t1 + (t2 - t1) * u
    A1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1
    A2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2
    A3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3
    B1 = (t2 - t) / (t2 - t0) * A1 + (t - t0) / (t2 - t0) * A2
    B2 = (t3 - t) / (t3 - t1) * A2 + (t - t1) / (t3 - t1) * A3
    return (t2 - t) / (t2 - t1) * B1 + (t - t1) / (t2 - t1) * B2


class ShotPath:
    def __init__(self, shot, args):
        self.shot = shot
        self.name = str(shot.get("name"))
        keys = shot.get("keys") or []
        if not keys:
            raise ValueError("shot %s has no keys" % self.name)
        self.keys = keys
        self.seconds = float(shot.get("seconds") or 0.0)
        self.static = len(keys) < 2 or self.seconds <= 0.0
        self.P = [np.array(k["pos"], dtype=np.float64) for k in keys]
        self.Lk = [np.array(k["lookAt"], dtype=np.float64) for k in keys]
        self.centripetal = str(shot.get("path") or "").lower().startswith("centrip")
        self.level = bool(shot.get("level"))
        self.aim = np.array(shot["aim"], dtype=np.float64) if shot.get("aim") is not None else None
        if self.static:
            self.kt = [0.0] * len(keys)
        elif self.centripetal:
            self.kt = [i / float(len(keys) - 1) for i in range(len(keys))]
        else:
            self.kt = [float(k.get("t", i * self.seconds / (len(keys) - 1))) / self.seconds for i, k in enumerate(keys)]
        ts = args.tangent_scale if args.tangent_scale is not None else shot.get("tangentScale")
        self.tscale = float(ts) if ts is not None else 0.5
        e = str(shot.get("easing") or "").lower()
        if e.startswith("scurve") and not args.ease:
            self.ease_name = "scurve %.0f/%.0f" % (100 * float(shot.get("accelFrac", 0.3)), 100 * float(shot.get("decelFrac", 0.5)))
            us, E = scurve_table(float(shot.get("accelFrac", 0.3)), float(shot.get("decelFrac", 0.5)))
            self.ease = lambda u, us=us, E=E: float(np.interp(min(max(u, 0.0), 1.0), us, E))
        else:
            self.ease_name = ease_name(shot, args.ease)
            self.ease = ease_fn(self.ease_name)
        tm = args.timing if args.timing != "shot" else (shot.get("timing") or ("arclength" if self.centripetal else "knot"))
        self.timing = "arclength" if str(tm).lower().startswith("arc") else "knot"
        self.length = 0.0
        self.key_s = [0.0] * len(keys)
        if not self.static:
            Ts = np.linspace(0.0, 1.0, 6001)
            pts = np.array([self.spline(T) for T in Ts])
            seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
            self._T = Ts
            self._s = np.concatenate([[0.0], np.cumsum(seg)])
            self.length = float(self._s[-1])
            self.key_s = [self.s_from_T(T) for T in self.kt]
        # composition targets and lens per key, eased by distance progress
        self.screen_keys = None
        if shot.get("screen") is not None or any(k.get("screen") is not None for k in keys):
            base = shot.get("screen") or [0.5, 0.5]
            self.screen_keys = [np.array(k.get("screen") or base, dtype=np.float64) for k in keys]
        self.focal_keys = None
        if any(k.get("focalMm") is not None for k in keys):
            base = float(shot.get("focalMm") or 50.0)
            self.focal_keys = [float(k.get("focalMm") or base) for k in keys]

    def progress(self, t):
        """Fraction of the path length covered at shot time t (0 for static shots)."""
        if self.static or self.length <= 0.0:
            return 0.0
        return self.s_from_T(self.T_at(t)) / self.length

    def _key_interp(self, vals, t):
        if vals is None:
            return None
        if self.static or len(vals) == 1:
            return vals[0]
        p = self.progress(t)
        fr = [s / self.length for s in self.key_s] if self.length > 0 else [0.0] * len(vals)
        if p <= fr[0]:
            return vals[0]
        for i in range(1, len(vals)):
            if p <= fr[i] + 1e-12:
                u = (p - fr[i - 1]) / max(fr[i] - fr[i - 1], 1e-9)
                return vals[i - 1] + (vals[i] - vals[i - 1]) * u
        return vals[-1]

    def screen_at(self, t):
        v = self._key_interp(self.screen_keys, t)
        return None if v is None else (float(v[0]), float(v[1]))

    def focal_at(self, t, default):
        v = self._key_interp(self.focal_keys, t)
        return default if v is None else float(v)

    def orient(self, pos, look):
        d = look - pos
        yaw = math.degrees(math.atan2(d[1], d[0]))
        pitch = 0.0 if self.level else math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
        return yaw, pitch

    # CompanionStageDirector::SampleSpline, with the 0.5 exposed as tscale
    def spline(self, T, look=False):
        V = self.Lk if look else self.P
        if self.centripetal and not self.static:
            if look and all(np.allclose(V[0], q) for q in V):
                return V[0].copy()
            return catmull_centripetal(V, min(max(T, 0.0), 1.0) * (len(V) - 1))
        K = self.kt
        n = len(V)
        if self.static or T <= K[0]:
            return V[0].copy()
        for i in range(1, n):
            if T <= K[i]:
                span = max(K[i] - K[i - 1], KINDA_SMALL)
                alpha = min(max((T - K[i - 1]) / span, 0.0), 1.0)
                A, B = V[i - 1], V[i]
                pi_, ni_ = max(i - 2, 0), min(i + 1, n - 1)
                prev, nxt = V[pi_], V[ni_]
                ss = span if i == 1 else max(K[i] - K[pi_], KINDA_SMALL)
                es = span if i == n - 1 else max(K[ni_] - K[i - 1], KINDA_SMALL)
                st = ((B - A) / span if i == 1 else (B - prev) / ss) * span * self.tscale
                et = ((B - A) / span if i == n - 1 else (nxt - A) / es) * span * self.tscale
                return cubic_interp(A, st, B, et, alpha)
        return V[-1].copy()

    def T_from_s(self, s):
        return float(np.interp(s, self._s, self._T))

    def s_from_T(self, T):
        return float(np.interp(T, self._T, self._s)) if not self.static else 0.0

    def T_at(self, t):
        if self.static:
            return 0.0
        u = self.ease(t / self.seconds)
        if self.timing == "arclength":
            return self.T_from_s(u * self.length)
        return u

    def t_for_T(self, T):
        if self.static:
            return 0.0
        lo, hi = 0.0, self.seconds
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            if self.T_at(mid) < T:
                lo = mid
            else:
                hi = mid
        return 0.5 * (lo + hi)

    def at(self, t):
        T = self.T_at(t)
        return self.spline(T), self.spline(T, True), T


# --------------------------------------------------------------------------- boxes and tests
class Rec:
    """Oriented box of a prop or blockout: yaw-aligned in plan plus a z range (Blender metres)."""

    def __init__(self, name, source, kind, yaw, objs, glb=None, part_of=None, mesh=None, status="ok", census=None):
        self.name, self.source, self.kind, self.yaw, self.objs = name, source, kind, yaw, objs
        self.census = census
        self.glb, self.part_of, self.mesh, self.status = glb, part_of, mesh, status
        phi = yaw2bz(yaw)
        self.ax = np.array([[math.cos(phi), math.sin(phi)], [-math.sin(phi), math.cos(phi)]])  # right, forward
        V = world_verts(objs)
        if len(V) == 0:
            V = np.zeros((1, 3))
        self.vmin, self.vmax = V.min(axis=0), V.max(axis=0)
        proj = V[:, :2] @ self.ax.T
        lo, hi = proj.min(axis=0), proj.max(axis=0)
        self.h = (hi - lo) / 2.0
        self.c = ((hi + lo) / 2.0) @ self.ax
        self.zmin, self.zmax = float(V[:, 2].min()), float(V[:, 2].max())

    def volume_ok(self):
        return self.h[0] > 0.005 and self.h[1] > 0.005 and (self.zmax - self.zmin) > 0.01

    def contains(self, P):
        d = P[:, :2] - self.c
        a = np.abs(d @ self.ax.T)
        depth = np.minimum.reduce([self.h[0] - a[:, 0], self.h[1] - a[:, 1], P[:, 2] - self.zmin, self.zmax - P[:, 2]])
        return depth

    def ue(self):
        cz = (self.zmin + self.zmax) / 2.0
        c = b2ue((self.c[0], self.c[1], cz))
        return {"centre": rl(c),
                "sizeLocal": rl((2 * self.h[1] * 100, 2 * self.h[0] * 100, (self.zmax - self.zmin) * 100)),
                "yaw": round(float(self.yaw or 0.0), 2),
                "aabbMin": rl((self.vmin[1] * 100, self.vmin[0] * 100, self.vmin[2] * 100)),
                "aabbMax": rl((self.vmax[1] * 100, self.vmax[0] * 100, self.vmax[2] * 100))}

    def corners_ue(self):
        out = []
        for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = self.c + sx * self.h[0] * self.ax[0] + sy * self.h[1] * self.ax[1]
            out.append((p[1] * 100.0, p[0] * 100.0))
        return out


def world_verts(objs):
    out = []
    for o in objs:
        if o.type != 'MESH' or o.data is None:
            continue
        me = o.data
        n = len(me.vertices)
        if n == 0:
            continue
        co = np.empty(n * 3, dtype=np.float32)
        me.vertices.foreach_get("co", co)
        co = co.reshape(n, 3).astype(np.float64)
        M = np.array(o.matrix_world, dtype=np.float64)
        out.append(co @ M[:3, :3].T + M[:3, 3])
    return np.concatenate(out) if out else np.zeros((0, 3))


def sat_depth(a, b):
    ha, hb = np.maximum(a.h, 0.005), np.maximum(b.h, 0.005)    # zero-thickness boxes count as 1 cm
    d = b.c - a.c
    best = float("inf")
    for ax in (a.ax[0], a.ax[1], b.ax[0], b.ax[1]):
        ra = ha[0] * abs(a.ax[0] @ ax) + ha[1] * abs(a.ax[1] @ ax)
        rb = hb[0] * abs(b.ax[0] @ ax) + hb[1] * abs(b.ax[1] @ ax)
        ov = ra + rb - abs(d @ ax)
        if ov <= 0.0:
            return 0.0
        best = min(best, ov)
    zo = min(a.zmax, b.zmax) - max(a.zmin, b.zmin)
    if max(a.zmax - a.zmin, b.zmax - b.zmin) < 0.01:
        zo = max(zo, 0.01) if zo > -0.005 else zo
    if zo <= 0.0:
        return 0.0
    return min(best, zo)


def same_machine(a, b):
    """Parts of one machine (e.g. a 729 body and its reels): same GLB and yaw, one centre inside the other."""
    if a.part_of == b.name or b.part_of == a.name or (a.part_of and a.part_of == b.part_of):
        return True
    if a.source == b.source == "prop" and a.glb and a.glb == b.glb and abs(wrap180((a.yaw or 0) - (b.yaw or 0))) < 0.5 \
            and a.mesh != b.mesh:
        for p, q in ((a, b), (b, a)):
            cz = np.array([[q.c[0], q.c[1], (q.zmin + q.zmax) / 2.0]])
            d = p.contains(cz)[0] if p.volume_ok() else -1
            if d > -0.02:
                return True
    return False


def find_overlaps(recs, tol_cm):
    out = []
    for i in range(len(recs)):
        a = recs[i]
        for j in range(i + 1, len(recs)):
            b = recs[j]
            # Room shell against room shell is skipped, but wall-mounted dressing (census "signage": the letters,
            # the roundels, the clock) is checked against everything: skipping it let a roundel sit on the LONGVIEW
            # letters. stage_layout.py --check makes the same check without Blender.
            if a.kind in SHELL_KINDS and b.kind in SHELL_KINDS and a.census != "signage" and b.census != "signage":
                continue
            if (a.kind in FLOOR_KINDS) or (b.kind in FLOOR_KINDS):
                continue
            if same_machine(a, b):
                continue
            dep = sat_depth(a, b) * 100.0
            if dep > tol_cm:
                out.append({"a": a.name, "b": b.name, "depthCm": round(dep, 1),
                            "zOverlapCm": round((min(a.zmax, b.zmax) - max(a.zmin, b.zmin)) * 100.0, 1),
                            "kinds": [a.kind, b.kind]})
    out.sort(key=lambda r: -r["depthCm"])
    return out


def build_bvh(recs, exclude_kinds=()):
    V, F, owner, off = [], [], [], 0
    for r in recs:
        if r.kind in exclude_kinds:
            continue
        for o in r.objs:
            if o.type != 'MESH' or o.data is None or len(o.data.vertices) == 0:
                continue
            me = o.data
            n = len(me.vertices)
            co = np.empty(n * 3, dtype=np.float32)
            me.vertices.foreach_get("co", co)
            M = np.array(o.matrix_world, dtype=np.float64)
            w = co.reshape(n, 3).astype(np.float64) @ M[:3, :3].T + M[:3, 3]
            me.calc_loop_triangles()
            nt = len(me.loop_triangles)
            if nt == 0:
                continue
            tri = np.empty(nt * 3, dtype=np.int32)
            me.loop_triangles.foreach_get("vertices", tri)
            V.append(w)
            F.append(tri.reshape(nt, 3).astype(np.int64) + off)
            owner.extend([r.name] * nt)
            off += n
    if not V:
        return None, []
    return BVHTree.FromPolygons(np.concatenate(V).tolist(), np.concatenate(F).tolist(), all_triangles=True), owner


# ----------------------------------------------------------------------------- shot analysis
def capsule_dist(p, mark_b, h_m, r_m=0.22):
    dh = math.hypot(p[0] - mark_b[0], p[1] - mark_b[1])
    z = p[2] - mark_b[2]
    dz = 0.0 if 0.0 <= z <= h_m else (z - h_m if z > h_m else -z)
    return math.hypot(max(dh - r_m, 0.0), dz)


def clearance(pb, bvh_c, owner_c, mark_b, fig_h):
    best, who = float("inf"), None
    if bvh_c is not None:
        loc, nrm, idx, dist = bvh_c.find_nearest(Vector(pb))
        if loc is not None:
            best, who = dist, owner_c[idx]
    dfig = capsule_dist(pb, mark_b, fig_h)
    if dfig < best:
        best, who = dfig, "resident (stand-in)"
    return best, who


def analyse_shot(sp, hz, recs, bvh_a, owner_a, bvh_c, owner_c, mark_b, fig_h, warn_cm, eye_ue=None):
    if sp.static:
        ts = np.array([0.0])
    else:
        ts = np.linspace(0.0, sp.seconds, max(2, int(round(sp.seconds * hz)) + 1))
    rows = [sp.at(float(t)) for t in ts]
    P = np.array([r[0] for r in rows])          # UE cm
    Lk = np.array([r[1] for r in rows])
    Tn = np.array([r[2] for r in rows])
    D = Lk - P
    if sp.level:
        D = D.copy()
        D[:, 2] = 0.0
    aim = np.linalg.norm(Lk - P, axis=1)
    U = D / np.maximum(np.linalg.norm(D, axis=1), 1e-9)[:, None]
    yaw = np.degrees(np.arctan2(D[:, 1], D[:, 0]))
    pitch = np.degrees(np.arctan2(D[:, 2], np.hypot(D[:, 0], D[:, 1])))
    lens0 = lens_for(sp.shot)[0]
    focal = np.array([sp.focal_at(float(t), lens0) for t in ts])
    eye = np.array(eye_ue if eye_ue is not None else (sp.aim if sp.aim is not None else Lk[-1]), dtype=np.float64)
    deye = np.linalg.norm(P - eye, axis=1)
    n = len(ts)
    res = {"name": sp.name, "static": sp.static, "seconds": sp.seconds, "timing": sp.timing, "ease": sp.ease_name,
           "tangentScale": sp.tscale, "keys": len(sp.P), "lengthM": round(sp.length / 100.0, 2)}
    if n > 1:
        dt = ts[1] - ts[0]
        step = np.linalg.norm(np.diff(P, axis=0), axis=1)
        v = np.zeros(n)
        v[1:-1] = np.linalg.norm(P[2:] - P[:-2], axis=1) / (2 * dt)
        v[0], v[-1] = step[0] / dt, step[-1] / dt
        acc = np.zeros(n)
        acc[1:-1] = np.linalg.norm(P[2:] - 2 * P[1:-1] + P[:-2], axis=1) / dt ** 2
        cosang = np.clip(np.sum(U[1:] * U[:-1], axis=1), -1.0, 1.0)
        turn = np.zeros(n)
        turn[1:] = np.degrees(np.arccos(cosang)) / dt
        dyaw = np.array([wrap180(a) for a in np.diff(yaw)])
        yawrate = np.zeros(n)
        yawrate[1:] = np.abs(dyaw) / dt
        res.update({
            "pathLengthM": round(float(step.sum()) / 100.0, 2),
            "speedMs": {"min": round(float(v.min()) / 100, 2), "max": round(float(v.max()) / 100, 2),
                        "mean": round(float(step.sum() / sp.seconds) / 100, 2),
                        "maxAt": {"t": round(float(ts[v.argmax()]), 3), "pos": rl(P[v.argmax()])}},
            "peakAccelMs2": {"value": round(float(acc.max()) / 100, 2), "t": round(float(ts[acc.argmax()]), 3)},
            "peakTurnDegS": {"value": round(float(turn.max()), 1), "t": round(float(ts[turn.argmax()]), 3)},
            "peakYawRateDegS": {"value": round(float(yawrate.max()), 1), "t": round(float(ts[yawrate.argmax()]), 3)},
            "totalYawDeg": round(float(np.abs(dyaw).sum()), 1),
        })
    else:
        v = acc = turn = yawrate = np.zeros(1)
        res.update({"pathLengthM": 0.0, "speedMs": {"min": 0, "max": 0, "mean": 0}, "peakAccelMs2": {"value": 0, "t": 0},
                    "peakTurnDegS": {"value": 0, "t": 0}, "peakYawRateDegS": {"value": 0, "t": 0}, "totalYawDeg": 0})
    res["closestAimM"] = {"value": round(float(aim.min()) / 100, 2), "t": round(float(ts[aim.argmin()]), 3)}
    res["pitchDeg"] = {"min": round(float(pitch.min()), 1), "max": round(float(pitch.max()), 1),
                       "minAt": round(float(ts[pitch.argmin()]), 3)}
    # --- camera inside a box
    PB = np.array([[p[1] / 100.0, p[0] / 100.0, p[2] / 100.0] for p in P])
    inside = []
    for r in recs:
        if not r.volume_ok():
            continue
        dep = r.contains(PB)
        m = dep > 0.0
        if not m.any():
            continue
        idx = np.flatnonzero(m)
        runs = np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1)
        for run in runs:
            inside.append({"object": r.name, "kind": r.kind, "t0": round(float(ts[run[0]]), 3), "t1": round(float(ts[run[-1]]), 3),
                           "maxDepthCm": round(float(dep[run].max()) * 100, 1),
                           "pos": rl(P[run[len(run) // 2]])})
    res["inside"] = sorted(inside, key=lambda e: e["t0"])
    # --- the path passing through a surface
    cross = []
    if bvh_a is not None and n > 1:
        for j in range(n - 1):
            p0, p1 = Vector(PB[j]), Vector(PB[j + 1])
            d = p1 - p0
            L = d.length
            if L < 1e-9:
                continue
            d.normalize()
            o, remaining = p0, L
            for _ in range(4):
                loc, nrm, idx, dist = bvh_a.ray_cast(o, d, remaining)
                if loc is None:
                    break
                frac = (L - remaining + dist) / L
                t = float(ts[j] + frac * (ts[j + 1] - ts[j]))
                name = owner_a[idx]
                if not (cross and cross[-1]["object"] == name and t - cross[-1]["t"] < 0.25):
                    cross.append({"object": name, "t": round(t, 3), "pos": rl(b2ue(loc))})
                o = loc + d * 1e-4
                remaining -= dist + 1e-4
                if remaining <= 0:
                    break
    res["passesThrough"] = cross
    # --- clearance
    clr = np.zeros(n)
    near = [None] * n
    for j in range(n):
        clr[j], near[j] = clearance(PB[j], bvh_c, owner_c, mark_b, fig_h)
    jm = int(clr.argmin())
    res["minClearance"] = {"cm": round(float(clr[jm]) * 100, 1), "object": near[jm], "t": round(float(ts[jm]), 3),
                           "pos": rl(P[jm])}
    per = {}
    for j in range(n):
        if clr[j] * 100 < warn_cm:
            k = near[j]
            if k not in per or clr[j] < per[k]["cm"] / 100.0:
                per[k] = {"object": k, "cm": round(float(clr[j]) * 100, 1), "t": round(float(ts[j]), 3),
                          "pos": rl(P[j]), "insideNearPlane": bool(clr[j] * 100 < NEAR_PLANE_CM)}
    res["nearMisses"] = sorted(per.values(), key=lambda e: e["cm"])
    # ---- comfort budget (the stage design's camera-comfort limits), only meaningful for moving shots
    comfort = {}
    if n > 1:
        dt = ts[1] - ts[0]
        moving = v > 5.0                                  # cm/s
        vz = np.zeros(n)
        vz[1:-1] = np.abs(P[2:, 2] - P[:-2, 2]) / (2 * dt)
        hfov = np.degrees(2.0 * np.arctan(18.0 / focal))   # 36 mm-wide 16:9 filmback
        ratio = yawrate / np.maximum(hfov / 7.0, 1e-6)
        dd = np.gradient(deye, dt)
        approach = -dd
        tau = np.where(approach > 2.0, deye / np.maximum(approach, 1e-6), np.inf)
        growth = np.gradient(np.log(focal / np.maximum(deye, 1.0)), dt)
        zoom = np.gradient(np.log(focal), dt)
        jerk = np.zeros(n)
        jerk[2:-2] = np.linalg.norm(P[4:] - 2 * P[3:-1] + 2 * P[1:-3] - P[:-4], axis=1) / (2 * dt ** 3)
        mv = moving if moving.any() else np.ones(n, bool)
        comfort = {
            "peakSpeedMs": round(float(v.max()) / 100, 3),
            "peakVerticalSpeedMs": round(float(vz.max()) / 100, 3),
            "peakAccelMs2": round(float(acc[2:-2].max()) / 100, 3) if n > 5 else 0.0,
            "peakJerkMs3": round(float(jerk.max()) / 100, 2),
            "peakYawRateDegS": round(float(yawrate.max()), 2),
            "yawRateBudgetDegS": round(float((hfov / 7.0).min()), 2),
            "peakYawRateOverBudget": round(float(ratio.max()), 2),
            "totalYawDeg": round(float(np.abs(np.array([wrap180(a) for a in np.diff(yaw)])).sum()), 1),
            "pitchAbsMaxDeg": round(float(np.abs(pitch).max()), 2),
            "minClearanceMovingCm": round(float(clr[mv].min()) * 100, 1),
            "minClearanceRestCm": round(float(clr[-1]) * 100, 1),
            "minEyeDistMovingCm": round(float(deye[v > 10.0].min()), 1) if (v > 10.0).any() else None,
            "minEyeDistCm": round(float(deye.min()), 1),
            "minTimeToContactS": round(float(tau.min()), 2) if np.isfinite(tau).any() else None,
            "peakGrowthPctS": round(float(growth.max()) * 100, 1),
            "peakZoomPctS": round(float(np.abs(zoom).max()) * 100, 1),
            "heightRangeCm": [round(float(P[:, 2].min()), 1), round(float(P[:, 2].max()), 1)],
            "heightMonotonic": bool(np.all(np.diff(P[:, 2]) <= 0.05) or np.all(np.diff(P[:, 2]) >= -0.05)),
            "focalRangeMm": [round(float(focal.min()), 1), round(float(focal.max()), 1)],
        }
        checks = [
            ("speed <= 1.0 m/s (target 0.9)", comfort["peakSpeedMs"] <= 1.0),
            ("acceleration <= 1.0 m/s2", comfort["peakAccelMs2"] <= 1.0),
            ("yaw rate <= HFOV/7 per s", comfort["peakYawRateOverBudget"] <= 1.0),
            ("total yaw <= 30 deg", comfort["totalYawDeg"] <= 30.0),
            ("pitch 0 +/- 1 deg", comfort["pitchAbsMaxDeg"] <= 1.0),
            ("clearance >= 80 cm while moving", comfort["minClearanceMovingCm"] >= 80.0),
            ("clearance >= 40 cm at rest", comfort["minClearanceRestCm"] >= 40.0),
            ("eyes >= 150 cm while moving (hard cap)", (comfort["minEyeDistMovingCm"] or 1e9) >= 150.0),
            ("time to contact >= 4 s", (comfort["minTimeToContactS"] or 1e9) >= 4.0),
            ("subject growth <= 25 %/s", comfort["peakGrowthPctS"] <= 25.0),
            ("zoom <= 10 %/s", comfort["peakZoomPctS"] <= 10.0),
            ("height monotonic", comfort["heightMonotonic"]),
        ]
        comfort["checks"] = [{"rule": r, "pass": bool(ok)} for r, ok in checks]
    res["comfort"] = comfort
    arrays = {"t": ts, "T": Tn, "P": P, "L": Lk, "v": v, "acc": acc, "turn": turn, "yawrate": yawrate, "yaw": yaw,
              "pitch": pitch, "aim": aim, "clr": clr, "near": near, "deye": deye, "focal": focal}
    return res, arrays


def frame_points(sp, n, mode):
    if sp.static:
        return [(0.0, 0.0)]
    out = []
    for i in range(n):
        u = i / (n - 1)
        if mode == "arclength":
            T = sp.T_from_s(u * sp.length)
            t = sp.t_for_T(T)
        elif mode == "param":
            T = u
            t = sp.t_for_T(T)
        else:
            t = u * sp.seconds
            T = sp.T_at(t)
        out.append((t, T))
    return out


SENSOR_H = 20.25     # vertical filmback (mm) for the 36 mm-wide 16:9 DSLR back; held for any aspect


def cam_basis(yaw, pitch):
    y, p = math.radians(yaw), math.radians(pitch)
    F = np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p)])
    R = np.array([-math.sin(y), math.cos(y), 0.0])
    Up = np.array([-math.sin(p) * math.cos(y), -math.sin(p) * math.sin(y), math.cos(p)])
    return F, R, Up


def project_mm(pos, yaw, pitch, f, X):
    """Image-plane coordinates (mm, +right, +up) and depth (cm) of UE point X for a camera at pos."""
    F, R, Up = cam_basis(yaw, pitch)
    D = np.asarray(X, dtype=np.float64) - np.asarray(pos, dtype=np.float64)
    depth = float(D @ F)
    if depth <= 1e-6:
        return None
    return f * float(D @ R) / depth, f * float(D @ Up) / depth, depth


def solve_shift(sp, t, pos, look, f, W, H):
    """Sensor offset (mm, Blender sign: +x window right, +y window up) that puts the shot's aim point on its
    screen target; a fixed shot.shiftMm otherwise."""
    scr = sp.screen_at(t)
    if scr is None or sp.aim is None:
        s = sp.shot.get("shiftMm")
        return (float(s[0]), float(s[1])) if s else (0.0, 0.0)
    yaw, pitch = sp.orient(pos, look)
    pr = project_mm(pos, yaw, pitch, f, sp.aim)
    if pr is None:
        return (0.0, 0.0)
    X, Y, _ = pr
    sw_eff = SENSOR_H * W / H
    return (X - (scr[0] - 0.5) * sw_eff, Y - (0.5 - scr[1]) * SENSOR_H)


def screen_uv(pos, yaw, pitch, f, shift, X, W, H):
    pr = project_mm(pos, yaw, pitch, f, X)
    if pr is None:
        return None
    sw_eff = SENSOR_H * W / H
    return (0.5 + (pr[0] - shift[0]) / sw_eff, 0.5 - (pr[1] - shift[1]) / SENSOR_H)


# ------------------------------------------------------------------------------- frame census
CENSUS_BK = {}       # blockout name -> layout dict
CENSUS_PROP = {}     # prop name -> layout dict


def _mat_emissive(m):
    if m is None or m.node_tree is None:
        return False
    b = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if b is None:
        return False
    es, ec = b.inputs.get("Emission Strength"), b.inputs.get("Emission Color")
    return bool(es is not None and ec is not None and es.default_value > 1e-4 and max(ec.default_value[:3]) > 1e-4)


def census_category(ob, face_index):
    """(category, emissive) for what a ray hit."""
    name = ob.name
    emis = False
    try:
        mi = ob.data.polygons[face_index].material_index if face_index is not None and face_index >= 0 else 0
        m = ob.material_slots[mi].material if len(ob.material_slots) > mi else None
        emis = _mat_emissive(m)
    except Exception:
        emis = False
    if name.startswith("resident|"):
        return "resident", False
    if name.startswith("bk|"):
        parts = name.split("|")
        b = CENSUS_BK.get(parts[1]) or {}
        if len(parts) > 2 and parts[2] == "text":
            return (b.get("census") or "signage"), emis
        kind = (b.get("kind") or "").lower()
        cat = b.get("census")
        if not cat:
            cat = {"floor": "floor", "ceiling": "ceiling", "troffer": "ceiling", "wall": "wall", "facade": "facade",
                   "mullion": "facade", "glass": "glass"}.get(kind, "furniture")
        return cat, emis
    if name.startswith("prop|"):
        p = CENSUS_PROP.get(name.split("|")[1]) or {}
        return (p.get("census") or "machine"), emis
    return "other", emis


def frame_census(sc, cam, eyes, crown_v, chin_v, W, H, portrait=False, nx=128, ny=72):
    """Ray-cast census of the frame as rendered (camera already posed, lens and shift applied, figure posed)."""
    bpy.context.view_layer.update()
    TR, BR, BL, TL = [Vector(v) for v in cam.data.view_frame(scene=sc)]
    Mw = cam.matrix_world
    R3 = Mw.to_3x3()
    O = Mw.translation.copy()
    state = {"dg": bpy.context.evaluated_depsgraph_get()}

    def cast(u, v):
        p = TL + (TR - TL) * u + (BL - TL) * v
        d = (R3 @ p).normalized()
        o = O.copy()
        travelled = 0.0
        for _ in range(8):
            ok, loc, nrm, idx, ob, mtx = sc.ray_cast(state["dg"], o, d, distance=400.0)
            if not ok:
                return "void", False, None, None
            travelled += (loc - o).length
            cat, emis = census_category(ob, idx)
            if cat == "glass":
                o = loc + d * 0.003
                continue
            return cat, emis, travelled * 100.0, ob.name
        return "void", False, None, None

    def in_ui(u, v):
        return (v > 0.70) if portrait else (u < 0.40 and v > 0.52) or (u < 0.55 and v > 0.88)

    tot, right, nr = {}, {}, 0
    for j in range(ny):
        v = (j + 0.5) / ny
        for i in range(nx):
            u = (i + 0.5) / nx
            cat, _, _, _ = cast(u, v)
            tot[cat] = tot.get(cat, 0) + 1
            zone = (v < 0.70) if portrait else (u > 0.40)
            if zone:
                right[cat] = right.get(cat, 0) + 1
                nr += 1
    n = float(nx * ny)
    out = {"grid": [nx, ny],
           "shares": {k: round(c / n, 4) for k, c in sorted(tot.items(), key=lambda kv: -kv[1])},
           "zone": "above the portrait UI band (v < 0.70)" if portrait else "right of the captions (u > 0.40)",
           "sharesInZone": {k: round(c / max(nr, 1), 4) for k, c in sorted(right.items(), key=lambda kv: -kv[1])}}
    out["wallOnlyInZone"] = out["sharesInZone"].get("wall", 0.0)
    if eyes is None or crown_v is None or chin_v is None:
        return out
    eu, ev = eyes
    face = abs(chin_v - crown_v)
    hw = max(0.5 * face * (H / float(W)) * 0.78, 0.008)
    # emissive within +-0.10 W/H of the eyes, as the viewer sees it (figure visible), then with the figure hidden
    def emis_scan():
        hits = {}
        for jj in range(21):
            vv = ev - 0.10 + 0.20 * jj / 20.0
            for ii in range(21):
                uu = eu - 0.10 + 0.20 * ii / 20.0
                if not (0.0 <= uu <= 1.0 and 0.0 <= vv <= 1.0):
                    continue
                cat, emis, dist, obn = cast(uu, vv)
                if emis:
                    key = (obn or cat).split("|")[1] if obn and "|" in obn else cat
                    hits[key] = hits.get(key, 0) + 1
        return hits
    out["emissiveNearEyes"] = emis_scan()
    figs = [o for o in bpy.data.objects if o.name.startswith("resident|")]
    for o in figs:
        o.hide_viewport = True
    bpy.context.view_layer.update()
    state["dg"] = bpy.context.evaluated_depsgraph_get()
    try:
        out["emissiveNearEyesFigureHidden"] = emis_scan()
        behind, dists = {}, []
        for jj in range(9):
            vv = crown_v + face * (jj + 0.5) / 9.0
            for ii in range(7):
                uu = eu - hw + 2 * hw * (ii + 0.5) / 7.0
                cat, emis, dist, obn = cast(uu, vv)
                behind[cat] = behind.get(cat, 0) + 1
                if dist is not None:
                    dists.append(dist)
        out["behindHead"] = {k: round(c / 63.0, 3) for k, c in sorted(behind.items(), key=lambda kv: -kv[1])}
        out["behindHeadNearestCm"] = round(min(dists), 1) if dists else None
        # object edges within 0.03 W of the head silhouette: neighbouring samples in the two side bands that hit
        # different objects at depths more than 20 cm apart
        edges = 0
        where = []
        for side in (-1, 1):
            cols = [eu + side * (hw + 0.03 * (k + 0.5) / 4.0) for k in range(4)]
            grid = []
            for jj in range(12):
                vv = crown_v + face * (jj + 0.5) / 12.0
                grid.append([cast(uu, vv) for uu in cols])
            for jj in range(12):
                for k in range(4):
                    a = grid[jj][k]
                    for b in ([grid[jj][k + 1]] if k < 3 else []) + ([grid[jj + 1][k]] if jj < 11 else []):
                        if a[3] != b[3] and a[2] is not None and b[2] is not None and abs(a[2] - b[2]) > 20.0:
                            edges += 1
                            where.append("%s/%s" % ((a[3] or "void").split("|")[1] if a[3] and "|" in a[3] else a[0],
                                                    (b[3] or "void").split("|")[1] if b[3] and "|" in b[3] else b[0]))
        out["edgesNearHead"] = edges
        out["edgesNearHeadWhere"] = sorted(set(where))[:6]
    finally:
        for o in figs:
            o.hide_viewport = False
        bpy.context.view_layer.update()
    return out


def near_white_share(img, zone_u=0.40, thr=0.8):
    a = img[..., :3]
    luma = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    h, w = luma.shape
    return round(float((luma > thr).mean()), 4), round(float((luma[:, int(zone_u * w):] > thr).mean()), 4)


# The value-structure gate (set-design blocker 3): in these shots the luminous ceiling must read at least as bright
# as the floor it lights. Measured on the rendered frame, at ray-cast samples that hit a ceiling panel ("lum_*") or
# the floor ("floor_tile"); the samples are written to the report so the same pixels can be read in an Unreal capture
# of the same camera (Deployment/blender has no Unreal; the capture tools take valueSamples from report.json).
VALUE_GATE_SHOTS = ("VITRINE", "CUTAWAY_HUB")


def value_structure(sc, cam, img, nx=96, ny=54):
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    TR, BR, BL, TL = [Vector(v) for v in cam.data.view_frame(scene=sc)]
    Mw = cam.matrix_world
    R3, O = Mw.to_3x3(), Mw.translation.copy()
    h, w = img.shape[:2]
    luma = 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]
    samples = {"ceiling": [], "floor": []}
    for j in range(ny):
        v = (j + 0.5) / ny
        for i in range(nx):
            u = (i + 0.5) / nx
            d = (R3 @ (TL + (TR - TL) * u + (BL - TL) * v)).normalized()
            o = O.copy()
            for _ in range(6):
                ok, loc, nrm, idx, ob, mtx = sc.ray_cast(dg, o, d, distance=400.0)
                if not ok:
                    break
                n = ob.name.split("|")
                b = CENSUS_BK.get(n[1]) if n[0] == "bk" and len(n) > 1 else None
                if b is not None and ((b.get("kind") or "") == "glass" or "opacity" in b):
                    o = loc + d * 0.003
                    continue
                if b is not None and n[1].startswith("lum_"):
                    samples["ceiling"].append((round(u, 4), round(v, 4)))
                elif b is not None and n[1] == "floor_tile":
                    samples["floor"].append((round(u, 4), round(v, 4)))
                break
    out = {"grid": [nx, ny]}
    for k, pts in samples.items():
        vals = [float(luma[min(h - 1, int(v * h)), min(w - 1, int(u * w))]) for u, v in pts]
        out[k] = {"n": len(pts), "meanDisplay": round(sum(vals) / len(vals), 4) if vals else None,
                  "medianDisplay": round(float(np.median(vals)), 4) if vals else None}
        out[k + "Samples"] = pts
    c, f = out["ceiling"]["medianDisplay"], out["floor"]["medianDisplay"]
    out["pass"] = None if c is None or f is None else bool(c >= f)
    return out


def census_text(cz):
    if not cz:
        return ""
    em = sum(cz.get("emissiveNearEyes", {}).values()) if "emissiveNearEyes" in cz else None
    bits = ["census: wall-only %s %.1f%%" % ("above UI" if "portrait" in cz.get("zone", "") else "right of captions",
                                            100.0 * cz.get("wallOnlyInZone", 0.0))]
    if em is not None:
        bits.append("emissive near eyes %d%s" % (em, (" (" + ", ".join("%s %d" % kv for kv in cz["emissiveNearEyes"].items()) + ")") if em else ""))
        bits.append("edges near head %d" % cz.get("edgesNearHead", 0))
        if cz.get("behindHeadNearestCm") is not None:
            bits.append("behind head %s %.1f m" % (next(iter(cz.get("behindHead", {"?": 0}))), cz["behindHeadNearestCm"] / 100.0))
    if cz.get("nearWhite") is not None:
        bits.append("near-white %.0f%%" % (100.0 * cz["nearWhite"]))
    return " | ".join(bits)


def frame_info(sp, t, T, recs, bvh_c, owner_c, mark_b, fig_h, facing, W=1280, H=720, mark=(0, 0, 0), head=(0.0, 0.0)):
    pos, look = sp.spline(T), sp.spline(T, True)
    d = look - pos
    yaw, pitch = sp.orient(pos, look)
    f = sp.focal_at(t, lens_for(sp.shot)[0])
    shift = solve_shift(sp, t, pos, look, f, W, H)
    sw_eff = SENSOR_H * W / H
    info = {"t": round(t, 3), "T": round(T, 4), "sM": round(sp.s_from_T(T) / 100.0, 2),
            "pos": rl(pos), "lookAt": rl(look),
            "yaw": round(yaw, 1), "pitch": round(pitch, 1), "aimM": round(float(np.linalg.norm(d)) / 100.0, 2),
            "facingYaw": round(facing, 1), "head": [round(head[0], 1), round(head[1], 1)],
            "focalMm": round(f, 2), "shiftMm": [round(shift[0], 3), round(shift[1], 3)], "res": [W, H],
            "edgeYaw": [round(yaw + math.degrees(math.atan((shift[0] - sw_eff / 2.0) / f)), 2),
                        round(yaw + math.degrees(math.atan((shift[0] + sw_eff / 2.0) / f)), 2)]}
    k = fig_h / 1.70
    eye = figure_point(mark, facing, k, EYE_LOCAL, head)
    crown = figure_point(mark, facing, k, CROWN_LOCAL, head)
    chin = figure_point(mark, facing, k, CHIN_LOCAL, head)
    ue, uc, uh = (screen_uv(pos, yaw, pitch, f, shift, q, W, H) for q in (eye, crown, chin))
    if ue is not None:
        info["eyesUV"] = [round(ue[0], 3), round(ue[1], 3)]
        info["eyeDistM"] = round(float(np.linalg.norm(np.array(eye) - pos)) / 100.0, 2)
    if uc is not None and uh is not None:
        info["headroom"] = round(uc[1], 3)
        info["facePx"] = int(round((uh[1] - uc[1]) * H))
    scr = sp.screen_at(t)
    if scr is not None:
        info["screenTarget"] = [round(scr[0], 3), round(scr[1], 3)]
    if sp.static:
        info.update({"speedMs": 0.0, "turnDegS": 0.0})
    else:
        h = 1.0 / 240.0
        t0, t1 = max(t - h, 0.0), min(t + h, sp.seconds)
        p0, l0, _ = sp.at(t0)
        p1, l1, _ = sp.at(t1)
        info["speedMs"] = round(float(np.linalg.norm(p1 - p0)) / max(t1 - t0, 1e-9) / 100.0, 2)
        u0, u1 = (l0 - p0), (l1 - p1)
        if sp.level:
            u0, u1 = u0 * np.array([1.0, 1.0, 0.0]), u1 * np.array([1.0, 1.0, 0.0])
        c = float(np.dot(u0, u1) / max(np.linalg.norm(u0) * np.linalg.norm(u1), 1e-12))
        info["turnDegS"] = round(math.degrees(math.acos(min(max(c, -1.0), 1.0))) / max(t1 - t0, 1e-9), 1)
    pb = ue2b(pos)
    cl, who = clearance(pb, bvh_c, owner_c, mark_b, fig_h)
    info["clearanceCm"] = round(cl * 100, 1)
    info["nearest"] = who
    PB = np.array([[pb.x, pb.y, pb.z]])
    info["insideOf"] = [r.name for r in recs if r.volume_ok() and r.contains(PB)[0] > 0.0]
    return info


# ------------------------------------------------------------------------------------- plan
class Plan:
    """Overlay geometry for top.png. Plan coords are UE cm: screen right = +X, screen down = +Y.
    Labels are placed greedily so they do not cover each other or the markers registered first."""

    def __init__(self, coll, ppm):
        self.coll = coll
        self.ppm = ppm
        self.n = 0
        self.white = flat_mat("plate", "#ffffff")
        self.rects = []

    def m(self, px):
        return px / self.ppm

    def cm(self, px):
        return px / self.ppm * 100.0

    def _obj(self, me, mat):
        self.n += 1
        o = bpy.data.objects.new("plan|%04d" % self.n, me)
        self.coll.objects.link(o)
        if mat is not None:
            o.active_material = mat
        return o

    def _mesh(self, bm):
        me = bpy.data.meshes.new("pv_plan")
        bm.to_mesh(me)
        bm.free()
        return me

    def block(self, x, y, r_px):
        r = self.cm(r_px)
        self.rects.append((x - r, y - r, x + r, y + r))

    def free(self, rect):
        return not any(rect[0] < q[2] and q[0] < rect[2] and rect[1] < q[3] and q[1] < rect[3] for q in self.rects)

    def disc(self, x, y, r_px, mat, dz=0.0, segs=24):
        bm = bmesh.new()
        bmesh.ops.create_circle(bm, cap_ends=True, segments=segs, radius=self.m(r_px),
                                matrix=Matrix.Translation((y / 100.0, x / 100.0, PLAN_Z + dz)))
        return self._obj(self._mesh(bm), mat)

    def poly(self, pts, mat, dz=0.0):
        bm = bmesh.new()
        f = bm.faces.new([bm.verts.new((y / 100.0, x / 100.0, PLAN_Z + dz)) for x, y in pts])
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
        return self._obj(self._mesh(bm), mat)

    def line(self, pts, w_px, mat, dz=0.0, closed=False, joints=True):
        if len(pts) < 2:
            return None
        bm = bmesh.new()
        w = self.cm(w_px)
        seq = list(pts) + ([pts[0]] if closed else [])
        for (x0, y0), (x1, y1) in zip(seq[:-1], seq[1:]):
            dx, dy = x1 - x0, y1 - y0
            L = math.hypot(dx, dy)
            if L < 1e-6:
                continue
            nx, ny = -dy / L * w / 2.0, dx / L * w / 2.0
            q = [(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)]
            f = bm.faces.new([bm.verts.new((qy / 100.0, qx / 100.0, PLAN_Z + dz)) for qx, qy in q])
            f.normal_update()
            if f.normal.z < 0:
                f.normal_flip()
        if joints:
            for (x, y) in seq[1:-1] if not closed else seq:
                bmesh.ops.create_circle(bm, cap_ends=True, segments=10, radius=w / 200.0,
                                        matrix=Matrix.Translation((y / 100.0, x / 100.0, PLAN_Z + dz)))
        return self._obj(self._mesh(bm), mat)

    def _rect(self, s, x, y, px, align):
        size = self.cm(px) / 0.72
        w = (len(s) * 0.5 + 0.6) * size
        h = 1.2 * size
        cx = x + {"LEFT": w / 2.0 - 0.3 * size, "CENTER": 0.0, "RIGHT": -w / 2.0 + 0.3 * size}[align]
        return (cx - w / 2.0, y - h / 2.0 - 0.1 * size, cx + w / 2.0, y + h / 2.0 - 0.1 * size)

    def text(self, s, x, y, px, mat, align="CENTER", plate=True, dz=0.3, avoid=False, leader=None):
        """avoid: try nearby rows/columns until the label's box is free; leader: material for a line
        back to (x, y) when the label had to move."""
        x0_, y0_ = x, y
        if avoid:
            h = self.cm(px) / 0.72 * 1.25
            w = (len(s) * 0.5 + 0.6) * self.cm(px) / 0.72
            cands = [(0, 0)] + [(0, k * h) for k in (1, -1, 2, -2, 3, -3, 4, -4)] + \
                    [(sx * w * 0.6, k * h) for sx in (1, -1) for k in (0, 1, -1, 2, -2)]
            for dx, dy in cands:
                if self.free(self._rect(s, x + dx, y + dy, px, align)):
                    x, y = x + dx, y + dy
                    break
        self.rects.append(self._rect(s, x, y, px, align))
        size = self.m(px) / 0.72          # px = cap height
        cu = bpy.data.curves.new("pv_txt", 'FONT')
        cu.body = s
        cu.size = size
        cu.align_x = align
        cu.align_y = 'CENTER'
        o = self._obj(cu, mat)
        o.location = (y / 100.0, x / 100.0, PLAN_Z + dz)
        o.rotation_euler = (0.0, 0.0, math.pi / 2.0)
        if plate:
            r = self._rect(s, x, y, px, align)
            self.poly([(r[0], r[1]), (r[2], r[1]), (r[2], r[3]), (r[0], r[3])], self.white, dz=dz - 0.05)
        if leader is not None and (abs(x - x0_) > 1 or abs(y - y0_) > 1):
            self.line([(x0_, y0_), (x, y)], 1.0, leader, dz=dz - 0.1, joints=False)
        return o


WHIP_DEGS = 90.0      # view turning faster than this reads as a whip pan
FAST_MS = 6.0         # camera faster than this through an interior
DOWN_DEG = 30.0       # aimed further down than this
CLOSE_AIM_M = 1.5     # look target nearer than this: the aim swings fast as the camera passes it
PLAN_OVERLAPS = 14    # overlaps tagged on top.png (all are in the report)


def frame_flags(fr, warn_cm):
    f = []
    if fr.get("insideOf"):
        f.append("INSIDE " + ", ".join(fr["insideOf"]))
    if fr["clearanceCm"] < NEAR_PLANE_CM:
        f.append("%.0f cm from %s (inside 10 cm near plane)" % (fr["clearanceCm"], fr["nearest"]))
    elif fr["clearanceCm"] < warn_cm:
        f.append("%.0f cm from %s" % (fr["clearanceCm"], fr["nearest"]))
    if fr.get("turnDegS", 0.0) >= WHIP_DEGS:
        f.append("WHIP %.0f deg/s" % fr["turnDegS"])
    if fr.get("speedMs", 0.0) >= FAST_MS:
        f.append("FAST %.1f m/s" % fr["speedMs"])
    if fr["pitch"] <= -DOWN_DEG:
        f.append("%.0f deg DOWN" % -fr["pitch"])
    if fr["aimM"] < CLOSE_AIM_M:
        f.append("TARGET %.1f m" % fr["aimM"])
    return f


def hold_text(beats, shot_name):
    for b in beats or []:
        if b.get("shot") == shot_name and isinstance(b.get("seconds"), (int, float)) and "hold" in str(b.get("name", "")).lower():
            return "; parked here for the %.1f s '%s' beat before it" % (b["seconds"], b.get("name"))
    return ""


def tint(hexcol, amount):
    rgb = [int(hexcol.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4)]
    return "#%02x%02x%02x" % tuple(int(c + (255 - c) * amount) for c in rgb)


def short_label(name):
    s = re.sub(r"^(prop_|hall_)", "", name)
    s = re.sub(r"(_-?\d+(?:\.\d+)?)+$", "", s)
    return s or name


def plan_overlaps(overlaps, recs):
    src = {r.name: r.source for r in recs}
    sel = [o for o in overlaps if o["depthCm"] >= 2.0 and
           ("prop" in (src.get(o["a"]), src.get(o["b"])) or o["depthCm"] >= 10.0)]
    return sel[:PLAN_OVERLAPS]


def machine_parts(recs):
    """Props that are parts of another prop (smaller member of a same_machine pair), and props with partners."""
    props = [r for r in recs if r.source == "prop"]
    parts, partnered = set(), set()
    for i in range(len(props)):
        for j in range(i + 1, len(props)):
            a, b = props[i], props[j]
            if same_machine(a, b):
                partnered.update((a.name, b.name))
                parts.add(a.name if a.h[0] * a.h[1] < b.h[0] * b.h[1] else b.name)
    return parts, partnered


def build_plan(plan, L, recs, sel, shots, analyses, frames_by_shot, mark, facing_by_shot, box, layout_name, args, hfov_by_shot,
               frag, parts):
    ink = flat_mat("ink", "#1d1f24")
    grey = flat_mat("ink_grey", "#5b6068")
    outline = flat_mat("prop_outline", "#3a3f47")
    red = flat_mat("event_red", "#d0191b")
    orange = flat_mat("event_orange", "#e57f00")
    magenta = flat_mat("overlap", "#c2187a")
    purple = flat_mat("resident", "#6a2c91")
    yellow = flat_mat("light", "#f4c20d")
    cm = plan.cm
    sx0, sy0, sx1, sy1 = box["set"]
    names = {r.name: r for r in recs}
    # grid labels, just outside the set
    for x in range(int(math.ceil(sx0 / 500.0)) * 500, int(sx1) + 1, 500):
        plan.text("x %d" % x, x, sy1 + cm(18), 13, grey, plate=False)
    for y in range(int(math.ceil(sy0 / 500.0)) * 500, int(sy1) + 1, 500):
        plan.text("y %d" % y, sx0 - cm(8), y, 13, grey, align="RIGHT", plate=False)
    # point lights
    for l in L.get("lights", []):
        if (l.get("type") or "").lower() in ("directional", "sun"):
            continue
        lx, ly, r = l.get("x") or 0.0, l.get("y") or 0.0, cm(6)
        plan.poly([(lx, ly - r), (lx + r, ly), (lx, ly + r), (lx - r, ly)], yellow, dz=0.02)
    # props: footprint and the model's glTF front
    for r in recs:
        if r.source != "prop":
            continue
        plan.line(r.corners_ue(), 1.4, outline, dz=0.01, closed=True, joints=False)
        if r.name in parts:
            continue
        front = r.ax[0] if args.gltf_axis == "unreal" else -r.ax[1]      # glTF +Z, Blender plan axes
        ext = r.h[0] if args.gltf_axis == "unreal" else r.h[1]
        perp = np.array([-front[1], front[0]])
        tip = r.c + front * (ext + plan.m(12))
        b0 = r.c + front * (ext + plan.m(2))
        tri = [tip, b0 + perp * plan.m(6), b0 - perp * plan.m(6)]
        plan.poly([(p[1] * 100.0, p[0] * 100.0) for p in tri], outline, dz=0.015)
    # design guides (sightlines, lanes) from meta.guides
    for gi, g in enumerate((L.get("meta") or {}).get("guides") or []):
        pts = [(float(p[0]), float(p[1])) for p in g.get("points") or []]
        if len(pts) < 2:
            continue
        gm = flat_mat("guide%d" % gi, g.get("color") or "#2a9d3a")
        plan.line(pts, float(g.get("widthPx") or 2.0), gm, dz=0.04, closed=bool(g.get("closed")), joints=False)
        if g.get("label"):
            lx, ly = pts[-1] if not g.get("labelAt") else g["labelAt"]
            labels_pre = (g["label"], lx, ly)
            plan.text(g["label"], lx + cm(8), ly, 12, gm, align="LEFT", plate=True, dz=0.45, avoid=True)
    # overlap outlines
    for ov in sel:
        for rr in (names[ov["a"]], names[ov["b"]]):
            plan.line(rr.corners_ue(), 2.6, magenta, dz=0.1, closed=True)
    # resident mark and facing per shot
    rx, ry = mark[0], mark[1]
    ring = [(rx + 45 * math.cos(a), ry + 45 * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 33)[:-1]]
    plan.line(ring, 3, purple, dz=0.05, closed=True)
    plan.rects.append((rx - 80, ry - 80, rx + 80, ry + 80))
    for si, sp in enumerate(shots):
        a = math.radians(facing_at(facing_by_shot[sp.name][0], 0.0))
        dx, dy = math.cos(a), math.sin(a)
        tip, bc = (rx + 78 * dx, ry + 78 * dy), (rx + 47 * dx, ry + 47 * dy)
        plan.poly([tip, (bc[0] - 16 * dy, bc[1] + 16 * dx), (bc[0] + 16 * dy, bc[1] - 16 * dx)],
                  flat_mat("shot%d" % si, SHOT_COLOURS[si % len(SHOT_COLOURS)]), dz=0.06)
    # shots: lines, markers (registered first so labels keep off them), then labels
    disc_at = []
    labels = []
    for si, sp in enumerate(shots):
        col = SHOT_COLOURS[si % len(SHOT_COLOURS)]
        cmat = flat_mat("shot%d" % si, col)
        lmat = flat_mat("shot%d_light" % si, tint(col, 0.55))
        ar = analyses[sp.name][1]
        res = analyses[sp.name][0]
        if not sp.static:
            stride = max(1, int(len(ar["t"]) / 500))
            pts = [(float(p[0]), float(p[1])) for p in ar["P"][::stride]] + [(float(ar["P"][-1][0]), float(ar["P"][-1][1]))]
            plan.line(pts, 4.5, cmat, dz=0.0)
        for ki in range(len(sp.P)):
            p, lk = sp.P[ki], sp.Lk[ki]
            plan.line([(p[0], p[1]), (lk[0], lk[1])], 1.3, lmat, dz=-0.03, joints=False)
            r = cm(4)
            plan.line([(lk[0] - r, lk[1] - r), (lk[0] + r, lk[1] + r)], 2.2, cmat, dz=0.01, joints=False)
            plan.line([(lk[0] - r, lk[1] + r), (lk[0] + r, lk[1] - r)], 2.2, cmat, dz=0.01, joints=False)
            if not sp.static:
                plan.disc(p[0], p[1], 7, cmat, dz=0.05)
                plan.block(p[0], p[1], 8)
                labels.append(("k%d %.2fs" % (ki, sp.t_for_T(sp.kt[ki])), p[0] + cm(12), p[1] - cm(15), 13, ink, "LEFT", None))
        hf = hfov_by_shot[sp.name]
        for fi, fr in enumerate(frames_by_shot[sp.name]):
            px_, py_ = fr["pos"][0], fr["pos"][1]
            qx, qy = px_, py_
            k = 0
            while any(math.hypot(qx - ox, qy - oy) < cm(20) for ox, oy in disc_at) and k < 6:   # coincident frames
                k += 1
                qx, qy = px_ + cm(16) * k, py_ + cm(24) * k
            if (qx, qy) != (px_, py_):
                plan.line([(px_, py_), (qx, qy)], 1.2, cmat, dz=0.11, joints=False)
            a = math.radians(fr["yaw"])
            edges = fr.get("edgeYaw") or [fr["yaw"] - hf / 2.0, fr["yaw"] + hf / 2.0]
            for eb in edges:
                b = math.radians(eb)
                plan.line([(px_, py_), (px_ + cm(60) * math.cos(b), py_ + cm(60) * math.sin(b))], 1.6, cmat, dz=0.08, joints=False)
            plan.disc(qx, qy, 11, cmat, dz=0.12 + 0.01 * si)
            plan.disc(qx, qy, 8.5, plan.white, dz=0.125 + 0.01 * si)
            plan.text(str(fi + 1) if not sp.static else "L", qx, qy, 10, ink, plate=False, dz=0.5 + 0.01 * si)
            plan.block(qx, qy, 12)
            disc_at.append((qx, qy))
        through = {}
        for e in res["passesThrough"]:
            x, y = e["pos"][0], e["pos"][1]
            r = cm(8)
            plan.line([(x - r, y - r), (x + r, y + r)], 3.5, red, dz=0.2, joints=False)
            plan.line([(x - r, y + r), (x + r, y - r)], 3.5, red, dz=0.2, joints=False)
            plan.block(x, y, 9)
            labels.append(("%.2fs: camera passes through %s" % (e["t"], e["object"]), x + cm(14), y + cm(24), 13, red, "LEFT", red))
            through.setdefault(e["object"], []).append(e["t"])
        for e in res["inside"]:
            if any(abs(t - e["t0"]) < 0.3 for t in through.get(e["object"], [])):
                continue
            x, y = e["pos"][0], e["pos"][1]
            plan.disc(x, y, 6, red, dz=0.21)
            plan.block(x, y, 7)
            labels.append(("%.2f-%.2fs: camera inside %s" % (e["t0"], e["t1"], e["object"]), x + cm(12), y - cm(20), 13, red, "LEFT", red))
        for e in res["nearMisses"][:6]:
            if e["object"] in through:
                continue
            x, y = e["pos"][0], e["pos"][1]
            plan.disc(x, y, 6, orange, dz=0.21)
            plan.block(x, y, 7)
            labels.append(("%.2fs: %.0f cm from %s" % (e["t"], e["cm"], e["object"]), x + cm(12), y + cm(22), 13, orange, "LEFT", orange))
    for s, x, y, px, mat, align, leader in labels:
        plan.text(s, x, y, px, mat, align=align, dz=0.55, avoid=True, leader=leader)
    plan.text("resident (%.0f, %.0f)" % (rx, ry), rx, ry + 62 + cm(10), 12, purple, dz=0.4, avoid=True)
    # prop labels (and blockouts that carry a "label"), then overlap tags
    explicit = {}
    for item in list(L.get("props", [])) + list(L.get("blockouts", [])):
        if item.get("label"):
            explicit[str(item.get("name"))] = str(item["label"])
    for r in recs:
        if r.name in explicit:
            cs = r.corners_ue()
            plan.text(explicit[r.name], sum(c[0] for c in cs) / 4.0, max(c[1] for c in cs) + cm(10), 11, ink, dz=0.25, avoid=True)
            continue
        if r.source != "prop" or r.name in parts:
            continue
        cs = r.corners_ue()
        lbl = short_label(r.name) + ((" " + frag[r.name]) if r.name in frag else "")
        plan.text(lbl, sum(c[0] for c in cs) / 4.0, max(c[1] for c in cs) + cm(10), 11, red if r.name in frag else ink,
                  dz=0.25, avoid=True)
    for i, ov in enumerate(sel):
        ra, rb = names[ov["a"]], names[ov["b"]]
        ca, cb = b2ue((ra.c[0], ra.c[1], 0)), b2ue((rb.c[0], rb.c[1], 0))
        mx, my = (ca[0] + cb[0]) / 2.0, (ca[1] + cb[1]) / 2.0
        plan.text("o%d" % (i + 1), mx, my, 12, magenta, dz=0.35, avoid=True, leader=magenta)
    # overlap legend under the set
    lx0, ly0 = box["legend"]
    plan.text("box overlaps (oriented boxes, minimum push-out depth; the %d deepest involving a prop or >= 10 cm; all in report.json):" % len(sel),
              lx0, ly0, 14, magenta, align="LEFT", plate=False, dz=0.6)
    half = int(math.ceil(len(sel) / 2.0))
    colw = (sx1 - sx0) / 2.0
    for i, ov in enumerate(sel):
        c_, r_ = divmod(i, half) if half else (0, 0)
        plan.text("o%-3d %4.0f cm  %s  x  %s" % (i + 1, ov["depthCm"], ov["a"], ov["b"]), lx0 + c_ * colw, ly0 + cm(22) * (r_ + 1), 13, ink,
                  align="LEFT", plate=False, dz=0.6)
    # title and legend, top-left
    tx, ty = box["title"]
    lines = ["%s  |  top view in UE cm: +X to the right (glass front -> back wall), +Y down  |  1 m grid, labelled every 5 m  |  ceiling and troffers hidden"
             % layout_name,
             "props: GLB meshes as placed (%s glTF axis), grey outline = footprint, grey triangle = the model's glTF front; "
             "red name + n/N = only n of the file's N mesh nodes placed" % args.gltf_axis,
             "yellow diamonds = point lights;  purple ring = resident mark (%.2f m stand-in), coloured arrows = where she faces in each shot;  "
             "magenta o1.. = box overlaps (legend below)" % args.figure_height]
    for si, sp in enumerate(shots):
        res = analyses[sp.name][0]
        lines.append("%s = %s: %s, %d key%s%s; numbered discs = contact-sheet frames (%s sampling) with the horizontal view wedge"
                     % (["vermilion", "blue", "green", "purple", "amber", "teal"][si % 6], sp.name,
                        "static" if sp.static else "%.1f s, %.1f m" % (sp.seconds, res["pathLengthM"]), len(sp.P),
                        "" if len(sp.P) == 1 else "s",
                        "" if sp.static else "; k = key and the time the camera passes it, x = its look-at target", args.sampling))
    lines.append("red = camera passes through / is inside a box;  orange = camera within %.0f cm of a surface" % args.clearance_warn)
    for i, s in enumerate(lines):
        plan.text(s, tx, ty + i * cm(24), 14, ink, align="LEFT", plate=False, dz=0.6)


# ------------------------------------------------------------------------------------- render
def setup_eevee(sc, W, H, samples):
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.render.use_stamp = False
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGB'
    ee = sc.eevee
    ee.taa_render_samples = samples
    for k, v in (("use_shadows", True), ("shadow_pool_size", '1024'), ("shadow_resolution_scale", 0.5),
                 ("use_raytracing", bool(REFLECTIONS))):
        try:
            setattr(ee, k, v)
        except Exception as e:
            log("eevee setting %s skipped: %s" % (k, e))
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'None'


def render_still(sc, path):
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path


def mean_display(path):
    a = load_png(path)
    return float(a[..., :3].mean())


REFLECTIONS = False


def solve_exposure(sc, cam, fig, mark, sp, t, facing, mode, tmpdir, W, H):
    pos, look, _ = sp.at(t)
    f = sp.focal_at(t, lens_for(sp.shot)[0])
    apply_lens(cam, sp.shot, f, solve_shift(sp, t, pos, look, f, W, H))
    pose_camera(cam, pos, look, level=sp.level)
    set_figure(fig, mark, facing, head_at(sp.shot, t))
    sc.render.resolution_x, sc.render.resolution_y = max(160, W // 4), max(90, H // 4)
    keep = sc.eevee.taa_render_samples
    sc.eevee.taa_render_samples = 8
    target = 120.0 / 255.0
    exp = 0.0
    hist = []
    for it in range(5):
        sc.view_settings.exposure = exp
        apply_emissive(exp, mode)
        m = mean_display(render_still(sc, os.path.join(tmpdir, "_exposure.png")))
        hist.append((round(exp, 3), round(m, 4)))
        if m <= 1e-4:
            exp += 3.0
            continue
        step = math.log2(target / m) * 2.2
        exp += max(-4.0, min(4.0, step))
        if abs(m - target) < 0.006:
            break
    sc.eevee.taa_render_samples = keep
    sc.render.resolution_x, sc.render.resolution_y = W, H
    try:
        os.remove(os.path.join(tmpdir, "_exposure.png"))
    except OSError:
        pass
    return exp, hist


# ---------------------------------------------------------------------------------------- main
def main():
    global REFLECTIONS
    t_start = time.time()
    args = parse_args()
    REFLECTIONS = bool(args.reflections)
    layout_path = os.path.abspath(args.layout)
    with open(layout_path, "r", encoding="utf-8") as f:
        L = json.load(f)
    layout_name = os.path.basename(layout_path)
    out = os.path.abspath(args.out)
    for sub in ("", "frames", "paths"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    W, H = [int(v) for v in args.res.lower().split("x")]
    only = {s.strip() for s in args.only.split(",") if s.strip()}
    overrides = {}
    for s in args.resident_yaw:
        k, _, v = s.partition("=")
        overrides[k.strip()] = float(v)

    reset_scene()
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    C = {k: new_coll(k) for k in ("glb_library", "props", "blockouts", "grid", "resident", "lights", "cams", "plan")}
    layer_coll(C["glb_library"]).exclude = True
    cube_me = unit_cube_mesh()
    lib = GlbLib(args.glb_dir, C["glb_library"])
    axis_fix = AXIS_FIX[args.gltf_axis]

    # ---- set
    recs, prop_status = [], []
    for p in L.get("props", []):
        objs, status = place_prop(p, lib, axis_fix, C["props"], cube_me)
        prop_status.append({"name": p.get("name"), "glb": p.get("glb"), "mesh": p.get("mesh"), "status": status, "objects": len(objs)})
        recs.append((p, objs, status))
    CENSUS_BK.clear()
    CENSUS_BK.update({str(b.get("name")): b for b in L.get("blockouts", [])})
    CENSUS_PROP.clear()
    CENSUS_PROP.update({str(p.get("name")): p for p in L.get("props", [])})
    bk_objs = []
    for b in L.get("blockouts", []):
        bk_objs.append((b, place_blockout(b, C["blockouts"], cube_me)))
    build_grid(L.get("blockouts", []), C["grid"])
    res = L.get("resident") or {"x": 0, "y": 0, "z": 0, "yaw": 0}
    mark = (float(res.get("x", 0)), float(res.get("y", 0)), float(res.get("z", 0)))
    base_facing, base_src = resident_base_facing(res)
    fig = build_figure(args.figure_height, C["resident"])
    set_figure(fig, mark, base_facing)
    global WB_WHITE
    wb_k = (L.get("meta") or {}).get("whiteBalanceK")
    WB_WHITE = white_balance_white(wb_k) if wb_k else (1.0, 1.0, 1.0)
    log("white balance %s: lights balanced by linear white %s" % (wb_k or "off", ", ".join("%.3f" % c for c in WB_WHITE)))
    for l in L.get("lights", []):
        place_light(l, C["lights"])
    world = bpy.data.worlds.new("previz_world")
    sc.world = world
    world.color = (0.86, 0.86, 0.85)          # Workbench background for top.png
    try:
        bg = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
        bg.inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)   # no sky, no SkyLight (as in Unreal)
        bg.inputs["Strength"].default_value = 0.0
    except Exception as e:
        log("world nodes:", e)
    cam = bpy.data.objects.new("previz_cam", bpy.data.cameras.new("previz_cam"))
    C["cams"].objects.link(cam)
    cam.data.clip_start = NEAR_PLANE_CM / 100.0
    cam.data.clip_end = 400.0
    sc.camera = cam
    bpy.context.view_layer.update()

    # ---- boxes, overlaps
    R = []
    for p, objs, status in recs:
        R.append(Rec(str(p.get("name")), "prop", "prop", float(p.get("yaw") or 0.0), objs, glb=p.get("glb"),
                     part_of=p.get("partOf"), mesh=p.get("mesh"), status=status))
    for b, o in bk_objs:
        R.append(Rec(str(b.get("name")), "blockout", (b.get("kind") or "").lower(), float(b.get("yaw") or 0.0), [o],
                     part_of=b.get("partOf"), census=b.get("census")))
    overlaps = find_overlaps(R, args.overlap_tol)
    log("%d props (%d objects), %d blockouts, %d lights; %d box overlaps > %.1f cm"
        % (len(recs), sum(len(o) for _, o, _ in recs), len(bk_objs), len(L.get("lights", [])), len(overlaps), args.overlap_tol))

    # ---- shots
    # shots flagged "reference": true (e.g. today's opening, resolved for comparison) are documentation only:
    # they would fly through this set, so they are neither analysed nor rendered (top.png draws them from meta.guides)
    shots = [ShotPath(s, args) for s in L.get("shots", []) if not s.get("reference")]
    beats = L.get("beats", []) or []
    facing_by_shot = {sp.name: resolve_facing(sp.shot, beats, base_facing, overrides) for sp in shots}
    for sp in shots:
        log("shot %-20s facing %s (from %s)" % (sp.name, facing_by_shot[sp.name][0], facing_by_shot[sp.name][1]))
    bvh_a, owner_a = build_bvh(R)
    bvh_c, owner_c = build_bvh(R, exclude_kinds=FLOOR_KINDS)
    mark_b = ue2b(mark)
    fig_h = args.figure_height
    analyses, frames_by_shot, hfov_by_shot, lens_by_shot, res_by_shot = {}, {}, {}, {}, {}
    k_fig = fig_h / 1.70
    for sp in shots:
        sw_, sh_ = [int(v) for v in str(sp.shot.get("res") or args.res).lower().split("x")]
        res_by_shot[sp.name] = (sw_, sh_)
        eye_anchor = sp.aim if sp.aim is not None else np.array(
            figure_point(mark, facing_at(facing_by_shot[sp.name][0], sp.seconds), k_fig, EYE_LOCAL), dtype=np.float64)
        r, ar = analyse_shot(sp, args.report_hz, R, bvh_a, owner_a, bvh_c, owner_c, mark_b, fig_h, args.clearance_warn,
                             eye_ue=eye_anchor)
        analyses[sp.name] = (r, ar)
        lens, sw, sh, vfov = lens_for(sp.shot)
        hfov = math.degrees(2.0 * math.atan(math.tan(math.radians(vfov) / 2.0) * sw_ / sh_))
        hfov_by_shot[sp.name] = hfov
        lens_by_shot[sp.name] = (lens, vfov, hfov)
        r["lens"] = {"focalMm": round(lens, 2), "sensorMm": [sw, round(sh, 2)], "vfovDeg": round(vfov, 2), "hfovDeg": round(hfov, 2),
                     "res": [sw_, sh_]}
        frs = []
        pts = frame_points(sp, args.frames, args.sampling)
        if sp.shot.get("sheetTimes") and not sp.static:
            pts = [(float(t), sp.T_at(float(t))) for t in sp.shot["sheetTimes"]]
        for (t, T) in pts:
            fy = facing_at(facing_by_shot[sp.name][0], t)
            fr = frame_info(sp, t, T, R, bvh_c, owner_c, mark_b, fig_h, fy, W=sw_, H=sh_, mark=mark, head=head_at(sp.shot, t))
            fr["flags"] = frame_flags(fr, args.clearance_warn)
            frs.append(fr)
        frames_by_shot[sp.name] = frs
        with open(os.path.join(out, "paths", "%s.csv" % sp.name), "w", newline="", encoding="utf-8") as f:
            wr = csv.writer(f)
            wr.writerow(["t", "T", "x", "y", "z", "lookx", "looky", "lookz", "speed_ms", "accel_ms2", "yaw", "pitch",
                         "yawrate_degs", "turn_degs", "aim_m", "clearance_cm", "nearest"])
            for j in range(len(ar["t"])):
                wr.writerow(["%.4f" % ar["t"][j], "%.5f" % ar["T"][j]] + ["%.1f" % c for c in ar["P"][j]] + ["%.1f" % c for c in ar["L"][j]] +
                            ["%.3f" % (ar["v"][j] / 100), "%.2f" % (ar["acc"][j] / 100), "%.2f" % ar["yaw"][j], "%.2f" % ar["pitch"][j],
                             "%.1f" % ar["yawrate"][j], "%.1f" % ar["turn"][j], "%.3f" % (ar["aim"][j] / 100), "%.1f" % (ar["clr"][j] * 100),
                             ar["near"][j]])

    report = {
        "layout": layout_path, "glbDir": args.glb_dir, "blender": bpy.app.version_string,
        "mapping": "Blender (x,y,z) m = (y_ue, x_ue, z_ue)/100, rotation_z = -yaw (the web -> UE mapping author_diorama.py documented at 0c78440, inverted via the glTF importer); "
                   "GLB meshes turned +90 deg about Z (--gltf-axis %s) to match Unreal Interchange's (gx, gz, gy)" % args.gltf_axis
                   if args.gltf_axis == "unreal" else
                   "Blender (x,y,z) m = (y_ue, x_ue, z_ue)/100, rotation_z = -yaw; GLB meshes as the web/three.js layout faces them (--gltf-axis web)",
        "figure": {"heightM": fig_h, "eyeZcm_director": round(fig_h * 100 * 0.93, 1), "mark": mark, "baseFacingYaw": base_facing,
                   "baseFacingFrom": base_src, "layoutEyeZcm": (L.get("meta") or {}).get("resident_eye_z")},
        "props": prop_status, "overlaps": overlaps,
        "objects": [dict(name=r.name, source=r.source, kind=r.kind, **r.ue()) for r in R],
        "shots": [], "renders": {}, "settings": vars(args),
    }
    for sp in shots:
        r = analyses[sp.name][0]
        r["facing"] = {"value": facing_by_shot[sp.name][0], "from": facing_by_shot[sp.name][1]}
        r["frames"] = frames_by_shot[sp.name]
        r["sampling"] = args.sampling
        report["shots"].append(r)

    parts, partnered = machine_parts(R)
    glb_nodes = {f: len(e) for f, e in lib.cache.items() if e}
    frag = {}
    for p in L.get("props", []):
        n = glb_nodes.get(p.get("glb"), 1)
        if p.get("mesh") and n > 1 and str(p.get("name")) not in partnered:
            frag[str(p.get("name"))] = "1/%d" % n
    report["fragments"] = frag

    # ---- renders
    tmpdir = os.path.join(out, "frames")
    if not args.no_render:
        card = TextCard(tmpdir)
        setup_eevee(sc, W, H, args.samples)
        C["plan"].hide_render = True
        exp_mode = args.exposure.strip().lower()
        if exp_mode == "auto":
            ref_shot, ref_t = shots[-1], shots[-1].seconds
            if args.exposure_ref:
                nm, _, tt = args.exposure_ref.partition("@")
                ref_shot = next(s for s in shots if s.name == nm)
                ref_t = float(tt) if tt else ref_shot.seconds
            apply_lens(cam, ref_shot.shot)
            exposure, hist = solve_exposure(sc, cam, fig, mark, ref_shot, ref_t, facing_at(facing_by_shot[ref_shot.name][0], ref_t),
                                            args.emissive, tmpdir, W, H)
            report["exposure"] = {"mode": "auto: mean 120/255 on %s @ %.2fs" % (ref_shot.name, ref_t), "blenderExposure": round(exposure, 3),
                                  "ev100Equivalent": round(EV100_OFFSET - exposure, 2), "iterations": hist}
        elif exp_mode.startswith("ev100:"):
            ev = float(exp_mode.split(":", 1)[1])
            exposure = EV100_OFFSET - ev
            report["exposure"] = {"mode": "pinned EV100 %.2f" % ev, "blenderExposure": round(exposure, 3), "ev100Equivalent": ev}
        else:
            exposure = float(exp_mode)
            report["exposure"] = {"mode": "fixed", "blenderExposure": exposure, "ev100Equivalent": round(EV100_OFFSET - exposure, 2)}
        sc.view_settings.exposure = exposure
        apply_emissive(exposure, args.emissive)
        log("exposure %.3f (EV100 ~%.2f)" % (exposure, EV100_OFFSET - exposure))
        pfrac = (9.0 / 16.0) / (W / H) if W > H else 1.0

        if "opening" in only and shots:
            sp = shots[0]
            OW, OH = res_by_shot[sp.name]
            pos, look, T = sp.at(0.0)
            f0 = sp.focal_at(0.0, lens_for(sp.shot)[0])
            sh0 = solve_shift(sp, 0.0, pos, look, f0, OW, OH)
            lens, vfov = apply_lens(cam, sp.shot, f0, sh0)
            fy = facing_at(facing_by_shot[sp.name][0], 0.0)
            fi = frame_info(sp, 0.0, T, R, bvh_c, owner_c, mark_b, fig_h, fy, W=OW, H=OH, mark=mark, head=head_at(sp.shot, 0.0))
            pose_camera(cam, pos, look, level=sp.level, focus=(fi.get("eyeDistM") or 0) * 1.0 or None)
            set_figure(fig, mark, fy, head_at(sp.shot, 0.0))
            sc.eevee.taa_render_samples = args.samples * 2
            sc.render.resolution_x, sc.render.resolution_y = OW, OH
            raw = render_still(sc, os.path.join(tmpdir, "opening_raw.png"))
            sc.eevee.taa_render_samples = args.samples
            img = load_png(raw)
            if not args.no_overlays and sp.shot.get("hud") == "lobby":
                draw_overlays(img, "lobby", None, None)
            flags = frame_flags(fi, args.clearance_warn)
            cap = ["opening.png  |  %s, first frame (t = 0.00 s%s)  |  %s%s"
                   % (sp.name, hold_text(beats, sp.name), layout_name, ("  |  " + args.tag) if args.tag else ""),
                   "camera (%.0f, %.0f, %.0f) yaw %.1f pitch %.1f  |  %.1f mm on 36 x 20.25 mm, vertical FOV %.1f deg, sensor shift (%.2f, %.2f) mm  |  eyes at (%s) of the frame, head %s px"
                   % (tuple(fi["pos"]) + (fi["yaw"], fi["pitch"], lens, vfov, fi["shiftMm"][0], fi["shiftMm"][1],
                      ", ".join("%.2f" % c for c in fi.get("eyesUV", [0, 0])), fi.get("facePx", "-"))),
                   ("nearest surface %.0f cm (%s)  |  stand-in %.2f m, resident faces yaw %.0f  |  exposure %s (Blender %.2f, EV100 ~%.1f)  |  EEVEE approximation, not Unreal lighting%s"
                    % (fi["clearanceCm"], fi["nearest"], fig_h, fy, report["exposure"]["mode"].split(":")[0], exposure, EV100_OFFSET - exposure,
                       "; screen-space reflections ON" if REFLECTIONS else ""), FG_DIM)]
            if flags:
                cap.append(("flags: " + "  |  ".join(flags), FG_WARN))
            strip = card.block(cap, OW, 16)
            canvas = np.concatenate([img, strip], axis=0)
            canvas[..., 3] = 1.0
            save_png(canvas, os.path.join(out, "opening.png"))
            report["renders"]["opening"] = os.path.join(out, "opening.png")
            log("wrote opening.png")

        if "sheets" in only:
            report["renders"]["sheets"] = {}
            want = {s.strip() for s in args.shots.split(",") if s.strip()}
            for si, sp in enumerate(shots):
                if want and sp.name not in want:
                    continue
                SW, SH = res_by_shot[sp.name]
                lens, vfov = apply_lens(cam, sp.shot)
                frs = frames_by_shot[sp.name]
                r = analyses[sp.name][0]
                big = sp.static or len(frs) == 1
                portrait = SH > SW
                th_full = SH
                ts_ = args.tile_scale if not portrait else min(1.0, args.tile_scale * 1.6)
                tw, th = (SW, SH) if big else (int(SW * ts_), int(SH * ts_))
                cols = 1 if big else (3 if not portrait else 4)
                font = 16 if big else 13
                imgs, caps = [], []
                pf = (9.0 / 16.0) / (SW / SH) if SW > SH else 1.0
                for fi, fr in enumerate(frs):
                    lens_f, vfov_f = apply_lens(cam, sp.shot, fr["focalMm"], fr["shiftMm"])
                    pose_camera(cam, fr["pos"], fr["lookAt"], level=sp.level, focus=(fr.get("eyeDistM") or 0) or None)
                    set_figure(fig, mark, fr["facingYaw"], fr.get("head"))
                    sc.render.resolution_x, sc.render.resolution_y = tw, th
                    pth = render_still(sc, os.path.join(tmpdir, "%s_%02d.png" % (sp.name, fi + 1)))
                    fr["file"] = pth
                    img = load_png(pth)
                    if sp.shot.get("census"):
                        crown_v = fr.get("headroom")
                        chin_v = (crown_v + fr["facePx"] / float(th_full)) if (crown_v is not None and fr.get("facePx")) else None
                        sc.render.resolution_x, sc.render.resolution_y = SW, SH
                        cz = frame_census(sc, cam, fr.get("eyesUV"), crown_v, chin_v, SW, SH, portrait=portrait)
                        cz["nearWhite"], cz["nearWhiteInZone"] = near_white_share(img)
                        fr["census"] = cz
                    if sp.name in VALUE_GATE_SHOTS and fi == 0:
                        vs = value_structure(sc, cam, img)
                        fr["valueStructure"] = vs
                        log("value structure %s: ceiling panels %s (n %d) vs floor %s (n %d) -> %s"
                            % (sp.name, vs["ceiling"]["medianDisplay"], vs["ceiling"]["n"], vs["floor"]["medianDisplay"],
                               vs["floor"]["n"], {True: "PASS", False: "FAIL", None: "n/a"}[vs["pass"]]))
                    if not args.no_overlays:
                        draw_overlays(img, str(sp.shot.get("hud") or "none"), fr.get("eyesUV"), fr.get("screenTarget"))
                    elif not portrait:
                        portrait_guides(img, pf)
                    imgs.append(img)
                    flags = frame_flags(fr, args.clearance_warn)
                    fr["flags"] = flags
                    lab = ["%s  t %.2fs  s %.2fm  %.2f m/s  turn %.1f deg/s  %.0f mm"
                           % ("static" if sp.static else "%d/%d" % (fi + 1, len(frs)), fr["t"], fr["sM"], fr["speedMs"], fr["turnDegS"],
                              fr["focalMm"] if not sp.shot.get("filmbackMm") else fr["focalMm"] * float(sp.shot["filmbackMm"][1]) / 20.25),
                           "cam (%.0f,%.0f,%.0f) yaw %.1f pitch %.0f | shift (%.2f, %.2f) mm"
                           % (fr["pos"][0], fr["pos"][1], fr["pos"][2], fr["yaw"], fr["pitch"], fr["shiftMm"][0], fr["shiftMm"][1]),
                           "eyes (%s) %s m | head %s px | headroom %s | body %.0f head %s"
                           % (", ".join("%.2f" % c for c in fr.get("eyesUV", [0, 0])), fr.get("eyeDistM", "-"), fr.get("facePx", "-"),
                              fr.get("headroom", "-"), fr["facingYaw"], "/".join("%.0f" % c for c in fr.get("head", [0, 0]))),
                           ("nearest surface %.0fcm: %s" % (fr["clearanceCm"], fr["nearest"] or "-"), FG_DIM)]
                    if fr.get("census"):
                        lab.append((census_text(fr["census"]), FG_TEXT))
                    if flags:
                        lab.append((" | ".join(flags), FG_WARN))
                    caps.append(lab)
                need = max(len(TextCard.wrap(c, tw, font)) for c in caps)
                tiles = [np.concatenate([im, card.block(c, tw, font, min_rows=need)], axis=0) for im, c in zip(imgs, caps)]
                gut = 8
                rows = int(math.ceil(len(tiles) / float(cols)))
                tile_h = tiles[0].shape[0]
                sheet_w = cols * tw + (cols + 1) * gut
                events = len(r["inside"]) + len(r["passesThrough"]) + len(r["nearMisses"])
                ev_line = ("camera inside a box %d time(s), passes through %d surface(s)%s, nearest approach %.0f cm (%s @%.2fs)"
                           % (len(r["inside"]), len(r["passesThrough"]),
                              (" (" + ", ".join("%s @%.2fs" % (e["object"], e["t"]) for e in r["passesThrough"]) + ")") if r["passesThrough"] else "",
                              r["minClearance"]["cm"], r["minClearance"]["object"], r["minClearance"]["t"]))
                if r["nearMisses"]:
                    ev_line += "; under %.0f cm: " % args.clearance_warn + ", ".join(
                        "%s %.0fcm @%.2fs" % (e["object"], e["cm"], e["t"]) for e in r["nearMisses"])
                cf = r.get("comfort") or {}
                comfort_line = ""
                if cf:
                    fails = [c["rule"] for c in cf.get("checks", []) if not c["pass"]]
                    comfort_line = ("comfort: peak %.2f m/s (vertical %.2f), accel %.2f m/s2, yaw %.1f deg/s = %.2f x HFOV/7, total yaw %.0f deg, "
                                    "pitch max %.1f, clearance moving %.0f cm, eyes moving >= %s cm, time to contact >= %s s, growth %.0f %%/s, zoom %.0f %%/s  ->  %s"
                                    % (cf["peakSpeedMs"], cf["peakVerticalSpeedMs"], cf["peakAccelMs2"], cf["peakYawRateDegS"], cf["peakYawRateOverBudget"],
                                       cf["totalYawDeg"], cf["pitchAbsMaxDeg"], cf["minClearanceMovingCm"], cf.get("minEyeDistMovingCm"),
                                       cf.get("minTimeToContactS"), cf["peakGrowthPctS"], cf["peakZoomPctS"],
                                       ("ALL BUDGETS MET" if not fails else "FAILS: " + "; ".join(fails))))
                head = ["%s  |  %s  |  %d frame%s, timing %s, ease %s%s%s"
                        % (layout_name, sp.name, len(tiles), "" if len(tiles) == 1 else "s", sp.timing, sp.ease_name,
                           ", centripetal Catmull-Rom" if sp.centripetal else ", tangents x%.2f" % sp.tscale,
                           ("  |  " + args.tag) if args.tag else ""),
                        ("static shot, 1 key" if sp.static else
                         "%.1f s  path %.2f m  speed %.2f-%.2f m/s  peak view turn %.1f deg/s @%.2fs  total yaw %.1f deg  closest look target %.2f m"
                         % (sp.seconds, r["pathLengthM"], r["speedMs"]["min"], r["speedMs"]["max"],
                            r["peakTurnDegS"]["value"], r["peakTurnDegS"]["t"], r["totalYawDeg"], r["closestAimM"]["value"])),
                        (comfort_line, FG_WARN if (cf and any(not c["pass"] for c in cf.get("checks", []))) else FG_TEXT) if comfort_line else "",
                        (ev_line, FG_WARN if events else FG_TEXT),
                        "lens %s (vertical FOV %.1f deg), level camera %s, composition by sensor shift  |  stand-in %.2f m, resident faces %s"
                        % (lens_label(sp.shot, lens) if sp.shot.get("filmbackMm") else "%.1f mm on 36 x 20.25 mm" % lens, vfov,
                           "yes" if sp.level else "no", fig_h, facing_by_shot[sp.name][0] if not isinstance(facing_by_shot[sp.name][0], list) else "keyed"),
                        ("overlays: tinted = reserved UI (%s), white ticks = thirds, yellow cross = where the eyes land, green cross = the composition target  |  EEVEE approximation, exposure %.2f (EV100 ~%.1f)%s"
                         % (sp.shot.get("hud") or "none", exposure, EV100_OFFSET - exposure, "; SSR on" if REFLECTIONS else ""), FG_DIM),
                        ("purpose: " + (sp.shot.get("purpose") or ""), FG_DIM)]
                head = [h for h in head if h]
                header = card.block(head, sheet_w, 15)
                sheet_h = header.shape[0] + rows * tile_h + (rows + 1) * gut
                canvas = np.zeros((sheet_h, sheet_w, 4), dtype=np.float32)
                canvas[..., :3] = 0.03
                canvas[:header.shape[0], :header.shape[1]] = header
                for i, tl in enumerate(tiles):
                    rr, cc = divmod(i, cols)
                    y = header.shape[0] + gut + rr * (tile_h + gut)
                    x = gut + cc * (tw + gut)
                    canvas[y:y + tile_h, x:x + tw] = tl
                canvas[..., 3] = 1.0
                name = "sheet_%s.png" % re.sub(r"[^A-Za-z0-9_.-]+", "_", sp.name)
                save_png(canvas, os.path.join(out, name))
                report["renders"]["sheets"][sp.name] = os.path.join(out, name)
                log("wrote", name)

        if "top" in only:
            xs, ys = [], []
            for r in R:
                for (x, y) in r.corners_ue():
                    xs.append(x)
                    ys.append(y)
            for sp in shots:
                ar = analyses[sp.name][1]
                xs += [float(v) for v in ar["P"][:, 0]] + [float(k[0]) for k in sp.Lk]
                ys += [float(v) for v in ar["P"][:, 1]] + [float(k[1]) for k in sp.Lk]
            sx0, sx1, sy0, sy1 = min(xs), max(xs), min(ys), max(ys)
            TW = args.top_width
            x0, x1 = sx0 - 190.0, sx1 + 70.0
            ppm = TW / ((x1 - x0) / 100.0)

            def px2cm(px):
                return px / ppm * 100.0
            sel = plan_overlaps(overlaps, R)
            n_title = 5 + len(shots)
            y0 = sy0 - 60.0 - px2cm(n_title * 24 + 26)
            legend_y = sy1 + 60.0 + px2cm(34)
            y1 = legend_y + px2cm((int(math.ceil(len(sel) / 2.0)) + 1) * 22 + 20)
            TH = int(round(TW * (y1 - y0) / (x1 - x0)))
            plan = Plan(C["plan"], ppm)
            box = {"set": (sx0, sy0, sx1, sy1), "title": (x0 + px2cm(16), y0 + px2cm(22)), "legend": (x0 + px2cm(16), legend_y)}
            build_plan(plan, L, R, sel, shots, analyses, frames_by_shot, mark, facing_by_shot, box, layout_name, args, hfov_by_shot,
                       frag, parts)
            pc = bpy.data.objects.new("plan_cam", bpy.data.cameras.new("plan_cam"))
            C["cams"].objects.link(pc)
            pc.data.type = 'ORTHO'
            pc.data.sensor_fit = 'HORIZONTAL'
            pc.data.ortho_scale = (x1 - x0) / 100.0
            pc.data.clip_start, pc.data.clip_end = 1.0, 200.0
            pc.location = ue2b(((x0 + x1) / 2.0, (y0 + y1) / 2.0, 6000.0))
            pc.rotation_euler = (0.0, 0.0, math.pi / 2.0)   # screen right = UE +X, screen down = UE +Y
            hidden = []
            for b, o in bk_objs:
                if (b.get("kind") or "").lower() in TOP_HIDE_KINDS:
                    o.hide_render = True
                    hidden.append(o)
            C["plan"].hide_render = False
            C["lights"].hide_render = True
            set_figure(fig, mark, facing_at(facing_by_shot[shots[-1].name][0], 0.0) if shots else base_facing)
            sc.camera = pc
            sc.render.engine = 'BLENDER_WORKBENCH'
            sh = sc.display.shading
            sh.light = 'FLAT'
            sh.color_type = 'TEXTURE'
            sh.show_object_outline = True
            sh.object_outline_color = (0.2, 0.2, 0.22)
            sh.show_cavity = False
            sh.show_shadows = False
            try:
                sc.display.render_aa = '16'
            except Exception:
                pass
            sc.view_settings.view_transform = 'Standard'
            ve = sc.view_settings.exposure
            sc.view_settings.exposure = 0.0
            sc.render.resolution_x, sc.render.resolution_y = TW, TH
            render_still(sc, os.path.join(out, "top.png"))
            sc.view_settings.exposure = ve
            sc.view_settings.view_transform = 'AgX'
            for o in hidden:
                o.hide_render = False
            C["plan"].hide_render = True
            C["lights"].hide_render = False
            sc.camera = cam
            report["renders"]["top"] = os.path.join(out, "top.png")
            log("wrote top.png (%dx%d, %.1f px/m)" % (TW, TH, ppm))

    if args.save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.save_blend), copy=True)

    report["seconds"] = round(time.time() - t_start, 1)
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
    txt = report_text(report)
    with open(os.path.join(out, "report.txt"), "w", encoding="utf-8") as f:
        f.write(txt)
    print(txt, flush=True)
    log("done in %.1fs -> %s" % (time.time() - t_start, out))


def report_text(rep):
    L = ["previz report: %s" % rep["layout"], "glb dir: %s   blender %s" % (rep["glbDir"], rep["blender"]),
         "mapping: %s" % rep["mapping"],
         "stand-in figure %.2f m (director EyeZ %.1f cm; layout eye_z %s), facing %s from %s"
         % (rep["figure"]["heightM"], rep["figure"]["eyeZcm_director"], rep["figure"]["layoutEyeZcm"], rep["figure"]["baseFacingYaw"],
            rep["figure"]["baseFacingFrom"])]
    bad = [p for p in rep["props"] if p["status"] != "ok"]
    L.append("props: %d placed, %d with a missing glb/mesh%s" % (len(rep["props"]), len(bad),
                                                               ("".join("\n  %s: %s (%s / %s)" % (p["name"], p["status"], p["glb"], p["mesh"]) for p in bad))))
    if "exposure" in rep:
        L.append("exposure: %s -> Blender %.2f (EV100 ~%.2f)" % (rep["exposure"]["mode"], rep["exposure"]["blenderExposure"],
                                                              rep["exposure"]["ev100Equivalent"]))
    L.append("")
    L.append("BOX OVERLAPS (oriented boxes: 2D separating-axis test with z overlap; room shell vs shell, wall dressing excepted, and parts of one machine skipped): %d"
             % len(rep["overlaps"]))
    for o in rep["overlaps"]:
        L.append("  %6.1f cm  %-28s x %-28s (z overlap %.0f cm)" % (o["depthCm"], o["a"], o["b"], o["zOverlapCm"]))
    for s in rep["shots"]:
        L.append("")
        L.append("SHOT %s: %s, %d keys, timing %s, ease %s, tangents x%.2f, lens %.1f mm (vFOV %.1f, hFOV %.1f)"
                 % (s["name"], "static" if s["static"] else "%.2f s" % s["seconds"], s["keys"], s["timing"], s["ease"], s["tangentScale"],
                    s["lens"]["focalMm"], s["lens"]["vfovDeg"], s["lens"]["hfovDeg"]))
        if not s["static"]:
            L.append("  path %.2f m; speed %.2f-%.2f m/s (mean %.2f, max at t=%.2f %s); peak accel %.1f m/s2 @%.2fs"
                     % (s["pathLengthM"], s["speedMs"]["min"], s["speedMs"]["max"], s["speedMs"]["mean"], s["speedMs"]["maxAt"]["t"],
                        s["speedMs"]["maxAt"]["pos"], s["peakAccelMs2"]["value"], s["peakAccelMs2"]["t"]))
            L.append("  peak view turn %.0f deg/s @%.2fs; peak yaw rate %.0f deg/s @%.2fs; total yaw %.0f deg; pitch %.1f..%.1f (min @%.2fs); closest look target %.2f m @%.2fs"
                     % (s["peakTurnDegS"]["value"], s["peakTurnDegS"]["t"], s["peakYawRateDegS"]["value"], s["peakYawRateDegS"]["t"],
                        s["totalYawDeg"], s["pitchDeg"]["min"], s["pitchDeg"]["max"], s["pitchDeg"]["minAt"], s["closestAimM"]["value"],
                        s["closestAimM"]["t"]))
        L.append("  resident facing %s (%s)" % (s["facing"]["value"], s["facing"]["from"]))
        cf = s.get("comfort") or {}
        if cf:
            L.append("  comfort budget: " + json.dumps({k: v for k, v in cf.items() if k != "checks"}))
            for c in cf.get("checks", []):
                L.append("    %-4s %s" % ("PASS" if c["pass"] else "FAIL", c["rule"]))
        L.append("  camera inside a box: %d" % len(s["inside"]))
        for e in s["inside"]:
            L.append("    %-28s %-8s t %.2f-%.2f s, max %.1f cm deep, at %s" % (e["object"], e["kind"], e["t0"], e["t1"], e["maxDepthCm"], e["pos"]))
        L.append("  path passes through a surface: %d" % len(s["passesThrough"]))
        for e in s["passesThrough"]:
            L.append("    %-28s t %.2f s at %s" % (e["object"], e["t"], e["pos"]))
        L.append("  min clearance %.1f cm to %s at t %.2f s %s" % (s["minClearance"]["cm"], s["minClearance"]["object"], s["minClearance"]["t"],
                                                                s["minClearance"]["pos"]))
        for e in s["nearMisses"]:
            L.append("    %5.1f cm  %-28s t %.2f s at %s%s" % (e["cm"], e["object"], e["t"], e["pos"], "  (inside the 10 cm near plane)" if e["insideNearPlane"] else ""))
        L.append("  frames (%s sampling):" % s["sampling"])
        for i, f in enumerate(s["frames"]):
            L.append("    %d  t %5.2f  s %5.2f m  pos (%6.0f,%6.0f,%4.0f)  yaw %6.1f pitch %5.1f  %4.0f mm shift (%5.2f,%5.2f)  %5.2f m/s  turn %4.1f deg/s  "
                     "eyes %s  head %s px  headroom %s  clear %5.1f cm %s%s"
                     % (i + 1, f["t"], f["sM"], f["pos"][0], f["pos"][1], f["pos"][2], f["yaw"], f["pitch"], f.get("focalMm", 0),
                        f.get("shiftMm", [0, 0])[0], f.get("shiftMm", [0, 0])[1], f["speedMs"], f["turnDegS"],
                        f.get("eyesUV"), f.get("facePx"), f.get("headroom"),
                        f["clearanceCm"], f["nearest"], ("  [" + "; ".join(f["flags"]) + "]") if f.get("flags") else ""))
            if f.get("census"):
                L.append("       " + census_text(f["census"]))
            vs = f.get("valueStructure")
            if vs:
                L.append("       value structure: ceiling panels %s (n %d) vs floor %s (n %d), display medians -> %s"
                         % (vs["ceiling"]["medianDisplay"], vs["ceiling"]["n"], vs["floor"]["medianDisplay"], vs["floor"]["n"],
                            {True: "PASS (ceiling at or above the floor)", False: "FAIL (ceiling darker than the floor)",
                             None: "n/a"}[vs["pass"]]))
    L.append("")
    L.append("renders: %s" % json.dumps(rep.get("renders", {}), indent=1))
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(1)
