#!/usr/bin/env python3
"""Builds the IBM 729 tape unit and exports it as ibm-729-tape.glb. Blender, headless.

    blender --background --python Deployment/blender/make_ibm729.py -- --out <dir_or_file>

With no --out it writes ibm-729-tape.glb into $MHLLM_GLB_DIR (the same variable
Deployment/author_diorama.py resolves the prop directory through), and falls back to the
current directory.

WHY THIS PROP IS MODELLED RATHER THAN SCAVENGED. None of the eleven remaining Sketchfab machines
is a 729 tape drive. The 729 is the machine a 1960s room is REMEMBERED by: two reels turning in an
open bay is the image, and nothing else in the set can supply it.

The front is an open, dark-backed reel bay in a hollow shell. The reels are separate root nodes
with their own origins at their hubs. Each has two open-spoke flanges around a dark tape pack, so
rotation reads in motion and in a still instead of looking like a blank coloured disc.

The reel disc lies in the cabinet's local XZ plane, with its axle on local Y. Unreal `Pitch`
therefore rotates it about its hub after the machine is turned to face across the aisle. The
authoring script restores each reel node's hub offset when it becomes a standalone StaticMeshActor;
the source node names are load-bearing because `author_diorama.py` uses them to match placement.

GEOMETRY IS SPEC, NOT TASTE. Every dimension is a real 729 measurement (0.76 x 0.74 x 1.80 m,
10.5-inch reels) and is written as an explicit constant, because the alternative -- eyeballing it
in a viewport -- produced the 29-metre desk terminal that HANDOFF.md still carries a warning about.

NO TEXTURES, deliberately. Flat PBR values only. The set is lit by explicit lights with GI off,
so what sells these cabinets is the SILHOUETTE and the value split between shell, chrome and
the dark reel backing; a diffuse map would add download weight and a licence question for detail
the camera never resolves at the distance the tape rank actually sits.

Palette follows the reference photographs: off-white shell, chrome frame, dark backing in the open
reel bay, IBM-blue lower panel, amber supply reel, white take-up reel, CI-mint status lamps (mint is
also period phosphor -- see Docs/SCENE-AUTHORING.md).
"""
import argparse
import math
import os
import sys

import bpy
import bmesh

# ---------------------------------------------------------------------------------------------
# Dimensions. Blender is Z-up and the cabinet FACES -Y; the glTF exporter converts to Y-up on the
# way out. The reel disc lies in the cabinet's local XZ plane, with its axle on local Y (Unreal Pitch).
# ---------------------------------------------------------------------------------------------
W, D, H = 0.76, 0.74, 1.80          # real 729 envelope, metres
SHELL_X, SHELL_Y = W / 2.0, D / 2.0
FRONT = -SHELL_Y                    # y of the cabinet's front face
TRIM_X = 0.40                       # chrome uprights stand 2cm proud of the shell on each side
TRIM_FRONT, TRIM_BACK = -0.405, 0.3774

REEL_R, REEL_T = 0.135, 0.045       # 10.5-inch reel, 4.5cm across the flange
REEL_Z = 1.26                       # hub height: the bay sits at chest height on the real machine
REEL_Y = -0.25                      # inside the cabinet, in front of the dark backing
SUPPLY_X, TAKEUP_X = -0.16, 0.17    # not symmetric: the take-up side carries the capstan
HUB_R, HUB_T = 0.045, 0.030         # chrome hub caps, part of the BODY -- they do not turn

GLASS_X, GLASS_Z0, GLASS_Z1 = 0.3496, 0.81, 1.638     # dark backing behind the open reel bay
BLUE_X, BLUE_Z0, BLUE_Z1 = 0.3268, 0.126, 0.666       # IBM-blue lower panel
PANEL_Y0, PANEL_Y1 = -0.395, -0.365                   # both panels are set into the front face

LAMP_N = 4
LAMP_X0, LAMP_X1 = -0.2175, 0.1375  # lamp row sits left of centre; the operator plate takes the right
LAMP_HW, LAMP_Z0, LAMP_Z1 = 0.020, 1.699, 1.721
LAMP_Y0, LAMP_Y1 = -0.410, -0.390   # proud of the frame, so they catch light from the side

# name, base colour, metallic, roughness, emission colour, emission strength
MATERIALS = [
    ("M_729_Shell", (0.82, 0.81, 0.78), 0.00, 0.38, None, 1.0),
    ("M_729_Trim",  (0.62, 0.63, 0.65), 0.85, 0.25, None, 1.0),
    ("M_729_Glass", (0.05, 0.05, 0.06), 0.00, 0.12, None, 1.0),
    ("M_729_Blue",  (0.09, 0.28, 0.62), 0.00, 0.40, None, 1.0),
    # Emissive rather than merely bright: with GI off, a "lit" lamp that is only a pale colour
    # renders DARKER than the chrome next to it and reads as switched off.
    ("M_729_Lamp",  (0.10, 0.10, 0.10), 0.00, 0.40, (0.15, 0.95, 0.55), 3.0),
    ("M_729_Reel",  (0.86, 0.66, 0.10), 0.00, 0.55, None, 1.0),   # amber supply
    ("M_729_ReelW", (0.85, 0.85, 0.83), 0.00, 0.50, None, 1.0),   # white take-up
]
SHELL, TRIM, GLASS, BLUE, LAMP, REEL_A, REEL_W = range(7)


def make_material(name, base, metallic, roughness, emission, strength):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (base[0], base[1], base[2], 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission is not None:
        bsdf.inputs["Emission Color"].default_value = (emission[0], emission[1], emission[2], 1.0)
        bsdf.inputs["Emission Strength"].default_value = strength
    return mat


def box(x0, x1, y0, y1, z0, z1):
    """Axis-aligned box as (verts, quads). Written from explicit bounds rather than a primitive
    plus a scale, so the numbers in this file ARE the numbers in the exported bounding box."""
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (2, 3, 7, 6), (1, 2, 6, 5), (3, 0, 4, 7)]
    return v, f


def disc(cx, cy, cz, radius, half_thickness, segments=32):
    """A closed cylinder lying in the XZ plane with its axis on Y -- i.e. a reel facing the front
    of the cabinet. `segments` is a multiple of 4 so vertices land exactly on the axes and the
    exported bounding box is exactly +/-radius; an odd count leaves the disc measurably smaller
    than the reel it is supposed to be."""
    v, f = [], []
    for i in range(segments):
        a = math.tau * i / segments
        x, z = cx + radius * math.cos(a), cz + radius * math.sin(a)
        v.append((x, cy - half_thickness, z))
        v.append((x, cy + half_thickness, z))
    for i in range(segments):
        j = (i + 1) % segments
        f.append((2 * i, 2 * j, 2 * j + 1, 2 * i + 1))          # side quad
    f.append(tuple(2 * i for i in range(segments)))              # back cap (n-gon)
    f.append(tuple(2 * i + 1 for i in range(segments - 1, -1, -1)))   # front cap
    return v, f


def annulus(outer_radius, inner_radius, y0, y1, segments=48):
    """A thin reel flange, open through the middle instead of a solid plate."""
    v, f = [], []
    for i in range(segments):
        a = math.tau * i / segments
        for radius, y in ((outer_radius, y0), (outer_radius, y1),
                          (inner_radius, y0), (inner_radius, y1)):
            v.append((radius * math.cos(a), y, radius * math.sin(a)))
    for i in range(segments):
        j = (i + 1) % segments
        oi, oj, ii, ij = 4 * i, 4 * j, 4 * i + 2, 4 * j + 2
        f.extend(((oi, 4 * i + 1, 4 * j + 1, oj),
                  (ii, ij, 4 * j + 3, 4 * i + 3),
                  (oi, oj, ij, ii),
                  (4 * i + 1, 4 * j + 1, 4 * j + 3, 4 * i + 3)))
    return v, f


def radial_spoke(theta, r0, r1, half_width, y0, y1):
    """A rectangular spoke in the reel's XZ plane, extruded along its axle (Y)."""
    ux, uz = math.cos(theta), math.sin(theta)
    vx, vz = -uz, ux
    points = [(r0 * ux - half_width * vx, r0 * uz - half_width * vz),
              (r1 * ux - half_width * vx, r1 * uz - half_width * vz),
              (r1 * ux + half_width * vx, r1 * uz + half_width * vz),
              (r0 * ux + half_width * vx, r0 * uz + half_width * vz)]
    v = [(x, y, z) for y in (y0, y1) for x, z in points]
    return v, [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1),
               (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]


def build_mesh(name, parts):
    """parts: list of (verts, faces, material_index). Emits ONE mesh with per-face material
    indices, which is what makes the exported GLB carry one primitive per material instead of one
    object per material -- UE imports the former as a single StaticMesh with slots."""
    verts, faces, mat_idx = [], [], []
    for pv, pf, mi in parts:
        base = len(verts)
        verts.extend(pv)
        for face in pf:
            faces.append(tuple(base + i for i in face))
            mat_idx.append(mi)

    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    for poly, mi in zip(me.polygons, mat_idx):
        poly.material_index = mi

    # Recalculate outward. Hand-written winding is easy to get backwards on half the faces, and
    # the symptom in Unreal is a cabinet that renders inside-out only from certain angles --
    # diagnosed as a lighting bug far more often than as the normals problem it is.
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()

    # A UV layer, even with no textures: the glTF importer and UE's lightmap builder both expect
    # TEXCOORD_0, and generating one here is cheaper than debugging why a cook warns about it.
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


def add_object(name, mesh, materials, location=(0.0, 0.0, 0.0)):
    for m in materials:
        mesh.materials.append(m)
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    bpy.context.scene.collection.objects.link(obj)
    return obj


def build(mats):
    # ---- body -------------------------------------------------------------------------------
    parts = [
        # Hollow chassis with a dark-backed open reel bay. The former full-depth cube occluded
        # the window while separate reels sat on its exterior, making them read as loose plates.
        (*box(-SHELL_X, SHELL_X, SHELL_Y - 0.06, SHELL_Y, 0.0, H), SHELL),
        (*box(-SHELL_X, -SHELL_X + 0.055, -SHELL_Y, SHELL_Y - 0.06, 0.0, H), SHELL),
        (*box(SHELL_X - 0.055, SHELL_X, -SHELL_Y, SHELL_Y - 0.06, 0.0, H), SHELL),
        (*box(-SHELL_X, SHELL_X, -SHELL_Y, SHELL_Y, 0.0, 0.075), SHELL),
        (*box(-SHELL_X, SHELL_X, -SHELL_Y, SHELL_Y, H - 0.075, H), SHELL),
        (*box(-0.34, 0.34, 0.245, 0.275, 0.72, 1.66), GLASS),
        # Chrome uprights and a capping rail. Full height, standing proud on both sides: this is
        # the profile that separates a 729 from a filing cabinet at 15 metres.
        (*box(-TRIM_X, -SHELL_X, TRIM_FRONT, TRIM_BACK, 0.0, H), TRIM),
        (*box(SHELL_X, TRIM_X, TRIM_FRONT, TRIM_BACK, 0.0, H), TRIM),
        (*box(-TRIM_X, TRIM_X, TRIM_FRONT, TRIM_BACK, H - 0.04, H), TRIM),
        (*box(-TRIM_X, TRIM_X, TRIM_FRONT, TRIM_BACK, 0.70, 0.77), TRIM),
        (*box(-BLUE_X, BLUE_X, PANEL_Y0, PANEL_Y1, BLUE_Z0, BLUE_Z1), BLUE),
    ]
    step = (LAMP_X1 - LAMP_X0) / (LAMP_N - 1)
    for i in range(LAMP_N):
        cx = LAMP_X0 + i * step
        parts.append((*box(cx - LAMP_HW, cx + LAMP_HW, LAMP_Y0, LAMP_Y1, LAMP_Z0, LAMP_Z1), LAMP))

    body = build_mesh("IBM729_Body", parts)
    add_object("IBM729_Body", body, [mats[i] for i in (SHELL, TRIM, GLASS, BLUE, LAMP)])

    # ---- reels ------------------------------------------------------------------------------
    # Open-spoke transport reels, centred at the hub so the imported node transform and runtime
    # spin use the same pivot.
    for name, x, mat in (("Reel_Supply", SUPPLY_X, REEL_A), ("Reel_Takeup", TAKEUP_X, REEL_W)):
        half = REEL_T / 2.0
        parts = [(*disc(0.0, 0.0, 0.0, 0.098, 0.010, segments=48), 1),
                 (*annulus(REEL_R, 0.116, -half, -half + 0.009), 0),
                 (*annulus(REEL_R, 0.116, half - 0.009, half), 0),
                 (*disc(0.0, 0.0, 0.0, 0.038, half, segments=32), 2),
                 (*disc(0.0, 0.0, 0.0, 0.010, half + 0.002, segments=24), 1)]
        for i in range(8):
            theta = math.tau * i / 8.0
            parts.append((*radial_spoke(theta, 0.035, 0.122, 0.010,
                                        -half, -half + 0.008), 0))
            parts.append((*radial_spoke(theta, 0.035, 0.122, 0.010,
                                        half - 0.008, half), 0))
        me = build_mesh(name, parts)
        add_object(name, me, [mats[mat], mats[GLASS], mats[TRIM]], location=(x, REEL_Y, REEL_Z))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="make_ibm729.py")
    ap.add_argument("--out", default=None,
                    help="output .glb, or a directory to write ibm-729-tape.glb into")
    args = ap.parse_args(argv)

    out = args.out or os.environ.get("MHLLM_GLB_DIR") or "."
    if os.path.isdir(out) or not out.lower().endswith(".glb"):
        out = os.path.join(out, "ibm-729-tape.glb")
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)

    # Factory settings, empty: the default scene's camera and lamp would export as glTF nodes and
    # arrive in Unreal as extra actors in the middle of every tape unit.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = [make_material(*m) for m in MATERIALS]
    build(mats)

    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True,
                              export_materials="EXPORT", export_apply=False)

    for obj in bpy.context.scene.objects:
        bb = [obj.matrix_world @ v.co for v in obj.data.vertices]
        lo = [min(v[i] for v in bb) for i in range(3)]
        hi = [max(v[i] for v in bb) for i in range(3)]
        print("  %-12s verts %4d  polys %3d  bounds x[%.3f %.3f] y[%.3f %.3f] z[%.3f %.3f]"
              % (obj.name, len(obj.data.vertices), len(obj.data.polygons),
                 lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    print("wrote %s (%.1f KB)" % (out, os.path.getsize(out) / 1024.0))


if __name__ == "__main__":
    main()
