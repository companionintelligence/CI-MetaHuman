# Headless UE authoring: the CompanionStage set, built from Deployment/stage_layout.json.
#
# THE SET IS DATA. Placement, blockouts, desks, lights, sounds, the resident's mark and the authored
# camera all live in stage_layout.json (the layout format the stage design is written in: meta,
# props, blockouts, resident, lights, shots, beats). This script is the interpreter: it imports the
# prop GLBs, spawns what the layout lists, places the resident and saves the map. Change the set by
# changing the JSON, not this file. Docs/SCENE-AUTHORING.md describes the schema.
#
# Writes /out/AUTHOR_RESULT.txt with progress + SUCCESS/FAIL, and the same lines to
# Content/AUTHOR_RESULT.txt beside the map (see THE RESULT TRAVELS WITH THE CONTENT below).
import unreal, traceback, math, os, hashlib, json

# INPUT LOCATIONS. The container mounts these at fixed absolute paths (see
# Deployment/core2/run_author_m2.sh), and every default below is that container path, so the
# server pipeline behaves exactly as before. Scripts/BuildNative.ps1 runs this SAME script
# against the locally-installed editor by overriding them with Windows paths — which is the
# whole point: the set has ONE definition, and a native build authors it rather than shipping
# a stale copy of it. Any new external input must be read through here, never hard-coded, or
# the local build silently diverges from the cook again.
def _dir(var, default):
    return os.environ.get(var, default).replace("\\", "/").rstrip("/")

GLB_DIR   = _dir("MHLLM_GLB_DIR",   "/glb")      # prop GLBs
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
# THE LAYOUT. Next to this script by default: Deployment/stage_layout.json in a checkout, and
# /proj/stage_layout.json in the container, where run_author_m2.sh copies both files side by side.
STAGE_LAYOUT = os.environ.get("MHLLM_STAGE_LAYOUT", "").strip() or \
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "stage_layout.json")
# Named input directories a layout may refer to (a texture's or a sound's "dir").
INPUT_DIRS = {"glb": GLB_DIR, "audio": AUDIO_DIR, "brand": BRAND_DIR, "gen": GEN_DIR}

# WHICH RESIDENT. Three choices, and the default is the only one whose licence clears this use.
#   "mannequin" (the default) places Epic's UE mannequin, SKM_Manny from the engine's MoverTests
#       plugin, playing its MM_Idle loop. Content installed with Unreal Engine is Licensed
#       Technology under the Unreal Engine EULA (section 1), and the current EULA carries no AI
#       restriction. It is a PLACEHOLDER: it has no facial morph targets, so the face driver binds
#       and resolves 0 curves (no lip-sync, no expression, no eye movement).
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
RESIDENTS = ("mannequin", "metahuman", "nia")

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

# SLOTS WHOSE SKETCHFAB MODEL IS NoAI-TAGGED. These three files were maxdragon's CC BY 4.0 models,
# and all three Sketchfab pages carry the NoAI tag. Sketchfab's Terms of Use bar NoAI content from
# use "in datasets for, in the development of, or as inputs to generative AI programs" (Sketchfab
# Help Center article 16152133), and this product is an AI companion, so that is a licence issue.
# A file in one of these slots is imported only when its glTF asset block names Companion
# Intelligence as the copyright holder, which is what Deployment/blender/make_period_props.py
# writes into the replacements it builds under the same names. Anything else, such as the old
# Sketchfab download or a file of unknown origin, is REFUSED and the slot stays empty, so a stale
# prop directory cannot put those models back into a build. This is licensing policy, not layout,
# so it stays in code: a layout cannot opt a file out of it.
CI_AUTHORED_ONLY = ("ibm-1401-cpu.glb", "ibm-3178-terminal.glb", "ibm-3350-disk.glb")
CI_COPYRIGHT_MARK = "Companion Intelligence"
# FILES THAT MUST NEVER SHIP, whatever a layout says: ibm-2401-tape.glb is maxdragon's NoAI-tagged
# Sketchfab model and has no CI-authored replacement under that name. A layout naming it is refused
# like a stale NoAI slot, so it cannot reach /Game, the map or the cook.
NEVER_SHIP = ("ibm-2401-tape.glb",)

def glb_copyright(path):
    """asset.copyright of a binary glTF file, "" when it has none, None when it cannot be read."""
    import struct
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

# ---------- colour: what the layout's numbers mean ----------
# LIGHT COLOURS are written sRGB-ENCODED (lights[].srgb: a colour picker's or a kelvin table's value).
# set_light_color() reads its argument as LINEAR (bSRGB=true only quantises the linear value into an
# sRGB FColor), so the encoded values must be decoded first. Passing them straight in is what rendered
# the "4100 K" ceiling field at about 5400 K and the "2700 K" practicals near-neutral once the 4300 K
# white balance had been applied (Docs/STAGE-DESIGN.md section 8).
def srgb_decode(c):
    """One sRGB-encoded channel (0..1) to linear."""
    c = float(c)
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def luminance(rgb):
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]

def white_balance_white(kelvin):
    """Linear sRGB (largest channel 1) of the white that Unreal's white balance turns neutral at
    WhiteTemp = kelvin and tint 0. Same source white as TonemapCommon.ush WhiteBalance(): the CIE
    daylight locus from 4000 K up (with its Planck-constant correction), the Planckian locus (Krystek)
    below."""
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
    rgb = (3.2404542 * X - 1.5371385 - 0.4985314 * Z,
           -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
           0.0556434 * X - 0.2040259 + 1.0572252 * Z)
    m = max(rgb)
    return tuple(c / m for c in rgb)

# EMISSIVE COLOURS are DISPLAY colours: how a lamp, a panel or a lit window should read on screen,
# after the white balance. The white balance would turn a neutral emissive blue (it neutralises a
# 4300 K white), so the scene colour handed to the material is the display colour times the balanced
# white, over that white's luminance (a von Kries scaling, which keeps the white's luminance as the
# engine's chromatic adaptation does). EMISSIVE STRENGTHS are cd/m2 per unit of the colour's
# luminance: Unreal reads emissive 1 as 1 cd/m2, the unit its candela lights and EV100 work in.
def emissive_scene_colour(display_rgb_lin, wb_white):
    k = luminance(wb_white)
    return tuple(c * wv / k for c, wv in zip(display_rgb_lin, wb_white))

# ---------- GLB edits the layout asks for: props[].clipLocal, props[].rotateLocal, one mesh a file ----------
def glb_mesh_nodes(path):
    """How many nodes of a binary glTF file's scene carry a mesh, None when it cannot be read."""
    import struct
    try:
        with open(path, "rb") as f:
            head = f.read(20)
            if len(head) < 20:
                return None
            magic, _version, _length, chunk_len, chunk_type = struct.unpack("<4sIII4s", head)
            if magic != b"glTF" or chunk_type != b"JSON":
                return None
            doc = json.loads(f.read(chunk_len).decode("utf-8"))
    except Exception:
        return None
    nodes = doc.get("nodes") or []
    scene = (doc.get("scenes") or [{}])[doc.get("scene", 0)]
    count, todo = 0, list(scene.get("nodes") or [])
    while todo:
        i = todo.pop()
        count += 1 if "mesh" in nodes[i] else 0
        todo += nodes[i].get("children") or []
    return count

def edit_glb(src, dst, clip=None, rotate=None, merge=False):
    """Writes src to dst edited the way the layout asks, so the map places what the previz shows. Returns
    (kept, total) triangle counts. Standard library only.
      clip    {"x": [lo, hi], "y": [...], "z": [...]} in glTF SCENE-space metres (node transforms applied):
              every triangle with a vertex outside the box is removed, the same box and the same rule as
              previz_diorama.py's clipped_mesh. It is how the RAMAC loses its loose platter.
      rotate  [["x", deg], ...]: turns about the glTF scene axes (right-handed, Y up), in order, after the
              clip. It stands up a model that was exported lying down (the Kenbak-1) once, before import, so
              every prop is still placed by yaw alone. It implies merge.
      merge   every node transform (and the rotation) baked into the vertices, and the file written as ONE
              mesh on ONE node at the origin. Interchange makes a StaticMesh of each glTF mesh and a prop
              places one, so a file of many meshes (the Kenbak's 79, the SRI mouse's 16) would otherwise
              arrive as one loose part of the model.
    Without rotate or merge the node tree is kept and only the clipped triangles go (the RAMAC's edit)."""
    import struct
    with open(src, "rb") as f:
        data = f.read()
    magic, version, _length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2:
        raise RuntimeError("%s is not a binary glTF 2 file" % src)
    off, chunks = 12, []
    while off + 8 <= len(data):
        n, kind = struct.unpack_from("<I4s", data, off)
        chunks.append((kind, data[off + 8: off + 8 + n]))
        off += 8 + n
    if not chunks or chunks[0][0] != b"JSON" or len(chunks) < 2 or chunks[1][0] != b"BIN\x00":
        raise RuntimeError("%s: expected a JSON and a BIN chunk" % src)
    doc = json.loads(chunks[0][1].decode("utf-8"))
    binc = bytearray(chunks[1][1])
    lim = {k: (float(v[0]), float(v[1])) for k, v in (clip or {}).items()}
    axis = {"x": 0, "y": 1, "z": 2}

    def mat_mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    def local_matrix(node):
        if "matrix" in node:
            m = node["matrix"]                      # column-major
            return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        tx, ty, tz = node.get("translation", [0.0, 0.0, 0.0])
        qx, qy, qz, qw = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
        sx, sy, sz = node.get("scale", [1.0, 1.0, 1.0])
        r = [[1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw)],
             [2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw)],
             [2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy)]]
        return [[r[0][0] * sx, r[0][1] * sy, r[0][2] * sz, tx],
                [r[1][0] * sx, r[1][1] * sy, r[1][2] * sz, ty],
                [r[2][0] * sx, r[2][1] * sy, r[2][2] * sz, tz],
                [0.0, 0.0, 0.0, 1.0]]

    nodes = doc.get("nodes") or []
    world = {}
    def walk(i, parent):
        world[i] = mat_mul(parent, local_matrix(nodes[i]))
        for c in nodes[i].get("children") or []:
            walk(c, world[i])
    ident = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    scene = (doc.get("scenes") or [{}])[doc.get("scene", 0)]
    for root in scene.get("nodes") or []:
        walk(root, ident)
    users = {}
    for i in world:
        if "mesh" in nodes[i]:
            users.setdefault(nodes[i]["mesh"], []).append(i)

    acc, views = doc["accessors"], doc["bufferViews"]
    def read(ai, per):
        a = acc[ai]
        v = views[a["bufferView"]]
        size = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}[a["componentType"]]
        fmt = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}[a["componentType"]]
        stride = v.get("byteStride") or size * per
        base = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        return [struct.unpack_from("<%d%s" % (per, fmt), binc, base + k * stride) for k in range(a["count"])]

    def write(values, comp, kind, target, bounds=False):
        """Appends a tightly packed accessor (float VEC3/VEC4 or uint32 SCALAR) and returns its index."""
        per = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        flat = [c for v in values for c in v] if per > 1 else list(values)
        while len(binc) % 4:
            binc.append(0)
        views.append({"buffer": 0, "byteOffset": len(binc), "byteLength": 4 * len(flat), "target": target})
        binc.extend(struct.pack("<%d%s" % (len(flat), "f" if comp == 5126 else "I"), *flat))
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": len(values), "type": kind}
        if bounds:
            a["min"] = [min(v[c] for v in values) for c in range(per)] if per > 1 else [min(values)]
            a["max"] = [max(v[c] for v in values) for c in range(per)] if per > 1 else [max(values)]
        acc.append(a)
        return len(acc) - 1

    kept_total, tri_total = 0, 0
    if rotate or merge:
        # ONE mesh on ONE node: each node's world matrix, then the rotation, baked into its own primitives.
        R = [[1.0 if i == j else 0.0 for j in range(3)] for i in range(3)]
        for ax, deg in rotate or []:
            c, s_ = math.cos(math.radians(float(deg))), math.sin(math.radians(float(deg)))
            q = {"x": [[1, 0, 0], [0, c, -s_], [0, s_, c]], "y": [[c, 0, s_], [0, 1, 0], [-s_, 0, c]],
                 "z": [[c, -s_, 0], [s_, c, 0], [0, 0, 1]]}[str(ax).lower()]
            R = [[sum(q[i][k] * R[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
        merged = []
        for node_index in sorted(i for i in world if "mesh" in nodes[i]):
            M = world[node_index]
            L = [[sum(R[i][k] * M[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
            T = [sum(R[i][k] * M[k][3] for k in range(3)) for i in range(3)]
            det = (L[0][0] * (L[1][1] * L[2][2] - L[1][2] * L[2][1]) - L[0][1] * (L[1][0] * L[2][2] - L[1][2] * L[2][0])
                   + L[0][2] * (L[1][0] * L[2][1] - L[1][1] * L[2][0]))
            if abs(det) < 1e-30:
                raise RuntimeError("%s: node %d has a singular transform" % (src, node_index))
            # normals take the inverse transpose: the cofactor matrix over the determinant
            N = [[(L[(i + 1) % 3][(j + 1) % 3] * L[(i + 2) % 3][(j + 2) % 3]
                   - L[(i + 1) % 3][(j + 2) % 3] * L[(i + 2) % 3][(j + 1) % 3]) / det for j in range(3)] for i in range(3)]
            for prim in doc["meshes"][nodes[node_index]["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4 or prim.get("targets"):
                    raise RuntimeError("%s: only plain triangle meshes can be merged" % src)
                attrs = dict(prim["attributes"])
                if acc[attrs["POSITION"]]["componentType"] != 5126:
                    raise RuntimeError("%s: positions are not floats (a quantised mesh cannot be merged)" % src)
                pos = read(attrs["POSITION"], 3)
                idx = [i[0] for i in read(prim["indices"], 1)] if "indices" in prim else list(range(len(pos)))
                tris = [idx[t:t + 3] for t in range(0, len(idx) - 2, 3)]
                if lim:
                    inside = []
                    for p in pos:
                        g = [M[r][0] * p[0] + M[r][1] * p[1] + M[r][2] * p[2] + M[r][3] for r in range(3)]
                        inside.append(all(lo <= g[axis[k]] <= hi for k, (lo, hi) in lim.items()))
                    keep = [t for t in tris if inside[t[0]] and inside[t[1]] and inside[t[2]]]
                else:
                    keep = tris
                tri_total += len(tris)
                kept_total += len(keep)
                if not keep:
                    continue
                if det < 0.0:
                    keep = [[t[0], t[2], t[1]] for t in keep]      # a mirror flips the winding
                attrs["POSITION"] = write([[sum(L[i][k] * p[k] for k in range(3)) + T[i] for i in range(3)] for p in pos],
                                          5126, "VEC3", 34962, bounds=True)
                if "NORMAL" in attrs and acc[attrs["NORMAL"]]["componentType"] == 5126:
                    out = []
                    for nv in read(attrs["NORMAL"], 3):
                        v = [sum(N[i][k] * nv[k] for k in range(3)) for i in range(3)]
                        ln = math.sqrt(sum(c * c for c in v)) or 1.0
                        out.append([c / ln for c in v])
                    attrs["NORMAL"] = write(out, 5126, "VEC3", 34962)
                if "TANGENT" in attrs and acc[attrs["TANGENT"]]["componentType"] == 5126:
                    out = []
                    for tv in read(attrs["TANGENT"], 4):
                        v = [sum(L[i][k] * tv[k] for k in range(3)) for i in range(3)]
                        ln = math.sqrt(sum(c * c for c in v)) or 1.0
                        out.append([v[0] / ln, v[1] / ln, v[2] / ln, tv[3] if det > 0.0 else -tv[3]])
                    attrs["TANGENT"] = write(out, 5126, "VEC4", 34962)
                merged_prim = {"attributes": attrs, "mode": 4,
                               "indices": write([i for t in keep for i in t], 5125, "SCALAR", 34963, bounds=True)}
                if "material" in prim:
                    merged_prim["material"] = prim["material"]
                merged.append(merged_prim)
        if not merged:
            raise RuntimeError("%s: the edit left no triangles" % src)
        stem = os.path.splitext(os.path.basename(src))[0]
        doc["meshes"] = [{"name": stem, "primitives": merged}]
        doc["nodes"] = [{"name": stem, "mesh": 0}]
        doc["scenes"] = [{"name": stem, "nodes": [0]}]
        doc["scene"] = 0
        for k in ("skins", "animations", "cameras"):
            doc.pop(k, None)
        users = {}
    for mesh, who in users.items():
        if len(who) > 1:
            raise RuntimeError("%s: mesh %d is instanced by %d nodes; clipLocal needs one node per mesh" % (src, mesh, len(who)))
    for mesh, (node_index,) in users.items():
        M = world[node_index]
        prims = doc["meshes"][mesh]["primitives"]
        survivors = []
        for prim in prims:
            if prim.get("mode", 4) != 4:
                survivors.append(prim)
                continue
            pos = read(prim["attributes"]["POSITION"], 3)
            inside = []
            for p in pos:
                g = [M[r][0] * p[0] + M[r][1] * p[1] + M[r][2] * p[2] + M[r][3] for r in range(3)]
                inside.append(all(lo <= g[axis[k]] <= hi for k, (lo, hi) in lim.items()))
            idx = [i[0] for i in read(prim["indices"], 1)] if "indices" in prim else list(range(len(pos)))
            tris = [idx[t:t + 3] for t in range(0, len(idx) - 2, 3)]
            keep = [t for t in tris if inside[t[0]] and inside[t[1]] and inside[t[2]]]
            tri_total += len(tris)
            kept_total += len(keep)
            if not keep:
                continue
            if len(keep) != len(tris):
                flat = [i for t in keep for i in t]
                while len(binc) % 4:
                    binc.append(0)
                views.append({"buffer": 0, "byteOffset": len(binc), "byteLength": 4 * len(flat), "target": 34963})
                binc += struct.pack("<%dI" % len(flat), *flat)
                acc.append({"bufferView": len(views) - 1, "componentType": 5125, "count": len(flat), "type": "SCALAR",
                            "min": [min(flat)], "max": [max(flat)]})
                prim["indices"] = len(acc) - 1
            survivors.append(prim)
        doc["meshes"][mesh]["primitives"] = survivors
        if not survivors:
            nodes[node_index].pop("mesh", None)
    while len(binc) % 4:
        binc.append(0)
    doc["buffers"][0]["byteLength"] = len(binc)
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    out = struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + 8 + len(binc))
    out += struct.pack("<I4s", len(js), b"JSON") + js + struct.pack("<I4s", len(binc), b"BIN\x00") + bytes(binc)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(out)
    return kept_total, tri_total

def clip_glb(src, dst, clip):
    """edit_glb with a clip box only (the RAMAC's edit): the node tree kept, the clipped triangles removed."""
    return edit_glb(src, dst, clip=clip)

def load_layout(path):
    """The stage layout, checked for the sections this script reads. Fails loudly: a missing or
    malformed layout must stop the build, never author an empty room."""
    with open(path, encoding="utf-8") as f:
        layout = json.load(f)
    for key in ("meta", "props", "blockouts", "resident", "lights", "shots"):
        if key not in layout:
            raise RuntimeError("stage layout %s has no '%s' section" % (path, key))
    for key in ("x", "y", "z"):
        if key not in layout["resident"]:
            raise RuntimeError("stage layout %s: resident has no '%s'" % (path, key))
    return layout

try:
    # Truncate both, so no line of an earlier run survives into this one. If the copy beside the map
    # cannot be written, fail here: an authored map without it is refused by every gate downstream.
    for path in RESULT_FILES:
        open(path, "w").close()
    w("start")
    if RESIDENT not in RESIDENTS:
        raise RuntimeError("MHLLM_RESIDENT=%r is not one of %s" % (RESIDENT, "/".join(RESIDENTS)))
    w("resident requested: %s (MHLLM_RESIDENT)" % RESIDENT)
    LAYOUT    = load_layout(STAGE_LAYOUT)
    META      = LAYOUT.get("meta") or {}
    PROPS     = LAYOUT.get("props") or []
    BLOCKOUTS = LAYOUT.get("blockouts") or []
    LIGHTS    = LAYOUT.get("lights") or []
    SOUNDS    = LAYOUT.get("sounds") or []
    SHOTS     = LAYOUT.get("shots") or []
    RES       = LAYOUT["resident"]
    w("layout: %s (%d props, %d blockouts, %d lights, %d sounds, %d shots)" % (
        STAGE_LAYOUT.replace("\\", "/"), len(PROPS), len(BLOCKOUTS), len(LIGHTS), len(SOUNDS), len(SHOTS)))

    # ONE FILE DRIVES BOTH. The same layout's shots and beats reach the C++ stage director through
    # Config/DefaultGame.ini ([/Script/CICompanion.CompanionStageSettings]); stage_layout.py, beside
    # this script, writes that section. It is written only into a project that has the CICompanion
    # plugin (the native build authors the real project). The container authors a content-only
    # project, so there the cook reads the COMMITTED section, which `stage_layout.py --check` (run by
    # core2/stage.sh) proves is current.
    try:
        import sys as _sys
        _here = os.path.dirname(os.path.abspath(__file__))
        if _here not in _sys.path:
            _sys.path.insert(0, _here)
        import stage_layout as _stage_ini
        _proj = str(unreal.Paths.project_dir()).replace("\\", "/").rstrip("/")
        _ini = os.path.join(_proj, "Config", "DefaultGame.ini")
        if os.path.isdir(os.path.join(_proj, "Plugins", "CICompanion")) and os.path.isfile(_ini):
            changed = _stage_ini.write_ini(_ini, STAGE_LAYOUT, LAYOUT)
            w("stage ini: %s %s from the layout" % (_ini.replace("\\", "/"), "rewritten" if changed else "already current"))
        else:
            w("stage ini: skipped (%s has no CICompanion plugin; the cook uses the committed Config/DefaultGame.ini)" % _proj)
    except ImportError:
        w("stage ini: skipped (stage_layout.py is not beside this script)")

    # Per-group tallies for the summary lines (meta.groupLog) and the self-checks at the end.
    GROUP_TOTAL, GROUP_PLACED, GROUP_REELS = {}, {}, {}
    def tally(group, ok, reels=0):
        if not group:
            return
        GROUP_TOTAL[group] = GROUP_TOTAL.get(group, 0) + 1
        GROUP_PLACED[group] = GROUP_PLACED.get(group, 0) + (1 if ok else 0)
        GROUP_REELS[group] = GROUP_REELS.get(group, 0) + reels

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # ---------- import every prop slot (GLB via Interchange; materials+textures come with) ----------
    # Props are imported as GLB, not FBX. Blender's FBX exporter only writes textures wired to a
    # Principled BSDF base color, so most of these Sketchfab materials exported with no maps at
    # all and every machine rendered as a blank white block. The GLB export carries materials +
    # textures intact, and Interchange's glTF importer brings Texture2D + MaterialInstanceConstant
    # across with the mesh. A SLOT is one imported file (/Game/Diorama/<slot>); any number of
    # props may place the same slot.
    # A slot whose prop asks for an edit is imported from an edited copy (edit_glb), so the map places
    # exactly what the previz shows: clipLocal removes the triangles outside its box (the RAMAC's loose
    # platter, the Kenbak's power cord), rotateLocal stands up a model exported lying down (the Kenbak),
    # and a file of several meshes is merged into one, because Interchange imports each glTF mesh as its
    # own StaticMesh and a prop places one of them (the SRI mouse is 16 meshes, the Kenbak 79). A slot
    # that places named sub-meshes (mesh, reels) is never merged. The copy goes to the temp directory,
    # never next to the licensed originals.
    CLIP_DIR = os.path.join(os.environ.get("TEMP") or os.environ.get("TMPDIR") or "/tmp", "ci_stage_glb_edits").replace("\\", "/")
    def import_prop(name, glb, mesh_name=None, clip=None, rotate=None, merge=False):
        task = unreal.AssetImportTask()
        task.filename = GLB_DIR + "/" + glb
        if clip or rotate or merge:
            edited = CLIP_DIR + "/" + name + "/" + glb
            kept, total = edit_glb(GLB_DIR + "/" + glb, edited, clip, rotate, merge)
            what = [("clipLocal %s" % json.dumps(clip, sort_keys=True)) if clip else "",
                    ("rotateLocal %s" % json.dumps(rotate)) if rotate else "",
                    ("%d meshes merged into one" % glb_mesh_nodes(GLB_DIR + "/" + glb)) if (rotate or merge) else ""]
            w("%s: edited before import (%s): kept %d of %d triangles (%d removed)"
              % (name, "; ".join(x for x in what if x), kept, total, total - kept))
            task.filename = edited
        task.destination_path = "/Game/Diorama/" + name
        task.automated = True
        task.save = True
        task.replace_existing = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        assets = ar.get_assets_by_path("/Game/Diorama/" + name, recursive=True)
        mesh, tex, meshes = None, 0, 0
        for a in assets:
            k = str(a.asset_class_path.asset_name)
            asset_name = str(a.asset_name)
            meshes += 1 if k == "StaticMesh" else 0
            if k == "StaticMesh" and mesh is None and (mesh_name is None or asset_name == mesh_name):
                mesh = unreal.load_asset(str(a.package_name))
            elif k == "Texture2D":
                tex += 1
        return mesh, tex, meshes

    def slot_of(p):
        return p.get("slot") or p["name"]

    SLOTS = {}                       # slot -> the first prop entry that names its GLB
    for p in PROPS:
        if p.get("glb") and slot_of(p) not in SLOTS:
            SLOTS[slot_of(p)] = p
    imported = {}
    slot_meshes = {}                 # slot -> {mesh name: StaticMesh} for props that place sub-meshes (reels)
    refused = []
    for slot, p in SLOTS.items():
        glb = p["glb"]
        if glb in NEVER_SHIP:
            imported[slot] = None
            refused.append(slot)
            w("prop REFUSED %-10s %s must not ship (NoAI-tagged Sketchfab model, no CI replacement)" % (slot, glb))
            continue
        if glb in CI_AUTHORED_ONLY and os.path.isfile(GLB_DIR + "/" + glb):
            mark = glb_copyright(GLB_DIR + "/" + glb)
            if not mark or CI_COPYRIGHT_MARK not in mark:
                # Not imported at all, so nothing of it reaches /Game, the map or the cook.
                # place_prop() then logs "SKIP place" for every prop that would have used it.
                imported[slot] = None
                refused.append(slot)
                w("prop REFUSED %-10s %s is not CI-authored (asset.copyright=%r); the Sketchfab model "
                  "that filled this slot is NoAI-tagged. Build the replacement with "
                  "Deployment/blender/make_period_props.py" % (slot, glb, mark))
                continue
        merge = bool(p.get("rotateLocal")) or (not p.get("mesh") and not p.get("reels")
                                                and (glb_mesh_nodes(GLB_DIR + "/" + glb) or 0) > 1)
        sm, tex, n_meshes = import_prop(slot, glb, p.get("mesh"), p.get("clipLocal"), p.get("rotateLocal"), merge)
        imported[slot] = sm
        if p.get("reels"):
            # This GLB carries separately-pivoted sub-meshes (the tape reels). The registry's first
            # entry is not guaranteed to be the cabinet body, so the body is selected by name above
            # and the reels by the names the layout gives them.
            wanted = set(r["mesh"] for q in PROPS if slot_of(q) == slot for r in q.get("reels", []))
            found = {}
            for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(
                    "/Game/Diorama/" + slot, recursive=True):
                if str(a.asset_class_path.asset_name) == "StaticMesh" and str(a.asset_name) in wanted:
                    found[str(a.asset_name)] = unreal.load_asset(str(a.package_name))
            slot_meshes[slot] = found
            w("%s: body=%s reels=%d" % (p.get("importLabel", slot), sm is not None, len(found)))
        else:
            w("import %-11s -> %s (%d tex, %d static mesh%s)" % (slot, "OK" if sm else "NO_STATICMESH", tex, n_meshes,
                                                                "" if n_meshes == 1 else "es"))
            if n_meshes > 1 and not p.get("mesh"):
                # A prop places ONE StaticMesh, so every other part of the file would be missing from the map.
                w("prop INCOMPLETE %s: %s imported as %d static meshes; one is placed" % (slot, glb, n_meshes))
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

    # The streamed pawn spawns at the WORLD ORIGIN, not at our PlayerStart (GameModeBase does not
    # honour it here). The view comes from the runtime stage director's own camera instead, so the
    # set is authored in world space.
    SET_OFFSET = (0.0, 0.0, 0.0)
    def spawn(cls, loc=(0,0,0), yaw=0.0, pitch=0.0, roll=0.0):
        world = (loc[0] + SET_OFFSET[0], loc[1] + SET_OFFSET[1], loc[2] + SET_OFFSET[2])
        return eas.spawn_actor_from_class(
            cls, unreal.Vector(*world), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))

    # ---------- lights ----------
    # CRITICAL: the Pixel Streaming app runs with GI DISABLED (r.DynamicGlobalIlluminationMethod 0
    # and friends — the RADV stability workarounds), so DIRECT light is all there is. Do not tune
    # the layout's lights against a local editor preview: Lumen is on by default there and will
    # bounce a 2.5-lux directional into a convincingly-lit room, while the real stream renders
    # black. Every light in the layout is chosen for a no-GI, direct-light-only scene.
    MOVABLE = unreal.ComponentMobility.MOVABLE
    def lighting_channels(channels):
        lc = unreal.LightingChannels()
        for i in range(3):
            lc.set_editor_property("channel%d" % i, i in channels)
        return lc

    # A light is the layout's type at its position, aimed by yaw/pitch (a light shines down its local
    # +X, so pitch -90 points it at the floor). Optional fields are applied only when present:
    #   units "cd"      intensity is in candela (the stage design's unit), set BEFORE the value
    #   radiusCm        attenuation radius: each light lights its own bay, not the whole room
    #   srgb | linear | kelvin  colour sRGB-encoded (decoded here: set_light_color takes linear), or
    #                   already linear, else a colour temperature
    #   shadows         cast shadows (the design keeps two shadow casters; every shadow costs)
    #   sourceRadiusCm  soft source for point and spot lights
    #   coneDeg         a spot's FULL cone; Unreal's outer cone angle is the half angle
    #   sizeCm          a rect light's [width, height]
    #   lightingChannels  e.g. [0, 1]: which channels it lights (the 1403 cover is channel 1 only)
    #   tags            e.g. ExteriorLight: the lights the director culls once the camera is inside
    LIGHT_TYPES = {
        "point":       (unreal.PointLight,       unreal.PointLightComponent),
        "spot":        (unreal.SpotLight,        unreal.SpotLightComponent),
        "rect":        (unreal.RectLight,        unreal.RectLightComponent),
        "directional": (unreal.DirectionalLight, unreal.DirectionalLightComponent),
    }
    def place_light(l):
        kind = l.get("type", "point")
        if kind not in LIGHT_TYPES:
            w("light %s: unknown type %r (not placed)" % (l.get("name"), kind))
            return False
        cls, comp_cls = LIGHT_TYPES[kind]
        a = spawn(cls, (l["x"], l["y"], l["z"]), yaw=l.get("yaw", 0.0), pitch=l.get("pitch", 0.0))
        c = a.get_component_by_class(comp_cls)
        c.set_editor_property("mobility", MOVABLE)
        if l.get("units") == "cd":
            c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        c.set_intensity(l["intensity"])
        if "radiusCm" in l:
            c.set_attenuation_radius(l["radiusCm"])
        if "srgb" in l:
            c.set_light_color(unreal.LinearColor(*[srgb_decode(v) for v in l["srgb"][:3]], 1.0))
        elif "linear" in l:
            c.set_light_color(unreal.LinearColor(*[float(v) for v in l["linear"][:3]], 1.0))
        elif "kelvin" in l:
            c.set_editor_property("use_temperature", True)
            c.set_editor_property("temperature", float(l["kelvin"]))
        if "shadows" in l:
            c.set_editor_property("cast_shadows", bool(l["shadows"]))
        if "sourceRadiusCm" in l and kind in ("point", "spot"):
            c.set_editor_property("source_radius", float(l["sourceRadiusCm"]))
        if kind == "spot" and "coneDeg" in l:
            outer = float(l["coneDeg"]) / 2.0
            c.set_editor_property("outer_cone_angle", outer)
            c.set_editor_property("inner_cone_angle", outer * 0.7)
        if kind == "rect" and "sizeCm" in l:
            c.set_editor_property("source_width", float(l["sizeCm"][0]))
            c.set_editor_property("source_height", float(l["sizeCm"][1]))
        if "lightingChannels" in l:
            c.set_editor_property("lighting_channels", lighting_channels(l["lightingChannels"]))
        if l.get("tags"):
            a.tags = list(l["tags"])
        if l.get("units"):
            # Only the new-format lights are labelled; the older layout's lights never were.
            a.set_actor_label("Light_" + l["name"])
        return True
    lights_placed = 0
    lights_shadowed = sum(1 for l in LIGHTS if l.get("shadows"))
    lights_exterior = sum(1 for l in LIGHTS if "ExteriorLight" in (l.get("tags") or []))
    for l in LIGHTS:
        try:
            ok = place_light(l)
        except Exception as e:
            w("light %s warn: %s" % (l.get("name"), e)); ok = False
        lights_placed += 1 if ok else 0
        tally(l.get("group"), ok)

    # ---------- exposure: PINNED, and this is the fix for the white-haze room ----------
    #
    # THE BUG. Config/DefaultEngine.ini sets
    #     r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True
    # which changes the UNITS of AutoExposureMinBrightness / MaxBrightness from average scene
    # luminance to EV100. The values that used to be here, 0.03 and 8.0, are UE4's LUMINANCE-space
    # defaults, pasted into a field this project reads as EV100. So "wide bounds" was in fact a
    # hard ceiling of EV100 8.0 = an average scene luminance of 1.2 * 2^8 = 307 cd/m^2, and every
    # frame shipped 3.5 STOPS — 11x — overexposed (Docs/SCENE-AUTHORING.md has the histogram).
    #
    # WHY PIN IT RATHER THAN WIDEN IT. r.EyeAdaptation.* console vars do not rescue it (a volume
    # that overrides exposure wins); one camera and one light rig leave nothing for adaptation to
    # adapt to; and auto exposure silently RE-GRADES a lighting change away. min == max is the
    # supported way to say "fixed exposure" in a volume, and it converges on frame ZERO.
    #
    # TUNING IT. +1 EV halves the image, -1 doubles it. The value (meta.exposureEV100) is solved
    # FOR the layout's lights: change either without re-measuring the other and the frame is wrong.
    # Re-derive it on a captured frame from the game (mean near 120/255, nothing above 240), never
    # in an editor viewport, which is neither GI-off nor governed by this volume.
    EXPOSURE_EV100 = float(META.get("exposureEV100", 9.4))
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
    # White balance (meta.whiteBalanceK): the stage design balances for its 4100 K ceiling field so
    # the room reads neutral while the 2700-3000 K practicals and her skin stay warm.
    if META.get("whiteBalanceK"):
        s.set_editor_property("override_white_temp", True)
        s.set_editor_property("white_temp", float(META["whiteBalanceK"]))
    ppv.set_editor_property("settings", s)
    w(META.get("lightsLog", "lights ok ({placed} of {total} placed, {shadowed} cast shadows, {exterior} tagged "
                            "ExteriorLight), exposure PINNED at EV100 {ev:.2f}").format(
        ev=EXPOSURE_EV100, placed=lights_placed, total=len(LIGHTS), shadowed=lights_shadowed,
        exterior=lights_exterior))
    _wb = white_balance_white(META["whiteBalanceK"]) if META.get("whiteBalanceK") else None
    w("light colours: %d sRGB-encoded (decoded to linear), %d linear, %d by temperature; white balance %s"
      % (sum(1 for l in LIGHTS if "srgb" in l), sum(1 for l in LIGHTS if "srgb" not in l and "linear" in l),
         sum(1 for l in LIGHTS if "srgb" not in l and "linear" not in l and "kelvin" in l),
         ("%.0f K (neutralises linear %s); emissives in cd/m2, display colours" % (float(META["whiteBalanceK"]), ", ".join("%.3f" % c for c in _wb)))
         if _wb else "off"))
    try:
        for _name, _passed, _detail in _stage_ini.gates(LAYOUT):
            w("layout gate %s: %s (%s)" % ("PASS" if _passed else "MISSED", _name, _detail))
    except NameError:
        w("layout gates: skipped (stage_layout.py is not beside this script)")

    # ---------- materials ----------
    # A blockout names its material in the layout ("material": {...}); the asset is created on
    # first use and reused after. Names are EXPLICIT: two create_asset calls for the same package
    # path trip a fatal C++ assertion in SharedPointer.h that Python cannot catch — the whole
    # authoring commandlet once died with appError that way, after a try/except had been written to
    # make it "safe". So a name used twice must carry the same definition, or authoring stops here.
    #
    # With GI off these are pure albedo — they do not light anything; colour bounce is placed as
    # explicit lights in the layout.
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    MEL = unreal.MaterialEditingLibrary

    def paint(spec):
        """Constant-colour set material. The engine's default grey is what made the old room read
        as untextured concrete regardless of lighting."""
        try:
            rgb = spec["linear"]
            mat = tools.create_asset(spec["name"], spec["folder"], unreal.Material, unreal.MaterialFactoryNew())
            if not mat:
                w("paint: create_asset returned None for %s" % spec["name"]); return None
            col = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -380, 0)
            col.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            if not MEL.connect_material_property(col, "RGB", unreal.MaterialProperty.MP_BASE_COLOR):
                MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
            r = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -380, 250)
            r.set_editor_property("r", spec.get("rough", 0.55))
            MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
            MEL.recompile_material(mat)
            return mat
        except Exception as e:
            w("paint warn: %s" % e)
            return None

    def emissive(spec):
        """Lit fittings (troffers, panels): colour times strength into emissive, colour as base."""
        try:
            rgb = spec["linear"]
            mat = tools.create_asset(spec["name"], spec["folder"], unreal.Material, unreal.MaterialFactoryNew())
            col = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -380, 0)
            col.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -180, 0)
            amt = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -380, 160)
            amt.set_editor_property("r", spec["strength"])
            MEL.connect_material_expressions(col, "", mul, "A")
            MEL.connect_material_expressions(amt, "", mul, "B")
            MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
            MEL.recompile_material(mat)
            return mat
        except Exception as e:
            w("emissive warn: %s" % e)
            return None

    def glass(spec):
        """Translucent tinted glass, created once and kept if it already exists."""
        path = spec["folder"] + "/" + spec["name"]
        mat = (unreal.EditorAssetLibrary.load_asset(path)
               if unreal.EditorAssetLibrary.does_asset_exist(path) else None)
        if not mat:
            rgb = spec["linear"]
            mat = tools.create_asset(spec["name"], spec["folder"], unreal.Material, unreal.MaterialFactoryNew())
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
            col = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
            col.set_editor_property("parameter_name", "Tint")
            col.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
            MEL.connect_material_property(col, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
            op = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 180)
            op.set_editor_property("r", spec["opacity"])
            MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
            ro = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 340)
            ro.set_editor_property("r", spec["rough"])
            MEL.connect_material_property(ro, "", unreal.MaterialProperty.MP_ROUGHNESS)
            MEL.recompile_material(mat)
        return mat

    def import_texture(tex):
        """Imports a layout texture ({"file", "dir", "dest"}) and returns the Texture2D, or raises."""
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", INPUT_DIRS[tex["dir"]] + "/" + tex["file"])
        t.set_editor_property("destination_path", tex["dest"])
        t.set_editor_property("automated", True); t.set_editor_property("save", True)
        t.set_editor_property("replace_existing", True)
        tools.import_asset_tasks([t])
        loaded = unreal.load_asset(tex["dest"] + "/" + os.path.splitext(tex["file"])[0])
        if not loaded:
            raise RuntimeError("no texture imported from %s/%s" % (INPUT_DIRS[tex["dir"]], tex["file"]))
        return loaded

    def texture_unlit_masked(spec):
        """A decal-like unlit sign (the wordmark): texture RGB to emissive, alpha to opacity mask."""
        tex = import_texture(spec["texture"])
        mat = tools.create_asset(spec["name"], spec["folder"], unreal.Material, unreal.MaterialFactoryNew())
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        sample = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -400, 0)
        sample.set_editor_property("texture", tex)
        MEL.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        MEL.recompile_material(mat)
        return mat

    def texture_emissive(spec):
        """A powered screen: the texture as base colour, and times strength as emissive, so it
        glows with GI off instead of rendering dimmer than the cabinet beside it."""
        tex = import_texture(spec["texture"])
        mat = tools.create_asset(spec["name"], spec["folder"], unreal.Material, unreal.MaterialFactoryNew())
        ts = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -420, 0)
        ts.set_editor_property("texture", tex)
        MEL.connect_material_property(ts, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -220, 180)
        MEL.connect_material_expressions(ts, "RGB", mul, "A")
        k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -380, 240)
        k.set_editor_property("r", spec["strength"])
        MEL.connect_material_expressions(k, "", mul, "B")
        MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        for _f in spec.get("flags", []):
            try: mat.set_editor_property(_f, True)
            except Exception: pass
        MEL.recompile_material(mat)
        return mat

    MATERIAL_BUILDERS = {"paint": paint, "emissive": emissive, "glass": glass,
                         "texture_unlit_masked": texture_unlit_masked, "texture_emissive": texture_emissive}
    MATERIALS = {}                   # name -> (definition, asset or None)
    def material_for(spec):
        name = spec["name"]
        if name in MATERIALS:
            if MATERIALS[name][0] != spec:
                raise RuntimeError("material %s is defined twice with different settings" % name)
            return MATERIALS[name][1]
        try:
            mat = MATERIAL_BUILDERS[spec.get("type", "paint")](spec)
        except Exception as e:
            w("material %s not built: %s" % (name, e))
            mat = None
        MATERIALS[name] = (spec, mat)
        return mat

    # DERIVED MATERIALS. A blockout with a "color" (sRGB hex) and no explicit material gets a
    # material INSTANCE of one of three masters, keyed by its look: opaque (colour, rough, metallic),
    # emissive (colour times strength, so practicals glow with GI off) or glass (tint, opacity).
    # Instances share their master's shaders, so 900 blockouts cost three compiles, not 900, and two
    # blockouts that look the same share one instance. Instance names are built from the values, so
    # the same look always maps to the same asset.
    STAGE_MATS = "/Game/Diorama/StageMats"
    MASTERS, INSTANCES = {}, {}
    material_failures = []
    # Emissive colours are display colours (see emissive_scene_colour): the white this layout's post
    # process balances to is folded into them here, once.
    WB_WHITE = white_balance_white(META["whiteBalanceK"]) if META.get("whiteBalanceK") else (1.0, 1.0, 1.0)
    # The floor's tile and grout, drawn in the material from WORLD position so a 60 cm tile is 60 cm
    # whatever the blockout's scale. Distance to the nearest grout line in cm, anti-aliased by its
    # screen-space derivative so the far floor does not shimmer. Where a tile spans fewer than about
    # five pixels (its footprint, the geometric mean of the two screen-space derivatives; the 3 cm
    # perforation of the raised-floor panels some metres out) the grid is finer than the screen can show
    # and aliased into a moire in a capture, so it fades to its average coverage by 2.5 pixels, as a
    # mip-mapped texture would. LWCToFloat: the world position input arrives as a large-world-coordinate
    # vector.
    TILE_HLSL = ("float3 W = LWCToFloat(P);\n"
                 "float2 F = abs(frac(W.xy / TileCm) - 0.5) * TileCm;\n"
                 "float D = 0.5 * TileCm - max(F.x, F.y);\n"
                 "float AA = max(fwidth(D), 0.02);\n"
                 "float G = 1.0 - smoothstep(0.5 * GroutCm - AA, 0.5 * GroutCm + AA, D);\n"
                 "float Open = 1.0 - GroutCm / TileCm;\n"
                 "float Px = sqrt(length(ddx(W.xy)) * length(ddy(W.xy))) / TileCm;\n"
                 "return lerp(G, 1.0 - Open * Open, saturate((Px - 0.2) / 0.2));")
    def srgb_lin(hexcol):
        h = hexcol.lstrip("#")
        return tuple((c / 12.92) if c <= 0.04045 else (((c + 0.055) / 1.055) ** 2.4)
                     for c in (int(h[i:i+2], 16) / 255.0 for i in (0, 2, 4)))
    def scalar_param(mat, name, default, x, y):
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", default)
        return e
    def vector_param(mat, name, x, y):
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
        return e
    def connect_rgb(expr, prop):
        if not MEL.connect_material_property(expr, "RGB", prop):
            MEL.connect_material_property(expr, "", prop)
    def master(kind):
        if kind in MASTERS:
            return MASTERS[kind]
        P = unreal.MaterialProperty
        if kind == "glass":
            m = tools.create_asset("M_StageGlass", STAGE_MATS, unreal.Material, unreal.MaterialFactoryNew())
            m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
            connect_rgb(vector_param(m, "Tint", -400, 0), P.MP_BASE_COLOR)
            MEL.connect_material_property(scalar_param(m, "Opacity", 0.15, -400, 200), "", P.MP_OPACITY)
            MEL.connect_material_property(scalar_param(m, "Roughness", 0.05, -400, 320), "", P.MP_ROUGHNESS)
        elif kind == "emissive":
            m = tools.create_asset("M_StageEmissive", STAGE_MATS, unreal.Material, unreal.MaterialFactoryNew())
            col = vector_param(m, "BaseColor", -500, 0)
            mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 120)
            MEL.connect_material_expressions(col, "", mul, "A")
            MEL.connect_material_expressions(scalar_param(m, "Emissive", 4.0, -500, 200), "", mul, "B")
            connect_rgb(col, P.MP_BASE_COLOR)
            MEL.connect_material_property(mul, "", P.MP_EMISSIVE_COLOR)
            MEL.connect_material_property(scalar_param(m, "Roughness", 0.4, -500, 320), "", P.MP_ROUGHNESS)
        elif kind == "tile":
            m = tools.create_asset("M_StageTile", STAGE_MATS, unreal.Material, unreal.MaterialFactoryNew())
            wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1000, 0)
            grid = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -700, 0)
            grid.set_editor_property("code", TILE_HLSL)
            grid.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
            grid.set_editor_property("description", "TileGrout")
            pins = []
            for pin in ("P", "TileCm", "GroutCm"):
                ci = unreal.CustomInput()
                ci.set_editor_property("input_name", pin)
                pins.append(ci)
            grid.set_editor_property("inputs", pins)
            MEL.connect_material_expressions(wp, "", grid, "P")
            MEL.connect_material_expressions(scalar_param(m, "TileCm", 60.0, -1000, 200), "", grid, "TileCm")
            MEL.connect_material_expressions(scalar_param(m, "GroutCm", 0.6, -1000, 300), "", grid, "GroutCm")
            col = MEL.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -350, 0)
            for pin, param in (("A", vector_param(m, "BaseColor", -700, 200)), ("B", vector_param(m, "GroutColor", -700, 400))):
                if not MEL.connect_material_expressions(param, "RGB", col, pin):
                    MEL.connect_material_expressions(param, "", col, pin)
            MEL.connect_material_expressions(grid, "", col, "Alpha")
            MEL.connect_material_property(col, "", P.MP_BASE_COLOR)
            rough = MEL.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -350, 300)
            MEL.connect_material_expressions(scalar_param(m, "Roughness", 0.2, -700, 600), "", rough, "A")
            MEL.connect_material_expressions(scalar_param(m, "GroutRoughness", 0.75, -700, 700), "", rough, "B")
            MEL.connect_material_expressions(grid, "", rough, "Alpha")
            MEL.connect_material_property(rough, "", P.MP_ROUGHNESS)
        else:
            m = tools.create_asset("M_StageOpaque", STAGE_MATS, unreal.Material, unreal.MaterialFactoryNew())
            connect_rgb(vector_param(m, "BaseColor", -400, 0), P.MP_BASE_COLOR)
            MEL.connect_material_property(scalar_param(m, "Roughness", 0.6, -400, 200), "", P.MP_ROUGHNESS)
            MEL.connect_material_property(scalar_param(m, "Metallic", 0.0, -400, 320), "", P.MP_METALLIC)
        MEL.recompile_material(m)
        MASTERS[kind] = m
        return m
    def derived_material(b):
        hexcol = str(b["color"]).lstrip("#").upper()
        colour = srgb_lin("#" + hexcol)
        vectors = []
        if b.get("kind") == "glass" or "opacity" in b:
            kind, params = "glass", [("Opacity", float(b.get("opacity", 0.15))), ("Roughness", float(b.get("rough", 0.05)))]
            name = "MI_Glass_%s_O%03d" % (hexcol, round(params[0][1] * 100))
        elif b.get("emissive"):
            # cd/m2 per unit luminance of the DISPLAY colour; the scene colour carries the balanced
            # white, and a channel over 1 moves into the strength (luminance unchanged).
            scene = emissive_scene_colour(colour, WB_WHITE)
            peak = max(max(scene), 1.0)
            colour = tuple(c / peak for c in scene)
            kind, params = "emissive", [("Emissive", float(b["emissive"]) * peak), ("Roughness", float(b.get("rough", 0.4)))]
            name = "MI_Emit_%s_E%06d" % (hexcol, round(float(b["emissive"]) * 100))
        elif b.get("tileCm"):
            kind = "tile"
            params = [("TileCm", float(b["tileCm"])), ("GroutCm", float(b.get("groutCm", 0.6))),
                      ("Roughness", float(b.get("rough", 0.2))), ("GroutRoughness", float(b.get("groutRough", 0.75)))]
            vectors = [("GroutColor", srgb_lin(b.get("groutColor") or "#BDB7AA"))]
            name = "MI_Tile_%s_T%03d_G%03d" % (hexcol, round(params[0][1]), round(params[1][1] * 10))
        else:
            kind = "opaque"
            params = [("Roughness", float(b.get("rough", 0.6))), ("Metallic", float(b.get("metallic", 0.0)))]
            name = "MI_Op_%s_R%03d_M%03d" % (hexcol, round(params[0][1] * 100), round(params[1][1] * 100))
        if name in INSTANCES:
            return INSTANCES[name]
        try:
            mi = tools.create_asset(name, STAGE_MATS, unreal.MaterialInstanceConstant,
                                    unreal.MaterialInstanceConstantFactoryNew())
            mi.set_editor_property("parent", master(kind))
            r, g, bl = colour
            MEL.set_material_instance_vector_parameter_value(mi, "Tint" if kind == "glass" else "BaseColor",
                                                             unreal.LinearColor(r, g, bl, 1.0))
            for pname, value in vectors:
                MEL.set_material_instance_vector_parameter_value(mi, pname, unreal.LinearColor(value[0], value[1], value[2], 1.0))
            for pname, value in params:
                MEL.set_material_instance_scalar_parameter_value(mi, pname, value)
            MEL.update_material_instance(mi)
        except Exception as e:
            material_failures.append(name)
            w("material %s not built: %s" % (name, e))
            mi = None
        INSTANCES[name] = mi
        return mi

    # ---------- blockouts ----------
    # A blockout is a true-size engine primitive standing in for a set piece until it is authored as
    # a CI prop (the layout's notes are the authoring spec). "size" is in centimetres in the
    # blockout's own frame (x along its yaw); the engine's BasicShapes are 100 cm, centred. A
    # cylinder's "axis" says which of those three extents is its length. The older layout format's
    # "scale" (BasicShapes units) and "shape" cube/plane still work.
    MESHES = {"cube": unreal.load_asset('/Engine/BasicShapes/Cube'),
              "plane": unreal.load_asset('/Engine/BasicShapes/Plane'),
              "cylinder": unreal.load_asset('/Engine/BasicShapes/Cylinder')}
    def blockout_transform(b):
        """(mesh key, scale, yaw, pitch, roll) for a blockout.

        Cylinders are turned with ROLL so their axle lies along the actor's yawed Y axis, which is
        the axis UTapeReelSpinner turns a reel about (it adds to Pitch, and a pitch increment is a
        rotation about the yawed Y axis whatever the roll). So a tagged reel spins about its own
        hub instead of tumbling, and every other cylinder uses the same convention."""
        shape = (b.get("shape") or "box").lower()
        yaw, pitch, roll = b.get("yaw", 0.0), b.get("pitch", 0.0), b.get("roll", 0.0)
        if "scale" in b:
            return ("cube" if shape == "box" else shape), tuple(b["scale"]), yaw, pitch, roll
        sx, sy, sz = [max(float(v), 0.2) / 100.0 for v in b["size"]]
        if shape == "cylinder":
            axis = (b.get("axis") or "z").lower()
            if axis == "x":
                return "cylinder", (sy, sz, sx), yaw + 90.0, pitch, -90.0
            if axis == "y":
                return "cylinder", (sx, sz, sy), yaw, pitch, -90.0
            return "cylinder", (sx, sy, sz), yaw, pitch, roll
        return ("cube" if shape == "box" else shape), (sx, sy, sz), yaw, pitch, roll

    def place_text(b):
        """A TextRenderActor (no mesh): signage the cook needs no imported asset for."""
        a = spawn(unreal.TextRenderActor, (b["x"], b["y"], b["z"]), yaw=b.get("yaw", 0.0),
                  pitch=b.get("pitch", 0.0), roll=b.get("roll", 0.0))
        a.set_actor_label(b.get("label") or b["name"])
        trc = a.text_render
        trc.set_text(unreal.Text(b["text"]))
        trc.set_editor_property("world_size", b.get("textWorldSize", 20.0))
        trc.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        rgba = b.get("textRGBA", [236, 236, 232, 255])
        trc.set_editor_property("text_render_color", unreal.Color(*rgba))
        return True

    def place_face_text(b):
        """Text on a box's face (the -X face by default, the one a wall sign shows the room), centred,
        cap height textSize cm, coloured textColor. A TextRenderActor reads from its local +X, so a
        sign on the -X face is turned 180 degrees from its box."""
        face = 1.0 if (b.get("textFace") or "-x") == "+x" else -1.0
        sx = max(float(b["size"][0]), 0.2)
        a = math.radians(b.get("yaw", 0.0))
        off = face * (sx / 2.0 + 0.3)
        t = spawn(unreal.TextRenderActor, (b["x"] + off * math.cos(a), b["y"] + off * math.sin(a), b["z"]),
                  yaw=b.get("yaw", 0.0) + (180.0 if face < 0 else 0.0))
        t.set_actor_label(b["name"] + "_text")
        trc = t.text_render
        trc.set_text(unreal.Text(str(b["text"])))
        size = float(b.get("textSize") or 0.7 * float(b["size"][2])) / 0.72
        trc.set_editor_property("world_size", size)
        trc.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        trc.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
        h = str(b.get("textColor") or "#111111").lstrip("#")
        trc.set_editor_property("text_render_color", unreal.Color(int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), 255))
        if b.get("textSpacing"):
            trc.set_editor_property("horiz_spacing_adjust", (float(b["textSpacing"]) - 1.0) * size * 0.5)
        return True

    PLACED_BLOCKOUTS = {}            # name -> actor, for attachment
    texts_placed = 0
    def place_blockout(b):
        global texts_placed
        shape = (b.get("shape") or "box").lower()
        if shape == "none":
            return place_text(b) if b.get("text") else False
        mesh_key, scale, yaw, pitch, roll = blockout_transform(b)
        mesh = MESHES.get(mesh_key)
        if not mesh:
            w("blockout %s: no mesh for shape %r" % (b["name"], shape)); return False
        if b.get("material"):
            mat = material_for(b["material"])
        elif b.get("color"):
            mat = derived_material(b)
        else:
            mat = None
        if b.get("material") and not mat and b.get("required"):
            # A screen or sign whose material failed is not placed at all: an unpowered CRT that
            # still counts as "placed" is exactly the lie the summary counts exist to catch.
            return False
        a = spawn(unreal.StaticMeshActor, (b["x"], b["y"], b["z"]), yaw=yaw, pitch=pitch, roll=roll)
        if b.get("mobility") == "movable":
            # Before the mesh: a set piece the runtime moves (a reel, or a window riding on one)
            # must be Movable, or the cook freezes it in place.
            a.static_mesh_component.set_editor_property("mobility", MOVABLE)
        a.static_mesh_component.set_static_mesh(mesh)
        a.set_actor_scale3d(unreal.Vector(*scale))
        if mat:
            a.static_mesh_component.set_material(0, mat)
        if b.get("label") or "size" in b:
            a.set_actor_label(b.get("label") or b["name"])
        if b.get("tags"):
            a.tags = list(b["tags"])
        if "lightingChannels" in b:
            a.static_mesh_component.set_editor_property("lighting_channels", lighting_channels(b["lightingChannels"]))
        if b.get("castShadow") is False:
            # A light fitting's own parts (the desk lamp's shade and bulb) must not shadow its light.
            a.static_mesh_component.set_editor_property("cast_shadow", False)
        if b.get("text") and "size" in b:
            if place_face_text(b):
                texts_placed += 1
        PLACED_BLOCKOUTS[b["name"]] = a
        # Read the mesh BACK off the component. set_static_mesh on a Static-mobility component is the
        # step that can quietly do nothing, and it is the only one whose failure leaves a correctly
        # placed, correctly named, invisible actor behind.
        return bool(a.static_mesh_component.static_mesh)

    blockouts_placed = 0
    blockouts_by_kind = {}
    for b in BLOCKOUTS:
        try:
            ok = place_blockout(b)
        except Exception as e:
            w("blockout %s warn: %s" % (b.get("name"), e)); ok = False
        blockouts_placed += 1 if ok else 0
        k = b.get("kind") or b.get("group") or "other"
        placed_k, total_k = blockouts_by_kind.get(k, (0, 0))
        blockouts_by_kind[k] = (placed_k + (1 if ok else 0), total_k + 1)
        tally(b.get("group"), ok)

    # Attachment, once everything exists: a part that must move with another (a reel's flange
    # windows ride on the reel the spinner turns) is attached to it, and the attachment is read back.
    attach_expected = sum(1 for b in BLOCKOUTS if b.get("attachTo"))
    attached = 0
    for b in BLOCKOUTS:
        parent_name = b.get("attachTo")
        if not parent_name:
            continue
        child, parent = PLACED_BLOCKOUTS.get(b["name"]), PLACED_BLOCKOUTS.get(parent_name)
        if not child or not parent:
            w("attach %s -> %s: %s missing" % (b["name"], parent_name, "child" if not child else "parent"))
            continue
        try:
            child.attach_to_actor(parent, "", unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                  unreal.AttachmentRule.KEEP_WORLD, False)
            if child.get_attach_parent_actor() == parent:
                attached += 1
            else:
                w("attach %s -> %s did not take" % (b["name"], parent_name))
        except Exception as e:
            w("attach %s -> %s FAILED: %s" % (b["name"], parent_name, e))
    if BLOCKOUTS and "size" in BLOCKOUTS[0]:
        w("blockouts placed: %d of %d (%s); %d instanced materials on %d masters; %d signs; %d of %d attached"
          % (blockouts_placed, len(BLOCKOUTS),
             ", ".join("%s %d/%d" % (k, v[0], v[1]) for k, v in sorted(blockouts_by_kind.items())),
             sum(1 for v in INSTANCES.values() if v), len(MASTERS), texts_placed, attached, attach_expected))

    # ---------- props: measure -> normalize scale -> ground -> place ----------
    # A prop with targetCm is scaled from its MEASURED bounds so its largest dimension is that many
    # centimetres, then its base is put on its placement height. Sketchfab FBX/.blend sources carry
    # wildly inconsistent unit scales (one shipped a 29-metre "desk terminal" that swallowed the
    # camera), and measuring in UE is the only self-correcting place to do it. A prop with no
    # targetCm is placed at the file's own scale, origin on its location.
    def place_prop(p):
        slot = slot_of(p)
        sm = imported.get(slot)
        if not sm:
            w("SKIP place %s (no mesh)" % slot); return False, 0
        pos = (p["x"], p["y"], p["z"])
        yaw = p.get("yaw", 0.0)
        a = spawn(unreal.StaticMeshActor, pos, yaw=yaw)
        a.static_mesh_component.set_static_mesh(sm)
        a.set_actor_label(p.get("label") or ("prop_" + slot))
        if "targetCm" in p:
            box = sm.get_bounding_box()
            size = box.max - box.min
            max_dim = max(size.x, size.y, size.z)
            sc = (p["targetCm"] / max_dim) if max_dim > 0.001 else 1.0
            a.set_actor_scale3d(unreal.Vector(sc, sc, sc))
            # sit the mesh's base exactly on its placement height (no float/sink)
            a.set_actor_location(unreal.Vector(pos[0], pos[1], pos[2] - box.min.z * sc), False, False)
        elif "scale" in p:
            # The stage design places each GLB by its scene origin at a declared uniform scale
            # (meta.props_convention), measured against the real machine, so nothing is normalised.
            s = float(p["scale"])
            a.set_actor_scale3d(unreal.Vector(s, s, s))
        for edit in ("clipLocal", "rotateLocal"):
            if p.get(edit) != SLOTS.get(slot, {}).get(edit):
                # The edit is made once per imported file, so every prop placing a slot must ask for the same one.
                w("prop %s warn: %s %s differs from slot %s's %s (the slot's edit is what is placed)"
                  % (p["name"], edit, p.get(edit), slot, SLOTS.get(slot, {}).get(edit)))
        if p.get("tags"):
            a.tags = list(p["tags"])
        reels = 0
        # Separately-pivoted sub-meshes (tape reels): each spawned at its hub offset, rotated with
        # the cabinet. Interchange does not keep a node's translation when it imports the node as a
        # standalone StaticMesh, so the offset has to be restored here, and the reel must be
        # Movable BEFORE its mesh is assigned: UTapeReelSpinner rotates it every frame, and Static
        # mobility would leave it frozen in the cook.
        for r in p.get("reels", []):
            rm = slot_meshes.get(slot, {}).get(r["mesh"])
            if rm is None:
                w("%s reel has no mesh: %s" % (slot, r["mesh"])); continue
            ox, oy, oz = r["offset"]
            angle = math.radians(yaw)
            rx = ox * math.cos(angle) - oy * math.sin(angle)
            ry = ox * math.sin(angle) + oy * math.cos(angle)
            ra = spawn(unreal.StaticMeshActor, (pos[0] + rx, pos[1] + ry, pos[2] + oz), yaw=yaw)
            ra.static_mesh_component.set_editor_property("mobility", MOVABLE)
            ra.static_mesh_component.set_static_mesh(rm)
            ra.tags = list(r.get("tags", ["TapeReel"]))
            ra.set_actor_label(r.get("label") or ("Reel_%s_%s" % (p["name"], r["mesh"])))
            reels += 1
        return True, reels

    props_placed = 0
    for p in PROPS:
        try:
            ok, nreels = place_prop(p)
        except Exception as e:
            w("prop %s warn: %s" % (p.get("name"), e)); ok, nreels = False, 0
        props_placed += 1 if ok else 0
        tally(p.get("group"), ok, nreels)
    w("props placed: %d of %d (%d slot(s) refused by the NoAI guard)" % (props_placed, len(PROPS), len(refused)))

    # ---------- re-home the glTF prop materials into /Game ----------
    # THE reason the props render pale. Interchange parents every imported material instance to
    #   /InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque_DS
    # which is PLUGIN content, not /Game. The cook stages /Game, so in the shipped build that
    # parent is missing, every instance falls back to the engine default, and each prop's real
    # colour -- carried in BaseColorFactor -- is silently discarded. It looks exactly like
    # "textures failed to load", which is why it went unexplained for so long.
    #
    # The master below deliberately declares parameters with the SAME NAMES Interchange used
    # (BaseColorTexture / BaseColorFactor / MetallicFactor / RoughnessFactor). Re-parenting then
    # preserves every per-prop override automatically -- no copying values, nothing to drift.
    # A slot marked keepImportedMaterials is left alone (its emissive lamps would not survive
    # this master, which drops emission).
    KEEP_IMPORTED = ["/Game/Diorama/%s/" % slot for slot, p in SLOTS.items() if p.get("keepImportedMaterials")]
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
            if any(str(a.package_name).startswith(k) for k in KEEP_IMPORTED):
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

    # ---------- per-prop recolours (materialColors) and opaque materials (opaque) ----------
    # The design recolours a prop by material name (the 1401 CPU's doors, band and console). Each
    # imported instance whose name contains a listed material name gets that colour as its
    # BaseColorFactor, the parameter M_PropGLTF multiplies its texture by, and is saved.
    # A glTF BLEND or MASK material keeps Interchange's translucent blend-mode override after the
    # re-parent, and M_PropGLTF has no opacity, so it renders as a dark see-through surface with GI
    # off (the Kenbak-1's front panel came out black, the SRI mouse's wooden body likewise). A prop
    # marked opaque drops that override on every instance of its slot, so they render as M_PropGLTF
    # does: opaque. Unmarked slots keep the import's blend modes.
    for p in PROPS:
        colours = p.get("materialColors") or {}
        opaque = bool(p.get("opaque"))
        if not (colours or opaque) or not imported.get(slot_of(p)):
            continue
        done = set()
        made_opaque = 0
        for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(
                "/Game/Diorama/" + slot_of(p), recursive=True):
            if str(a.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            if opaque:
                mi = unreal.load_asset(str(a.package_name))
                ov = mi.get_editor_property("base_property_overrides")
                if (ov.get_editor_property("override_blend_mode")
                        and ov.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_OPAQUE):
                    ov.set_editor_property("override_blend_mode", False)
                    mi.set_editor_property("base_property_overrides", ov)
                    MEL.update_material_instance(mi)
                    unreal.EditorAssetLibrary.save_loaded_asset(mi)
                    made_opaque += 1
            for mname, hexcol in colours.items():
                if mname in str(a.asset_name):
                    mi = unreal.load_asset(str(a.package_name))
                    h = hexcol.lstrip("#")
                    lin = [(c / 12.92) if c <= 0.04045 else (((c + 0.055) / 1.055) ** 2.4)
                           for c in (int(h[i:i+2], 16) / 255.0 for i in (0, 2, 4))]
                    MEL.set_material_instance_vector_parameter_value(mi, "BaseColorFactor", unreal.LinearColor(*lin, 1.0))
                    unreal.EditorAssetLibrary.save_loaded_asset(mi)
                    done.add(mname)
        if colours:
            w("%s: recoloured %d of %d materials%s" % (p["name"], len(done), len(colours),
              "" if len(done) == len(colours) else " (not found: %s)" % ", ".join(sorted(set(colours) - done))))
        if opaque:
            w("%s: opaque: %d material instance(s) lost a translucent blend-mode override" % (p["name"], made_opaque))

    # ---------- stage marks and camera TargetPoints ----------
    # The runtime director finds the set's marks by TAG (labels do not survive the cook):
    #   StageMark.Home      the resident's home mark, yawed to her home facing; every resident-frame
    #                       shot (ARRIVAL, HOME, CLOSE, FAREWELL) is solved from it and her eyes
    #   StageCam.<Shot>[.i] the world-frame shots (FRIEZE, the cutaways) and the lobby stills, one
    #                       point per key, aimed at the key's look-at
    # TargetPoints are engine actors, so the content-only authoring project can place them.
    def place_target(tag, pos, look=None, yaw=None, level=True):
        pitch = 0.0
        if look is not None:
            dx, dy, dz = look[0] - pos[0], look[1] - pos[1], look[2] - pos[2]
            yaw = math.degrees(math.atan2(dy, dx))
            if not level:
                pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
        t = spawn(unreal.TargetPoint, pos, yaw=yaw or 0.0, pitch=pitch)
        t.tags = [tag]
        t.set_actor_label(tag)
        return True
    targets_expected = 0
    targets_placed = 0
    if "homeFacingYaw" in RES:
        targets_expected += 1
        targets_placed += 1 if place_target("StageMark.Home", (RES["x"], RES["y"], RES["z"]),
                                            yaw=float(RES["homeFacingYaw"])) else 0
    for s in SHOTS:
        tag = s.get("targetPoint")
        if not tag or s.get("reference"):
            continue
        keys = s.get("keys") or []
        for i, k in enumerate(keys):
            targets_expected += 1
            try:
                ok = place_target(tag if len(keys) == 1 else "%s.%d" % (tag, i), k["pos"], k.get("lookAt"),
                                  level=bool(s.get("level", True)))
            except Exception as e:
                w("TargetPoint %s warn: %s" % (tag, e)); ok = False
            targets_placed += 1 if ok else 0
    if targets_expected:
        w("TargetPoints placed: %d of %d" % (targets_placed, targets_expected))

    # ---------- the resident ----------
    # Default (MHLLM_RESIDENT=mannequin): Epic's UE mannequin, placed further down. Opt-in: the
    # MetaHuman "Seo" (metahuman) or the Reallusion "Nia" (nia); see WHICH RESIDENT at the top.
    #
    # Seo: converted from mhc_seo.mhpkg (5.6 EditableCharacter): assembled in a local 5.8 editor,
    # exported Face+Body+Outfit to FBX (version-agnostic), re-imported HERE into 5.5 as
    # skeletal meshes. Face carries 849 morph targets = the MetaHuman facial-blendshape rig
    # (the future viseme source). Placed at the camera's look target.
    # THE MARK comes from the layout (resident x, y, z). actorYaw: these residents' meshes face 90
    # degrees off the actor's forward axis (visual yaw = actor yaw + 90), so actor yaw 90 faces -X.
    # Always confirm facing by looking for EYES in a captured frame, not by reasoning about the
    # forward vector: the old -90 was 180 degrees out and turned her BACK to the camera, which went
    # unnoticed while she was still untextured grey.
    RES_POS, RES_YAW = (RES["x"], RES["y"], RES["z"]), float(RES.get("actorYaw", 90.0))

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
    # Facing: same +90 intrinsic offset as the other residents, so the same actor yaw.
    NIA_YAW = RES_YAW
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
    if nia_placed or RESIDENT == "mannequin":
        # Nia is the resident, or the default mannequin is: skip the Seo import entirely (it also
        # saves ~2 min). A mannequin build never reads MH_DIR, so Seo cannot reach its map or cook.
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
    if not nia_placed and RESIDENT != "mannequin":
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
    if RESIDENT == "mannequin" or (placed_seo == 0 and not nia_placed):
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

    # ---------- sounds ----------
    # Authored into the MAP rather than driven from C++: a loop that plays and never reacts has no
    # runtime decisions to make, and keeping it in the level keeps it out of the cook-critical path.
    # Each layout sound is imported, set looping and placed; "volume" and "spatialize" are applied
    # only when the layout gives them (a bed placed at the camera is non-spatialized, so it does not
    # swing across the stereo field while the camera moves).
    for snd in SOUNDS:
        try:
            st = unreal.AssetImportTask()
            st.filename = INPUT_DIRS[snd.get("dir", "audio")] + "/" + snd["file"]
            st.destination_path = snd["dest"]
            st.automated = True; st.save = True; st.replace_existing = True
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([st])
            wav = unreal.load_asset(snd["dest"] + "/" + os.path.splitext(snd["file"])[0])
            if wav:
                wav.set_editor_property("looping", True)
                unreal.EditorAssetLibrary.save_loaded_asset(wav)
                sa = spawn(unreal.AmbientSound, (snd["x"], snd["y"], snd["z"]))
                sa.set_actor_label(snd.get("label") or snd["name"])
                sa.audio_component.set_sound(wav)
                if "volume" in snd:
                    sa.audio_component.set_editor_property("volume_multiplier", snd["volume"])
                if "spatialize" in snd:
                    sa.audio_component.set_editor_property("allow_spatialization", bool(snd["spatialize"]))
                w(snd.get("log") or ("sound placed: %s" % snd["name"]))
                tally(snd.get("group"), True)
            else:
                # SAY SO: a failed import used to produce no line at all, so a silent build looked
                # identical in the log to a working one.
                w("%s: no SoundWave imported from %s (left silent)" % (snd["name"], st.filename))
                tally(snd.get("group"), False)
        except Exception as e:
            w("%s: skipped (%s)" % (snd.get("name"), e))
            tally(snd.get("group"), False)

    # ---------- summary lines, one per layout group ----------
    # Each line reports a COUNT of what was really placed (meta.groupLog). A line that prints the
    # same thing whether or not anything was placed is not evidence of anything.
    for group, fmt in (META.get("groupLog") or {}).items():
        w(fmt.format(placed=GROUP_PLACED.get(group, 0), total=GROUP_TOTAL.get(group, 0),
                     reels=GROUP_REELS.get(group, 0)))

    # ---------- camera / player ----------
    # The runtime StageDirector owns the moving camera. This authored camera is only a fallback
    # and intentionally does not auto-activate, which previously overrode the tour during stream.
    # Its pose is the layout's shot labelled StageCamera (HOME in the stage design), solved from her
    # mark and eyeline the way the director solves it:
    #   residentRelative {rCm, thetaDeg, dzCm}: r from her eye anchor E, theta toward her left of the
    #     home facing, dz above her eyes; r and dz scale with her stature (eye height / 0.93), and
    #     E sits eyeForwardCm in front of the mark, toward the lens.
    #   offsetCm / lookOffsetCm (older layouts): x and y from the mark, z from her eyeline.
    # The eyeline comes from the measured mesh (NIA_EYE_Z, or MANNY_EYE_Z for the default
    # mannequin); the layout's eyeZFallback is only for the Seo path.
    cam_shot = next((s for s in SHOTS if s.get("label") == "StageCamera"), None)
    if cam_shot is None:
        raise RuntimeError("stage layout has no shot labelled StageCamera")
    EYE_Z = float(globals().get("NIA_EYE_Z", globals().get("MANNY_EYE_Z", RES.get("eyeZFallback", 165.0))))
    ck = cam_shot["keys"][0]
    if "residentRelative" in ck:
        rr = ck["residentRelative"]
        k = (EYE_Z / 0.93) / float(RES.get("stature_cm", 170.0))
        f = math.radians(float(RES.get("homeFacingYaw", 180.0)))
        fwd = float(META.get("eyeForwardCm", 9.0)) * k
        E = (RES["x"] + fwd * math.cos(f), RES["y"] + fwd * math.sin(f), RES["z"] + EYE_Z)
        a = math.radians(float(RES.get("homeFacingYaw", 180.0)) - float(rr["thetaDeg"]))
        CAM_POS = (E[0] + rr["rCm"] * k * math.cos(a), E[1] + rr["rCm"] * k * math.sin(a), E[2] + rr.get("dzCm", 0.0) * k)
        LOOK_AT = E
    else:
        CAM_POS = (RES["x"] + ck["offsetCm"][0], RES["y"] + ck["offsetCm"][1], EYE_Z + ck["offsetCm"][2])
        LOOK_AT = (RES["x"] + ck["lookOffsetCm"][0], RES["y"] + ck["lookOffsetCm"][1], EYE_Z + ck["lookOffsetCm"][2])
    dx, dy, dz = (LOOK_AT[0]-CAM_POS[0], LOOK_AT[1]-CAM_POS[1], LOOK_AT[2]-CAM_POS[2])
    cam_yaw = math.degrees(math.atan2(dy, dx))
    cam_pitch = math.degrees(math.atan2(dz, math.sqrt(dx*dx + dy*dy)))
    # The field of view is horizontal: given, or derived from a focal length on the 36 mm-wide
    # (16:9) filmback the stage design is written for.
    cam_fov = float(cam_shot["fovDeg"]) if "fovDeg" in cam_shot else \
        math.degrees(2.0 * math.atan(18.0 / float(cam_shot.get("focalMm", 50.0))))
    cam = spawn(unreal.CameraActor, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    cam.set_actor_label(cam_shot["label"])
    # The runtime StageDirector owns the view target and animates its own camera along the tour.
    # Auto-activating this static editor camera overrode the director in Pixel Streaming, so the
    # flythrough ran in logs while viewers stayed on the unmoving authored camera.
    try:
        cam.camera_component.set_editor_property("field_of_view", cam_fov)
        # Let the camera fill the configured 16:9 stream without letterboxing.
        cam.camera_component.set_editor_property("constrain_aspect_ratio", False)
        aspect = cam_shot.get("aspect", [1920.0, 1080.0])
        cam.camera_component.set_editor_property("aspect_ratio", aspect[0] / aspect[1])
    except Exception as e:
        w("fov warn " + str(e))
    if cam_shot.get("playerStart", True):
        spawn(unreal.PlayerStart, CAM_POS, yaw=cam_yaw, pitch=cam_pitch)
    w("StageCamera ok (%s, landscape 16:9, at %.0f %.0f %.0f, yaw=%.1f pitch=%.1f, fov=%.1f): a fallback; the "
      "stage director's camera owns the stream" % (cam_shot["name"], CAM_POS[0], CAM_POS[1], CAM_POS[2],
                                                  cam_yaw, cam_pitch, cam_fov))

    # ---------- self-checks: every expected count comes from the layout ----------
    # A prop slot refused by the NoAI guard is allowed to leave a gap (a stale prop folder gives a
    # licence-clean set with holes rather than no build); everything else the layout lists must
    # exist in the map, or the build stops here.
    expected_props = sum(1 for p in PROPS if slot_of(p) not in refused)
    tagged_reels = sum(1 for b in BLOCKOUTS if "TapeReel" in (b.get("tags") or [])) + \
        sum(len(p.get("reels", [])) for p in PROPS if slot_of(p) not in refused)
    placed_reels_all = sum(1 for b in BLOCKOUTS if "TapeReel" in (b.get("tags") or [])
                           and PLACED_BLOCKOUTS.get(b["name"])) + sum(GROUP_REELS.values())
    checks = [("blockouts", blockouts_placed, len(BLOCKOUTS)),
              ("lights", lights_placed, len(LIGHTS)),
              ("props", props_placed, expected_props),
              ("TapeReel actors", placed_reels_all, tagged_reels),
              ("attachments", attached, attach_expected),
              ("TargetPoints", targets_placed, targets_expected),
              ("material failures", len(material_failures), 0)]
    short = [c for c in checks if c[1] != c[2]]
    w("self-check: %s%s" % (", ".join("%s %d/%d" % c for c in checks),
                            "; all counts match" if not short else "; MISMATCH"))

    ok = les.save_current_level()
    w("save_current_level=%s" % ok)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    # SUCCESS has to MEAN a map was written. It previously printed unconditionally, so a run that
    # authored 60 actors into an unsaved level and wrote nothing to disk still reported SUCCESS --
    # and every downstream gate (m2_clean.sh's `grep -q SUCCESS`) believed it.
    if not ok:
        raise RuntimeError("save_current_level returned False; no .umap was written")
    if short:
        raise RuntimeError("layout self-check failed: " + ", ".join("%s %d of %d" % c for c in short))
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
    # Expected counts come from the LAYOUT, not from literals: every prop that carries reels is a
    # tape station, and each of its reels must have been spawned.
    expected729 = sum(1 for p in PROPS if p.get("reels"))
    expected_reels = sum(len(p["reels"]) for p in PROPS if p.get("reels"))
    placed729 = sum(GROUP_PLACED.get(g, 0) for g in set(p.get("group") for p in PROPS if p.get("reels")))
    placed_reels = sum(GROUP_REELS.get(g, 0) for g in set(p.get("group") for p in PROPS if p.get("reels")))
    if placed729 != expected729 or placed_reels != expected_reels:
        raise RuntimeError("tape asset placement incomplete: bodies %d/%d, reels %d/%d"
                           % (placed729, expected729, placed_reels, expected_reels))
    w("SUCCESS")
except Exception as e:
    w("FAIL " + str(e))
    w(traceback.format_exc())
