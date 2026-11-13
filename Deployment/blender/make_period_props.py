#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 LifeScope INC, DBA Companion Intelligence (https://ci.computer)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This script imports Blender's Python API (bpy), so it is licensed under the GNU General Public
# License v3.0 or later (LICENSES/GPL-3.0-or-later.txt), not under the PolyForm Noncommercial
# licence that covers the rest of this repository. NOTICE.md explains why. The GPL does not
# extend to the GLB files the script writes.
"""Builds the CI-authored machines that replace the scavenged props the set could not keep, and
exports each under the file name Deployment/author_diorama.py already loads. Blender, headless.

    blender --background --python Deployment/blender/make_period_props.py -- --out <dir> [--only NAME]

With no --out it writes into $MHLLM_GLB_DIR (the variable author_diorama.py resolves the prop
directory through, as make_ibm729.py does) and falls back to the current directory. --only builds
just the named output(s); it may be repeated.

    ibm-1401-cpu.glb        IBM 1401 Processing Unit, 4-cube (1959)
    ibm-3178-terminal.glb   IBM 3277 Display Station Model 2 with keyboard (1971)
    ibm-3350-disk.glb       IBM 3350 dual-drive disk storage unit (1975)

WHY THESE ARE MODELLED RATHER THAN SCAVENGED. The Sketchfab models that used to fill these three
slots (maxdragon's "IBM 1401 Central Processing Unit", "IBM 3178" and "IBM 3350 'Madrid'") are
CC BY 4.0 but carry Sketchfab's NoAI tag: their pages emit <meta name="robots" content="index,
noAI">, and Sketchfab's Terms of Use prohibit using NoAI-tagged content in datasets for, in the
development of, or as inputs to generative AI programs (Sketchfab Help Center article 16152133).
This product is an AI companion, so the assets were replaced rather than argued over. CREDITS.md
records the audit.

THE TERMINAL SLOT NOW HOLDS A 3277, NOT A 3178. The file keeps the name the set loads, but the 3178
is a 1983 terminal and the room is a 1970s machine room. The 3277 Model 2 is the 3270-family
display that a 1956-1975 installation actually had on its desks.

GEOMETRY IS SPEC, NOT TASTE, as in make_ibm729.py. Each envelope is a published measurement written
as a named constant, and the script fails if the exported bounds drift from it:
  * 1401: Ingenium (Canada Science and Technology Museum) artifact 1979.0133.001, an IBM 1401
    processing unit -- width 147.5 cm, length 78.0 cm, height 152.7 cm. The 2x2 "4-cube" front and
    the console in the upper-left cube follow Figure 8 of IBM D24-1401-1, "General Information
    Manual: IBM 1401" (1960). The palette follows the Ingenium description: textured grey casing,
    brushed-metal trim, grey ridged front panels, a blue enamel band across the top, and a grey
    console with multicoloured controls.
  * 3277: IBM SY27-2349-4, "3270 Information Display System Physical Planning" (1978), Section 5 --
    Model 2 display 16 x 21 x 19 in; the keyboard adds 8-1/2 in to the front and the 78-key
    keyboard is 18 in wide. The rounded bezel, offset screen and separate low keyboard follow
    Figures 1-2, 2-7 and 2-8 of IBM GA27-2739-4, "An Introduction to the IBM 3270" (1973).
  * 3350: IBM GC22-7064-10, "Input/Output Equipment Installation Manual-Physical Planning" (1986),
    section 3350.2 -- dual-drive unit 42 x 33-1/2 x 46-1/2 in, plus a 3-in end cover on each exposed
    side, because every cabinet in the set stands free. The front (one door, smoked window and
    top-mounted operator panel per drive) follows Figure 1 of IBM GA26-1638-2, "Reference Manual
    for IBM 3350 Direct Access Storage" (1977). The operator-panel controls follow GA26-1638-0
    (1975): Ready lamp, Attention pushbutton, R/W-Read switch and Start/Stop switch.
Detail inside each envelope (frame widths, panel splits, control positions) is read off those
figures and is marked "from Fig." rather than presented as a published figure.

Every machine faces -Y, like the 729 and like the models these replace. The glTF exporter converts
-Y to +Z, so the yaw values in author_diorama.py's PROPS and HALL tables are still valid. Each GLB
holds ONE mesh object with one material slot per surface: import_prop() takes the first StaticMesh
it finds, so a second object would leave the placed actor showing only part of the machine.

NO TEXTURES, deliberately, as in make_ibm729.py: flat PBR values only. author_diorama.py re-parents
prop materials onto M_PropGLTF, which keeps BaseColorFactor, MetallicFactor and RoughnessFactor
but not emission. Lamps and phosphor therefore get a bright BASE colour as well as an emissive one,
so they still read as lit in the cooked build.

Each GLB records its provenance in the asset block (asset.copyright) and in the scene extras, so a
future audit can identify the file from the file alone.
"""
import argparse
import math
import os
import sys

import bpy
import bmesh

COPYRIGHT = ("Copyright LifeScope INC, DBA Companion Intelligence (https://ci.computer). "
             "CI-authored; generated by CI-MetaHuman Deployment/blender/make_period_props.py.")
SPEC_TOLERANCE = 0.0005      # metres: exported bounds must match the published envelope this closely

# =============================================================================================
# IBM 1401 Processing Unit, 4-cube  ->  ibm-1401-cpu.glb
# =============================================================================================
P14_W, P14_D, P14_H = 1.475, 0.780, 1.527     # Ingenium 1979.0133.001 (width, length, height)
P14_PLINTH_H, P14_PLINTH_IN = 0.070, 0.035    # recessed toe kick (from Fig. 8)
P14_FRAME_W = 0.028          # brushed-metal stiles and rails around each cube (from Fig. 8)
P14_FRAME_DEPTH = 0.035      # the frames stand this far proud of the casing
P14_PANEL_T = 0.008          # panels sit proud of the casing, behind the frame face
P14_MID_Z = 0.805            # rail between lower and upper cubes (from Fig. 8: ~0.53 H)
P14_KICK_H = 0.065           # light kick strip under each lower panel
P14_HEADER_H = 0.105         # blue enamel band across the top of the front and back
P14_CONSOLE_W = 0.405        # console column in the upper-left cube (from Fig. 8)
P14_CONSOLE_Z0 = 1.035       # console bottom edge; the console runs up to the top rail
P14_RIDGE_PITCH, P14_RIDGE_W, P14_RIDGE_T = 0.045, 0.010, 0.004   # "grey ridged synthetic"
P14_LAMP_ROWS, P14_LAMP_COLS = 4, 9      # register lamp field
P14_KNOBS = 7                            # rotary switch row (from Fig. 8)

P14_MATERIALS = [
    # name,            base colour,          metallic, roughness, emission colour, strength
    ("M_1401_Shell",   (0.66, 0.66, 0.64), 0.00, 0.70, None, 1.0),   # textured grey casing
    ("M_1401_Trim",    (0.72, 0.73, 0.75), 0.85, 0.32, None, 1.0),   # brushed-metal frames
    ("M_1401_Panel",   (0.30, 0.31, 0.32), 0.00, 0.80, None, 1.0),   # grey ridged synthetic
    ("M_1401_PanelL",  (0.72, 0.72, 0.70), 0.00, 0.75, None, 1.0),   # light panel, kick strips
    ("M_1401_Blue",    (0.09, 0.28, 0.62), 0.00, 0.35, None, 1.0),   # blue enamel band
    ("M_1401_Console", (0.58, 0.59, 0.58), 0.00, 0.55, None, 1.0),   # grey console
    ("M_1401_Dark",    (0.05, 0.05, 0.06), 0.00, 0.30, None, 1.0),   # windows, lamp plate, plinth
    ("M_1401_Lamp",    (1.00, 0.86, 0.55), 0.00, 0.40, (1.00, 0.80, 0.45), 3.0),
    ("M_1401_LampR",   (0.90, 0.12, 0.08), 0.00, 0.40, (1.00, 0.15, 0.08), 3.0),   # check lamps
    ("M_1401_Knob",    (0.04, 0.04, 0.045), 0.00, 0.45, None, 1.0),
    ("M_1401_Button",  (0.88, 0.87, 0.82), 0.00, 0.40, None, 1.0),
]
(S14_SHELL, S14_TRIM, S14_PANEL, S14_PANEL_L, S14_BLUE, S14_CONSOLE, S14_DARK, S14_LAMP,
 S14_LAMP_R, S14_KNOB, S14_BUTTON) = range(len(P14_MATERIALS))

# =============================================================================================
# IBM 3277 Display Station Model 2 with keyboard  ->  ibm-3178-terminal.glb
# =============================================================================================
T77_DISP_W, T77_DISP_D, T77_DISP_H = 0.406, 0.533, 0.483   # 16 x 21 x 19 in (SY27-2349-4)
T77_KB_ADD = 0.216            # keyboard adds 8-1/2 in to the front of the display
T77_KB_W = 0.457              # 78-key keyboard, 18 in wide
T77_KB_D = 0.200              # case depth; the rest of the 8-1/2 in is the gap to the display
T77_KB_Z_FRONT, T77_KB_Z_BACK = 0.042, 0.082   # sloped key bed (from GA27-2739-4 Fig. 1-2)
T77_BASE_H = 0.075            # recessed pedestal under the display head (from Fig. 1-2)
T77_CORNER_R = 0.045          # rounded head and bezel corners (from Figs. 2-7 and 2-8)
T77_BEZEL_T = 0.035           # bezel depth in front of the housing
T77_SCREEN_W, T77_SCREEN_H, T77_SCREEN_R = 0.290, 0.225, 0.028   # opening in the bezel
T77_SCREEN_CX, T77_SCREEN_CZ = -0.030, 0.300   # set left and high: indicators sit to the right
T77_SCREEN_RECESS = 0.0165    # CRT face behind the bezel front
T77_TEXT_ROWS = 8             # phosphor text lines on the screen
T77_KEY = 0.016               # keycap footprint
T77_KEY_PITCH = 0.0195
T77_MAIN_KEYS = 14            # keys per row in the main block

T77_MATERIALS = [
    ("M_3277_Case",     (0.80, 0.78, 0.71), 0.00, 0.55, None, 1.0),   # IBM off-white
    ("M_3277_Bezel",    (0.86, 0.85, 0.80), 0.00, 0.50, None, 1.0),
    ("M_3277_Base",     (0.36, 0.37, 0.38), 0.00, 0.60, None, 1.0),
    ("M_3277_Screen",   (0.015, 0.035, 0.030), 0.00, 0.12, (0.010, 0.050, 0.035), 1.0),
    # CI mint #58ebbf, which is also period green phosphor. Linear values, so the exported
    # baseColorFactor renders as #58ebbf; see Docs/SCENE-AUTHORING.md on authoring in sRGB.
    ("M_3277_Phosphor", (0.098, 0.831, 0.521), 0.00, 0.40, (0.098, 0.831, 0.521), 3.0),
    ("M_3277_KeyD",     (0.10, 0.10, 0.11), 0.00, 0.50, None, 1.0),
    ("M_3277_KeyL",     (0.62, 0.62, 0.60), 0.00, 0.50, None, 1.0),
    ("M_3277_Glass",    (0.04, 0.04, 0.05), 0.00, 0.20, None, 1.0),
    ("M_3277_Lamp",     (1.00, 0.62, 0.15), 0.00, 0.40, (1.00, 0.62, 0.15), 3.0),
    ("M_3277_Knob",     (0.05, 0.05, 0.05), 0.00, 0.40, None, 1.0),
]
(S77_CASE, S77_BEZEL, S77_BASE, S77_SCREEN, S77_PHOSPHOR, S77_KEY_D, S77_KEY_L, S77_GLASS,
 S77_LAMP, S77_KNOB) = range(len(T77_MATERIALS))

# =============================================================================================
# IBM 3350 dual-drive disk storage unit  ->  ibm-3350-disk.glb
# =============================================================================================
D50_UNIT_W = 1.067            # 42 in dual-drive unit (GC22-7064-10, 3350.2)
D50_END_COVER = 0.076         # 3 in end cover (ibid.), on BOTH sides: the cabinets stand alone
D50_W = D50_UNIT_W + 2 * D50_END_COVER
D50_D, D50_H = 0.851, 1.181   # 33-1/2 in deep, 46-1/2 in high
D50_PLINTH_H, D50_PLINTH_IN = 0.055, 0.030
D50_DOOR_GAP = 0.010          # each drive's door stops this far short of the drive's edge
D50_DOOR_Z1 = 0.900           # door top; the smoked window sits above it (from Fig. 1)
D50_WINDOW_W, D50_WINDOW_Z0, D50_WINDOW_Z1, D50_WINDOW_R = 0.440, 0.930, 1.090, 0.030
D50_TOP_Z0 = D50_H - 0.050    # casing top; a chamfer climbs from here to the full height
D50_CHAMFER_D = 0.105         # depth of the sloped front edge carrying the operator panels
D50_SHELL_INSET = 0.020       # casing front face behind the end-cover front
D50_DOOR_T = 0.014            # door thickness, proud of the casing

D50_MATERIALS = [
    ("M_3350_Shell",  (0.80, 0.80, 0.77), 0.00, 0.50, None, 1.0),   # pebble-grey casing
    ("M_3350_Blue",   (0.09, 0.28, 0.62), 0.00, 0.40, None, 1.0),   # the 729's IBM blue
    ("M_3350_Glass",  (0.05, 0.05, 0.06), 0.00, 0.12, None, 1.0),   # smoked window
    ("M_3350_Dark",   (0.08, 0.08, 0.09), 0.00, 0.60, None, 1.0),   # plinth
    ("M_3350_Panel",  (0.14, 0.15, 0.16), 0.00, 0.45, None, 1.0),   # operator panel housing
    ("M_3350_Trim",   (0.62, 0.63, 0.65), 0.85, 0.25, None, 1.0),   # toggle switches
    ("M_3350_Lamp",   (0.95, 0.95, 0.88), 0.00, 0.40, (0.95, 0.95, 0.88), 3.0),   # Ready
    ("M_3350_Amber",  (1.00, 0.62, 0.15), 0.00, 0.40, (1.00, 0.62, 0.15), 3.0),   # Attention
    ("M_3350_Badge",  (0.70, 0.71, 0.72), 0.60, 0.30, None, 1.0),
]
(S50_SHELL, S50_BLUE, S50_GLASS, S50_DARK, S50_PANEL, S50_TRIM, S50_LAMP, S50_AMBER,
 S50_BADGE) = range(len(D50_MATERIALS))


# =============================================================================================
# Geometry helpers. Every helper returns (verts, faces) for a CLOSED solid, so recalculating
# normals per island always finds an outside.
# =============================================================================================
def box(x0, x1, y0, y1, z0, z1):
    """Axis-aligned box from explicit bounds, so the numbers in this file ARE the numbers in the
    exported bounding box."""
    assert x0 < x1 and y0 < y1 and z0 < z1, (x0, x1, y0, y1, z0, z1)
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (2, 3, 7, 6), (1, 2, 6, 5), (3, 0, 4, 7)]
    return v, f


def slope_box(x0, x1, y0, y1, z0, z_front, z_back):
    """A box whose top rises from z_front at its front (y0) to z_back at its back (y1): a
    keyboard case, a console desk, a sloped cabinet edge."""
    assert x0 < x1 and y0 < y1 and z_front > z0 and z_back > z0
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z_front), (x1, y0, z_front), (x1, y1, z_back), (x0, y1, z_back)]
    f = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (2, 3, 7, 6), (1, 2, 6, 5), (3, 0, 4, 7)]
    return v, f


def cyl_y(cx, cz, radius, y0, y1, segments=12):
    """A cylinder on the Y axis: a knob facing out of the front. `segments` is a multiple of 4
    so the silhouette's extremes land on the axes."""
    v, f = [], []
    for i in range(segments):
        a = math.tau * i / segments
        x, z = cx + radius * math.cos(a), cz + radius * math.sin(a)
        v.append((x, y0, z))
        v.append((x, y1, z))
    for i in range(segments):
        j = (i + 1) % segments
        f.append((2 * i, 2 * j, 2 * j + 1, 2 * i + 1))
    f.append(tuple(2 * i for i in range(segments)))
    f.append(tuple(2 * i + 1 for i in range(segments - 1, -1, -1)))
    return v, f


def rrect(cx, cz, width, height, radius, steps=3):
    """Rounded-rectangle outline in the XZ plane (the front view), `steps` segments per corner."""
    assert width > 2 * radius and height > 2 * radius
    corners = ((cx + width / 2 - radius, cz - height / 2 + radius, -90.0),
               (cx + width / 2 - radius, cz + height / 2 - radius, 0.0),
               (cx - width / 2 + radius, cz + height / 2 - radius, 90.0),
               (cx - width / 2 + radius, cz - height / 2 + radius, 180.0))
    pts = []
    for ccx, ccz, a0 in corners:
        for k in range(steps + 1):
            a = math.radians(a0 + 90.0 * k / steps)
            pts.append((ccx + radius * math.cos(a), ccz + radius * math.sin(a)))
    return pts


def prism_y(outline, y0, y1):
    """Extrude an XZ outline from y0 (front) to y1 (back)."""
    n = len(outline)
    v = [(x, y0, z) for x, z in outline] + [(x, y1, z) for x, z in outline]
    f = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    f.append(tuple(range(n - 1, -1, -1)))
    f.append(tuple(range(n, 2 * n)))
    return v, f


def ring_y(outer, inner, y0, y1):
    """A frame: the region between two outlines with the same vertex count (a bezel), extruded."""
    assert len(outer) == len(inner)
    n = len(outer)
    v = ([(x, y0, z) for x, z in outer] + [(x, y1, z) for x, z in outer] +
         [(x, y0, z) for x, z in inner] + [(x, y1, z) for x, z in inner])
    of, ob, inf, inb = 0, n, 2 * n, 3 * n
    f = []
    for i in range(n):
        j = (i + 1) % n
        f.append((of + i, of + j, ob + j, ob + i))        # outer wall
        f.append((inf + j, inf + i, inb + i, inb + j))    # inner wall
        f.append((of + j, of + i, inf + i, inf + j))      # front face
        f.append((ob + i, ob + j, inb + j, inb + i))      # back face
    return v, f


def ridges(x0, x1, z0, z1, y_face, pitch, width, depth):
    """Vertical ribs standing `depth` proud of a panel face at y_face: ridged panels and grilles."""
    out = []
    count = int((x1 - x0 - width) / pitch) + 1
    start = (x0 + x1) / 2 - (count - 1) * pitch / 2
    for i in range(count):
        cx = start + i * pitch
        out.append(box(cx - width / 2, cx + width / 2, y_face - depth, y_face, z0, z1))
    return out


# =============================================================================================
# Blender plumbing (same contract as make_ibm729.py)
# =============================================================================================
def make_material(name, base, metallic, roughness, emission, strength):
    mat = bpy.data.materials.new(name)
    if bpy.app.version < (5, 0, 0):
        mat.use_nodes = True          # always on from Blender 5, where setting it is deprecated
    # Look the shader up by TYPE: node names are localized.
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = (base[0], base[1], base[2], 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission is not None:
        bsdf.inputs["Emission Color"].default_value = (emission[0], emission[1], emission[2], 1.0)
        bsdf.inputs["Emission Strength"].default_value = strength
    mat.diffuse_color = (base[0], base[1], base[2], 1.0)    # viewport only
    return mat


def build_mesh(name, parts):
    """parts: list of (verts, faces, material_index). Emits ONE mesh with per-face material
    indices, so the GLB carries one primitive per material and Unreal imports a single
    StaticMesh with slots."""
    verts, faces, mat_idx = [], [], []
    for pv, pf, mi in parts:
        base = len(verts)
        verts.extend(pv)
        for face in pf:
            faces.append(tuple(base + i for i in face))
            mat_idx.append(mi)

    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    expected = len(me.polygons)
    me.validate()
    if len(me.polygons) != expected:
        # A dropped face would silently shift every later material index by one.
        raise RuntimeError("%s: validate() removed %d faces" % (name, expected - len(me.polygons)))
    me.polygons.foreach_set("material_index", mat_idx)

    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()

    # TEXCOORD_0 even with no textures: the glTF importer and UE's lightmap builder expect it.
    uv = me.uv_layers.new(name="UVMap")
    for poly in me.polygons:
        nx, ny, nz = (abs(c) for c in poly.normal)
        for li in poly.loop_indices:
            x, y, z = me.vertices[me.loops[li].vertex_index].co
            if nx >= ny and nx >= nz:
                uv.data[li].uv = (y, z)
            elif ny >= nz:
                uv.data[li].uv = (x, z)
            else:
                uv.data[li].uv = (x, y)
    me.update()
    return me


def add_object(name, mesh, materials):
    for m in materials:
        mesh.materials.append(m)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# =============================================================================================
# IBM 1401 Processing Unit
# =============================================================================================
def build_1401():
    hx, hy, h = P14_W / 2, P14_D / 2, P14_H
    fw, ph = P14_FRAME_W, P14_PLINTH_H
    yf = -hy                          # frame face: the front of the bounding box
    ys = yf + P14_FRAME_DEPTH         # casing front face
    yp = ys - P14_PANEL_T             # panel face
    parts = []

    def add(geom, mat):
        parts.append((geom[0], geom[1], mat))

    add(box(-hx + P14_PLINTH_IN, hx - P14_PLINTH_IN, -hy + P14_PLINTH_IN, hy - P14_PLINTH_IN,
            0.0, ph), S14_DARK)
    add(box(-hx, hx, ys, -ys, ph, h), S14_SHELL)

    lo_z0, lo_z1 = ph + fw, P14_MID_Z - fw / 2      # lower cube opening
    up_z0, up_z1 = P14_MID_Z + fw / 2, h - fw       # upper cube opening
    left_x0, left_x1 = -hx + fw, -fw / 2
    right_x0, right_x1 = fw / 2, hx - fw
    header_z0 = up_z1 - P14_HEADER_H

    for face_sign in (-1, 1):                       # -1 front, +1 back
        # Frame members span frame face -> casing face on this side.
        y0, y1 = (yf, ys) if face_sign < 0 else (-ys, hy)
        for x0, x1 in ((-hx, -hx + fw), (hx - fw, hx), (-fw / 2, fw / 2)):
            add(box(x0, x1, y0, y1, ph, h), S14_TRIM)
        for z0, z1 in ((ph, ph + fw), (P14_MID_Z - fw / 2, P14_MID_Z + fw / 2), (h - fw, h)):
            add(box(-hx, hx, y0, y1, z0, z1), S14_TRIM)

    # ---- back: blue band over plain grey panels (Ingenium: blue on front AND back) ----------
    pb0, pb1 = -ys, -ys + P14_PANEL_T
    for x0, x1 in ((left_x0, left_x1), (right_x0, right_x1)):
        add(box(x0, x1, pb0, pb1, lo_z0, lo_z1), S14_PANEL)
        add(box(x0, x1, pb0, pb1, up_z0, header_z0), S14_PANEL)
        add(box(x0, x1, pb0, pb1, header_z0, up_z1), S14_BLUE)

    # ---- front, lower cubes: ridged grey panels over light kick strips ----------------------
    for x0, x1 in ((left_x0, left_x1), (right_x0, right_x1)):
        add(box(x0, x1, yp, ys, lo_z0, lo_z0 + P14_KICK_H), S14_PANEL_L)
        add(box(x0, x1, yp, ys, lo_z0 + P14_KICK_H, lo_z1), S14_PANEL)
        for g in ridges(x0 + 0.02, x1 - 0.02, lo_z0 + P14_KICK_H + 0.012, lo_z1 - 0.012,
                        yp, P14_RIDGE_PITCH, P14_RIDGE_W, P14_RIDGE_T):
            add(g, S14_PANEL)

    # ---- front, upper-right cube and the right column of the upper-left cube ----------------
    console_x1 = left_x0 + P14_CONSOLE_W
    divider = 0.014
    for x0, x1 in ((console_x1 + divider, left_x1), (right_x0, right_x1)):
        add(box(x0, x1, yp, ys, header_z0, up_z1), S14_BLUE)
        add(box(x0, x1, yp, ys, up_z0, header_z0), S14_PANEL)
        for g in ridges(x0 + 0.02, x1 - 0.02, up_z0 + 0.012, header_z0 - 0.012,
                        yp, P14_RIDGE_PITCH, P14_RIDGE_W, P14_RIDGE_T):
            add(g, S14_PANEL)
    # "IBM 1401" nameplate at the top right of the upper-right panel (Fig. 8).
    add(box(right_x1 - 0.200, right_x1 - 0.035, yp - 0.004, yp,
            header_z0 - 0.065, header_z0 - 0.025), S14_TRIM)
    # Trim between the console column and the panel beside it, and under the console.
    add(box(console_x1, console_x1 + divider, yf + 0.012, ys, up_z0, up_z1), S14_TRIM)
    add(box(left_x0, console_x1, yf + 0.012, ys, P14_CONSOLE_Z0 - 0.012, P14_CONSOLE_Z0),
        S14_TRIM)

    # ---- console column: light ridged panel below, operator's console above ------------------
    add(box(left_x0, console_x1, yp, ys, up_z0, P14_CONSOLE_Z0 - 0.012), S14_PANEL_L)
    for g in ridges(left_x0 + 0.02, console_x1 - 0.02, up_z0 + 0.012,
                    P14_CONSOLE_Z0 - 0.024, yp, P14_RIDGE_PITCH, P14_RIDGE_W, P14_RIDGE_T):
        add(g, S14_PANEL_L)
    yc = ys - 0.004                                  # console face
    add(box(left_x0, console_x1, yc, ys, P14_CONSOLE_Z0, up_z1), S14_CONSOLE)
    cx0, cx1 = left_x0 + 0.020, console_x1 - 0.020   # usable console width
    cw = cx1 - cx0
    # three register display windows across the top
    for i in range(3):
        x = cx0 + cw * (i + 0.5) / 3
        add(box(x - 0.035, x + 0.035, yc - 0.002, yc, up_z1 - 0.070, up_z1 - 0.030), S14_DARK)
    # register lamp field: a dark plate with rows of lamps; the last column is the red check row
    plate_z0, plate_z1 = up_z1 - 0.245, up_z1 - 0.085
    add(box(cx0, cx1, yc - 0.003, yc, plate_z0, plate_z1), S14_DARK)
    for r in range(P14_LAMP_ROWS):
        z = plate_z0 + (plate_z1 - plate_z0) * (r + 0.5) / P14_LAMP_ROWS
        for c in range(P14_LAMP_COLS):
            x = cx0 + 0.020 + (cw - 0.040) * c / (P14_LAMP_COLS - 1)
            if c == P14_LAMP_COLS - 1:
                mat = S14_LAMP_R if r % 2 == 0 else S14_DARK
            else:
                # A deterministic mix of lit and dark bits: a register holding a value, not a
                # test pattern with every lamp on.
                mat = S14_LAMP if (r * 7 + c * 3) % 5 not in (0, 3) else S14_KNOB
            add(box(x - 0.007, x + 0.007, yc - 0.009, yc - 0.003, z - 0.007, z + 0.007), mat)
    # rotary switch row; the left-most is the larger mode switch
    knob_z = plate_z0 - 0.065
    for k in range(P14_KNOBS):
        x = cx0 + 0.030 + (cw - 0.060) * k / (P14_KNOBS - 1)
        add(cyl_y(x, knob_z, 0.024 if k == 0 else 0.016, yc - 0.024, yc), S14_KNOB)
    # start / stop / reset keys and a row of toggles
    key_z0, key_z1 = P14_CONSOLE_Z0 + 0.030, P14_CONSOLE_Z0 + 0.075
    for i, mat in enumerate((S14_BUTTON, S14_LAMP_R, S14_KNOB)):
        x = cx0 + 0.035 + i * 0.060
        add(box(x - 0.024, x + 0.024, yc - 0.012, yc, key_z0, key_z1), mat)
    for i in range(3):
        x = cx0 + 0.230 + i * 0.045
        add(box(x - 0.006, x + 0.006, yc - 0.016, yc, key_z0 + 0.008, key_z1 - 0.008), S14_TRIM)

    return "IBM1401_ProcessingUnit", parts, (P14_W, P14_D, P14_H)


# =============================================================================================
# IBM 3277 Display Station Model 2
# =============================================================================================
def build_3277():
    total_d = T77_DISP_D + T77_KB_ADD
    y_front, y_back = -total_d / 2, total_d / 2
    disp_y0 = y_front + T77_KB_ADD                   # bezel front face
    parts = []

    def add(geom, mat):
        parts.append((geom[0], geom[1], mat))

    # ---- display: recessed pedestal, rounded head, bezel with an offset screen -------------
    add(box(-0.165, 0.165, disp_y0 + 0.060, y_back - 0.050, 0.0, T77_BASE_H), S77_BASE)
    head_h = T77_DISP_H - T77_BASE_H
    head_cz = T77_BASE_H + head_h / 2
    outline = rrect(0.0, head_cz, T77_DISP_W, head_h, T77_CORNER_R)
    add(prism_y(outline, disp_y0 + T77_BEZEL_T, y_back), S77_CASE)
    opening = rrect(T77_SCREEN_CX, T77_SCREEN_CZ, T77_SCREEN_W, T77_SCREEN_H, T77_SCREEN_R)
    add(ring_y(outline, opening, disp_y0, disp_y0 + T77_BEZEL_T), S77_BEZEL)
    screen_y = disp_y0 + T77_SCREEN_RECESS
    add(prism_y(rrect(T77_SCREEN_CX, T77_SCREEN_CZ, T77_SCREEN_W + 0.010, T77_SCREEN_H + 0.010,
                      T77_SCREEN_R + 0.005), screen_y, disp_y0 + T77_BEZEL_T), S77_SCREEN)
    # Phosphor text: a screen that is merely dark green reads as switched off.
    x_left = T77_SCREEN_CX - T77_SCREEN_W / 2 + 0.020
    top = T77_SCREEN_CZ + T77_SCREEN_H / 2 - 0.030
    lengths = (0.240, 0.180, 0.205, 0.090, 0.225, 0.150, 0.115, 0.195)
    for i in range(T77_TEXT_ROWS):
        z = top - i * 0.021
        add(box(x_left, x_left + lengths[i % len(lengths)], screen_y - 0.0012, screen_y,
                z - 0.0035, z + 0.0035), S77_PHOSPHOR)
    # Indicator windows in the wide right-hand bezel margin; one lit.
    ind_x = (T77_SCREEN_CX + T77_SCREEN_W / 2 + T77_DISP_W / 2) / 2
    for i in range(3):
        z = T77_SCREEN_CZ + 0.090 - i * 0.032
        add(box(ind_x - 0.016, ind_x + 0.016, disp_y0 - 0.002, disp_y0, z - 0.006, z + 0.006),
            S77_LAMP if i == 0 else S77_GLASS)
    # Brightness knob at the lower left of the bezel (Fig. 2-7).
    add(cyl_y(T77_SCREEN_CX - T77_SCREEN_W / 2 + 0.010, T77_BASE_H + 0.050, 0.012,
              disp_y0 - 0.012, disp_y0), S77_KNOB)

    # ---- keyboard: low sloped case, dark main block, light numeric/function pad -------------
    kb_y0, kb_y1 = y_front, y_front + T77_KB_D
    add(slope_box(-T77_KB_W / 2, T77_KB_W / 2, kb_y0, kb_y1, 0.0, T77_KB_Z_FRONT, T77_KB_Z_BACK),
        S77_CASE)

    def bed_z(y):
        return T77_KB_Z_FRONT + (y - kb_y0) / (kb_y1 - kb_y0) * (T77_KB_Z_BACK - T77_KB_Z_FRONT)

    def key(cx, cy, w, mat):
        z = bed_z(cy)
        add(box(cx - w / 2, cx + w / 2, cy - T77_KEY / 2, cy + T77_KEY / 2, z - 0.004, z + 0.009),
            mat)

    rows_y = [kb_y0 + 0.058 + i * 0.027 for i in range(4)]
    main_x0 = -T77_KB_W / 2 + 0.024
    for r, cy in enumerate(rows_y):
        for k in range(T77_MAIN_KEYS):
            key(main_x0 + r * 0.005 + k * T77_KEY_PITCH + T77_KEY / 2, cy, T77_KEY, S77_KEY_D)
    key(main_x0 + 0.155, kb_y0 + 0.028, 0.150, S77_KEY_D)            # space bar
    pad_x0 = T77_KB_W / 2 - 0.024 - 3 * T77_KEY_PITCH + (T77_KEY_PITCH - T77_KEY)
    for cy in rows_y:
        for k in range(3):
            key(pad_x0 + k * T77_KEY_PITCH + T77_KEY / 2, cy, T77_KEY, S77_KEY_L)

    return "IBM3277_DisplayStation", parts, (T77_KB_W, total_d, T77_DISP_H)


# =============================================================================================
# IBM 3350 dual-drive unit
# =============================================================================================
def build_3350():
    hx, ux, hy, h = D50_W / 2, D50_UNIT_W / 2, D50_D / 2, D50_H
    ys = -hy + D50_SHELL_INSET           # casing front face
    yd = ys - D50_DOOR_T                 # door face
    ch_y1 = ys + D50_CHAMFER_D           # top of the sloped front edge
    lip = 0.006
    parts = []

    def add(geom, mat):
        parts.append((geom[0], geom[1], mat))

    def chamfer_z(y):
        return D50_TOP_Z0 + lip + (y - ys) / (ch_y1 - ys) * (h - D50_TOP_Z0 - lip)

    for x0, x1 in ((-hx, -ux), (ux, hx)):                    # end covers, floor to top
        add(box(x0, x1, -hy, hy, 0.0, h), S50_SHELL)
    add(box(-ux, ux, -hy + D50_PLINTH_IN, hy - D50_PLINTH_IN, 0.0, D50_PLINTH_H), S50_DARK)
    add(box(-ux, ux, ys, hy, D50_PLINTH_H, D50_TOP_Z0), S50_SHELL)
    add(box(-ux, ux, ch_y1, hy, D50_TOP_Z0, h), S50_SHELL)
    add(slope_box(-ux, ux, ys, ch_y1, D50_TOP_Z0, D50_TOP_Z0 + lip, h), S50_SHELL)

    for dx0, dx1 in ((-ux, 0.0), (0.0, ux)):                 # one drive per half
        cx = (dx0 + dx1) / 2
        add(box(dx0 + D50_DOOR_GAP, dx1 - D50_DOOR_GAP, yd, ys,
                D50_PLINTH_H + 0.012, D50_DOOR_Z1), S50_BLUE)
        add(box(cx - 0.035, cx + 0.035, yd - 0.003, yd, D50_DOOR_Z1 - 0.065,
                D50_DOOR_Z1 - 0.025), S50_BADGE)
        win = rrect(cx, (D50_WINDOW_Z0 + D50_WINDOW_Z1) / 2, D50_WINDOW_W,
                    D50_WINDOW_Z1 - D50_WINDOW_Z0, D50_WINDOW_R)
        add(prism_y(win, ys - 0.008, ys), S50_GLASS)
        # Operator panel on the sloped top edge: Ready lamp, Attention pushbutton, R/W-Read
        # switch, Start/Stop switch (GA26-1638-0), in a dark housing that follows the slope.
        py0, py1 = ys + 0.010, ys + 0.070
        add(slope_box(cx - 0.100, cx + 0.100, py0, py1, D50_TOP_Z0,
                      chamfer_z(py0) + 0.012, chamfer_z(py1) + 0.012), S50_PANEL)

        def housing_z(y):
            return chamfer_z(y) + 0.012

        cy = (py0 + py1) / 2
        for ox, mat, tall in ((-0.070, S50_LAMP, 0.006), (-0.035, S50_AMBER, 0.006),
                              (0.010, S50_TRIM, 0.008), (0.050, S50_TRIM, 0.008)):
            z = housing_z(cy)
            add(box(cx + ox - 0.008, cx + ox + 0.008, cy - 0.008, cy + 0.008,
                    z - 0.006, z + tall), mat)

    return "IBM3350_DualDriveUnit", parts, (D50_W, D50_D, D50_H)


PROPS = {
    # output file (the name author_diorama.py loads) -> (builder, materials, what it is)
    "ibm-1401-cpu.glb": (build_1401, P14_MATERIALS, "IBM 1401 Processing Unit, 4-cube (1959)"),
    "ibm-3178-terminal.glb": (build_3277, T77_MATERIALS,
                              "IBM 3277 Display Station Model 2 with keyboard (1971)"),
    "ibm-3350-disk.glb": (build_3350, D50_MATERIALS, "IBM 3350 dual-drive disk storage (1975)"),
}


def export_one(filename, out_dir):
    builder, material_table, label = PROPS[filename]
    # Factory settings, empty: the default camera and lamp would export as extra glTF nodes.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = [make_material(*m) for m in material_table]
    name, parts, spec = builder()
    used = sorted({mi for _, _, mi in parts})
    if used != list(range(len(mats))):
        raise RuntimeError("%s: material slots %s unused" % (name, sorted(set(range(len(mats))) - set(used))))
    obj = add_object(name, build_mesh(name, parts), mats)

    scene = bpy.context.scene
    scene["ci_provenance"] = {
        "model": label,
        "generator": "CI-MetaHuman Deployment/blender/make_period_props.py",
        "authored_by": "LifeScope INC, DBA Companion Intelligence",
    }
    out = os.path.join(out_dir, filename)
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True,
                              export_materials="EXPORT", export_apply=False,
                              export_copyright=COPYRIGHT, export_extras=True)

    co = [obj.matrix_world @ v.co for v in obj.data.vertices]
    lo = [min(v[i] for v in co) for i in range(3)]
    hi = [max(v[i] for v in co) for i in range(3)]
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    print("  %-23s %-24s verts %5d  tris %5d  slots %2d  bounds x[%.4f %.4f] y[%.4f %.4f] z[%.4f %.4f]"
          % (filename, name, len(obj.data.vertices), tris, len(mats),
             lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    size = [hi[i] - lo[i] for i in range(3)]
    drift = [abs(size[i] - spec[i]) for i in range(3)]
    centred = abs(lo[0] + hi[0]) < SPEC_TOLERANCE and abs(lo[1] + hi[1]) < SPEC_TOLERANCE
    if max(drift) > SPEC_TOLERANCE or abs(lo[2]) > SPEC_TOLERANCE or not centred:
        raise RuntimeError("%s: bounds %s drift from spec %s (floor %.4f, centred %s)"
                           % (filename, ["%.4f" % s for s in size], spec, lo[2], centred))
    print("  %-23s spec W %.3f D %.3f H %.3f m: OK    wrote %s (%.1f KB)"
          % ("", spec[0], spec[1], spec[2], out, os.path.getsize(out) / 1024.0))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="make_period_props.py")
    ap.add_argument("--out", default=None, help="directory to write the GLBs into")
    ap.add_argument("--only", action="append", choices=sorted(PROPS), default=None,
                    help="build only this output (repeatable)")
    args = ap.parse_args(argv)

    out_dir = os.path.abspath(args.out or os.environ.get("MHLLM_GLB_DIR") or ".")
    if out_dir.lower().endswith(".glb"):
        ap.error("--out is a directory: the file names are fixed by author_diorama.py")
    os.makedirs(out_dir, exist_ok=True)
    for filename in args.only or sorted(PROPS):
        export_one(filename, out_dir)


if __name__ == "__main__":
    main()
