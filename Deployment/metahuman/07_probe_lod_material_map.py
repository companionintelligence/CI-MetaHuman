import unreal
# Dumps the FULL material topology of the re-imported MetaHuman meshes to test the
# "top-level materials[] array is not what the rendered LOD sections resolve to" hypothesis.
# For each mesh: the materials[] slots, and per-LOD `lod_material_map` (the section->slot remap).
# A non-empty lod_material_map on LOD0 whose entries point past the slots the author assigns
# (FACE_SLOTS 0..8) would leave those sections on the FBX-default (gray) material -> the bug.
R = "/out/LODMAP.txt"; out = []
def w(s):
    out.append(str(s)); unreal.log("LODMAP: " + str(s)); open(R, "w").write("\n".join(out))

def imp(fbx, dest):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True); ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_materials", True); ui.set_editor_property("import_textures", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.skeletal_mesh_import_data.set_editor_property("import_morph_targets", True)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx); t.set_editor_property("destination_path", dest)
    t.set_editor_property("automated", True); t.set_editor_property("save", False)
    t.set_editor_property("replace_existing", True); t.set_editor_property("options", ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in ar.get_assets_by_path(dest, recursive=True):
        if str(a.asset_class_path.asset_name) == "SkeletalMesh":
            return unreal.load_asset(str(a.package_name))
    return None

for name, fbx in [("BODY", "/mh/fbx/SKM_MHC_Seo_BodyMesh.fbx"),
                  ("FACE", "/mh/fbx/SKM_MHC_Seo_FaceMesh.fbx")]:
    m = imp(fbx, "/Game/Probe/" + name)
    w("================ %s ================" % name)
    if not m:
        w("  NO MESH"); continue
    # materials[] array
    mats = m.get_editor_property("materials")
    w("materials[] count = %d" % len(mats))
    for i, sm in enumerate(mats):
        mi = sm.get_editor_property("material_interface")
        w("  slot[%2d] name=%-28s mat=%s" % (
            i, sm.get_editor_property("material_slot_name"),
            mi.get_name() if mi else "<none>"))
    # per-LOD material map
    try:
        lodinfo = m.get_editor_property("lod_info")
        w("num LODs (lod_info) = %d" % len(lodinfo))
        for li, info in enumerate(lodinfo):
            try:
                lmm = list(info.get_editor_property("lod_material_map"))
            except Exception as e:
                lmm = "ERR:%s" % e
            w("  LOD%d lod_material_map = %s" % (li, lmm))
    except Exception as e:
        w("lod_info err: %s" % e)
    # sections / render material indices (best-effort — may be unavailable in Python)
    for getter in ("get_num_lods", "get_num_sections", "get_num_imported_vertices"):
        try:
            w("  %s = %s" % (getter, getattr(m, getter)()))
        except Exception:
            pass
w("LODMAP_DONE")
