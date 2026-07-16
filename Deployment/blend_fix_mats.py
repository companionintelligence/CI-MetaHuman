# Headless Blender: rebuild each material as a clean Principled BSDF with its image wired to
# Base Color, then export GLB.
#
# Why: the four .blend-sourced props exported with ZERO textures through BOTH the FBX and
# glTF paths, so every one of them rendered as a blank white block. Exporters only follow an
# image that reaches a Principled BSDF Base Color; these materials carry their image in node
# graphs the exporters don't traverse (or unconnected entirely). Relinking the image file
# (fbx2glb.py) fixes the PATH but not the GRAPH — so we rewire the graph here.
#
# Usage: blender <file.blend> --background --python blend_fix_mats.py -- <textures_dir> <out.glb> <target_max_dim_m>
import bpy, sys, os, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
tex_dir, out, target = argv[0], argv[1], float(argv[2])

def stem(name):
    return os.path.splitext(os.path.basename(name))[0].lower()

available = {}
if os.path.isdir(tex_dir):
    for f in os.listdir(tex_dir):
        available.setdefault(stem(f), os.path.join(tex_dir, f))

# relink images to the textures actually shipped with this prop
for img in bpy.data.images:
    hit = available.get(stem(img.name))
    if hit:
        img.filepath = hit
        try:
            img.reload()
        except Exception:
            pass

def load_image(path):
    for im in bpy.data.images:
        try:
            if im.filepath and os.path.samefile(bpy.path.abspath(im.filepath), path):
                return im
        except Exception:
            pass
    return bpy.data.images.load(path, check_existing=True)

def image_for(mat):
    """The image this material should use.

    Prefer one already in its graph. Several of these .blend props reference NO image node
    at all (the PNGs merely ship beside the .blend), so there is nothing to rewire — the
    texture has to be attached. Match a shipped file to the material by name, then fall back
    to the prop's primary texture, so the machine gets its skin instead of rendering white.
    """
    if mat.use_nodes and mat.node_tree:
        for n in mat.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image:
                return n.image
    if not available:
        return None
    mname = mat.name.lower().replace(" ", "_")
    for st, path in available.items():
        if st in mname or mname in st:
            return load_image(path)
    # fall back to the prop's primary map: the largest shipped texture (body/panel art,
    # not a small glass/illum detail map)
    primary = max(available.values(), key=lambda p: os.path.getsize(p))
    return load_image(primary)

rewired = 0
for mat in bpy.data.materials:
    img = image_for(mat)
    if img is None:
        continue
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out_node = nt.nodes.new('ShaderNodeOutputMaterial'); out_node.location = (400, 0)
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled');     bsdf.location = (100, 0)
    tex = nt.nodes.new('ShaderNodeTexImage');            tex.location = (-250, 0)
    tex.image = img
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    nt.links.new(bsdf.outputs['BSDF'], out_node.inputs['Surface'])
    try:
        bsdf.inputs['Roughness'].default_value = 0.65
        bsdf.inputs['Metallic'].default_value = 0.0
    except Exception:
        pass
    rewired += 1

# normalize to real-world size + floor-align (same contract as the other props)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
if meshes:
    mins = mathutils.Vector((1e9, 1e9, 1e9)); maxs = mathutils.Vector((-1e9, -1e9, -1e9))
    for o in meshes:
        for c in o.bound_box:
            wc = o.matrix_world @ mathutils.Vector(c)
            mins.x, mins.y, mins.z = min(mins.x, wc.x), min(mins.y, wc.y), min(mins.z, wc.z)
            maxs.x, maxs.y, maxs.z = max(maxs.x, wc.x), max(maxs.y, wc.y), max(maxs.z, wc.z)
    size = maxs - mins
    md = max(size.x, size.y, size.z)
    sc = target / md if md > 0 else 1.0
    cx, cy = (mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2
    for o in meshes:
        o.location.x = (o.location.x - cx) * sc
        o.location.y = (o.location.y - cy) * sc
        o.location.z = (o.location.z - mins.z) * sc
        o.scale = (o.scale.x * sc, o.scale.y * sc, o.scale.z * sc)
    for o in list(bpy.context.scene.objects):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)

bpy.ops.export_scene.gltf(
    filepath=out, export_format='GLB', export_yup=True, export_apply=True,
    export_materials='EXPORT', export_image_format='AUTO',
    export_draco_mesh_compression_enable=False,
)
print("FIXGLB_OK:", out, "rewired_materials:", rewired)
