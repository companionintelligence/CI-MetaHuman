import unreal, os
R="/out/MHPROBE.txt"; lines=[]
def w(s): lines.append(str(s)); unreal.log("MHP: "+str(s)); open(R,"w").write("\n".join(lines))
def imp(fbx, dest, morphs=True):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    sk = ui.skeletal_mesh_import_data
    sk.set_editor_property("import_morph_targets", morphs)
    sk.set_editor_property("convert_scene", True)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("options", ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    mesh=None
    for a in ar.get_assets_by_path(dest, recursive=True):
        if str(a.asset_class_path.asset_name)=="SkeletalMesh": mesh=unreal.load_asset(str(a.package_name))
    if not mesh: w("  %s -> NO SKELETALMESH"%os.path.basename(fbx)); return
    # bounds
    try:
        b=mesh.get_bounds(); ext=b.box_extent; dim=(ext.x*2,ext.y*2,ext.z*2)
    except Exception as e:
        dim=("?",str(e))
    # morph targets
    nm="?"
    try: nm=len(mesh.get_editor_property("morph_targets"))
    except Exception:
        try: nm=len(mesh.morph_targets)
        except Exception as e2: nm="n/a(%s)"%str(e2)[:30]
    w("  %s -> OK  dims(cm)=%s  morphs=%s" % (os.path.basename(fbx), dim, nm))

w("=== import Seo FBX into dev-5.5 ===")
imp("/mh/fbx/SKM_MHC_Seo_BodyMesh.fbx", "/Game/MHSeo/Body", morphs=False)
imp("/mh/fbx/SKM_MHC_Seo_FaceMesh.fbx", "/Game/MHSeo/Face", morphs=True)
w("MHPROBE_DONE")
