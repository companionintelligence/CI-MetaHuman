# Headless Blender: prop source (FBX or .blend) -> self-contained FBX for Unreal import.
# ROBUST transform handling: parent transforms (Sketchfab roots often carry 0.01 scale
# and/or -90deg rotations) are BAKED into each mesh before measuring, so deleting the
# empties can no longer orphan meshes at raw scale/rotation. Then: texture relink by
# basename, real-world scale normalization, floor alignment, embedded-FBX export.
# Usage: blender --background --python ue_prop_fbx.py -- <src> <textures_dir> <out.fbx> <target_max_dim_m>
import bpy, sys, os, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
src, tex_dir, out, target = argv[0], argv[1], argv[2], float(argv[3])

def stem(name):
    return os.path.splitext(os.path.basename(name))[0].lower()

available = {}
if os.path.isdir(tex_dir):
    for f in os.listdir(tex_dir):
        available.setdefault(stem(f), os.path.join(tex_dir, f))

if src.lower().endswith(".blend"):
    bpy.ops.wm.open_mainfile(filepath=src)
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=src)

relinked = 0
for img in bpy.data.images:
    hit = available.get(stem(img.name))
    if hit:
        img.filepath = hit
        try:
            img.reload()
        except Exception:
            pass
        relinked += 1

meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']

# --- bake parent transforms into the meshes, then flatten rotation+scale into verts ---
if meshes:
    for o in bpy.context.scene.objects:
        o.select_set(False)
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    # world matrix preserved while unparenting: children keep their visual transform
    bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    # bake rotation+scale into vertex data (location left as-is for floor alignment math)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

    # now bounds are true world-space and per-object transforms are identity-rot/scale
    mins = mathutils.Vector((1e9, 1e9, 1e9))
    maxs = mathutils.Vector((-1e9, -1e9, -1e9))
    for o in meshes:
        for corner in o.bound_box:
            wc = o.matrix_world @ mathutils.Vector(corner)
            mins.x, mins.y, mins.z = min(mins.x, wc.x), min(mins.y, wc.y), min(mins.z, wc.z)
            maxs.x, maxs.y, maxs.z = max(maxs.x, wc.x), max(maxs.y, wc.y), max(maxs.z, wc.z)
    size = maxs - mins
    max_dim = max(size.x, size.y, size.z)
    scale = target / max_dim if max_dim > 0 else 1.0
    cx, cy = (mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2
    for o in meshes:
        o.location.x = (o.location.x - cx) * scale
        o.location.y = (o.location.y - cy) * scale
        o.location.z = (o.location.z - mins.z) * scale
        o.scale = (o.scale.x * scale, o.scale.y * scale, o.scale.z * scale)
    # non-mesh leftovers are now safe to drop — nothing depends on them anymore
    for o in list(bpy.context.scene.objects):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)

bpy.ops.export_scene.fbx(
    filepath=out,
    use_selection=False,
    apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE',
    object_types={'MESH'},
    use_mesh_modifiers=True,
    mesh_smooth_type='FACE',
    add_leaf_bones=False,
    path_mode='COPY',
    embed_textures=True,
)
print("UEFBX_OK:", out, "meshes:", len(meshes), "relinked:", relinked)
