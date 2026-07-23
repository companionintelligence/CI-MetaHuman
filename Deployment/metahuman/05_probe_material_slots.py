import unreal
R="/out/SLOTS.txt"; out=[]
def w(s): out.append(str(s)); unreal.log("SLOT: "+str(s)); open(R,"w").write("\n".join(out))
def imp(fbx, dest):
    ui=unreal.FbxImportUI()
    ui.set_editor_property("import_mesh",True); ui.set_editor_property("import_as_skeletal",True)
    ui.set_editor_property("import_materials",True); ui.set_editor_property("import_textures",False)
    ui.set_editor_property("mesh_type_to_import",unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.skeletal_mesh_import_data.set_editor_property("import_morph_targets",True)
    t=unreal.AssetImportTask()
    t.set_editor_property("filename",fbx); t.set_editor_property("destination_path",dest)
    t.set_editor_property("automated",True); t.set_editor_property("save",False)
    t.set_editor_property("replace_existing",True); t.set_editor_property("options",ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    ar=unreal.AssetRegistryHelpers.get_asset_registry()
    for a in ar.get_assets_by_path(dest,recursive=True):
        if str(a.asset_class_path.asset_name)=="SkeletalMesh": return unreal.load_asset(str(a.package_name))
    return None
for name,fbx in [("BODY","/mh/fbx/SKM_MHC_Seo_BodyMesh.fbx"),("FACE","/mh/fbx/SKM_MHC_Seo_FaceMesh.fbx")]:
    m=imp(fbx,"/Game/Probe/"+name)
    w("=== %s slots ==="%name)
    if not m: w("  NO MESH"); continue
    try:
        mats=m.get_editor_property("materials")
        for i,sm in enumerate(mats):
            slot=sm.get_editor_property("material_slot_name")
            w("  [%d] slot=%s"%(i,slot))
    except Exception as e: w("  err %s"%e)
w("SLOTS_DONE")
