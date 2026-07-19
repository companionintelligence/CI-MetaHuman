# Stage 2b: export WITHOUT the crashing full build. The imported character is already rigged
# (its .mhdna loaded on import), and export_geometry/export_materials extract standalone
# skeletal meshes + textures without running the cinematic TextureGraph pass that crashed.
# Then FBX-export the meshes to disk (version-agnostic) for the UE 5.5 project.
import unreal, os, traceback
R = "C:/devel/_mh-convert/STAGE2B.txt"
OUT = "C:/devel/_mh-convert/out/seo"
os.makedirs(OUT + "/fbx", exist_ok=True)
lines = []
def w(s):
    lines.append(str(s)); unreal.log("MH2B: " + str(s)); open(R, "w").write("\n".join(lines))

CHAR = "/Game/EditableCharacters/Seo/MHC_Seo"
ss = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
ch = unreal.load_asset(CHAR)
w("loaded %s -> %s" % (CHAR, ch is not None))
try:
    ss.try_add_object_to_edit(ch); w("added to edit")
except Exception as e:
    w("add_to_edit warn: %s" % e)

# 1) geometry -> in-project skeletal meshes (no build, no texture graph)
try:
    gp = unreal.MetaHumanGeometryExportParams()
    gp.set_editor_property("project_path", "/Game/MHExport/Seo")
    gp.set_editor_property("head_skeletal_mesh", True)
    gp.set_editor_property("body_skeletal_mesh", True)
    gp.set_editor_property("full_body_skeletal_mesh", True)
    unreal.MetaHumanCharacterExportBlueprintLibrary.export_geometry(ch, gp)
    w("export_geometry OK")
except Exception as e:
    w("export_geometry ERROR: %s\n%s" % (e, traceback.format_exc()[:600]))

# 2) materials -> textures + MICs in-project
try:
    mp = unreal.MetaHumanMaterialsExportParams()
    mp.set_editor_property("project_path", "/Game/MHExport/Seo")
    mp.set_editor_property("apply_as_overrides", False)
    unreal.MetaHumanCharacterExportBlueprintLibrary.export_materials(ch, mp)
    w("export_materials OK")
except Exception as e:
    w("export_materials ERROR: %s" % e)

# 3) DNA (RigLogic face rig) to disk for 5.5's MetaHuman/RigLogic
try:
    dn = unreal.MetaHumanDNAExportParams()
    dn.set_editor_property("external_path", OUT + "/dna")
    dn.set_editor_property("dna_head", True)
    dn.set_editor_property("dna_body", True)
    os.makedirs(OUT + "/dna", exist_ok=True)
    unreal.MetaHumanCharacterExportBlueprintLibrary.export_dna(ch, dn)
    w("export_dna OK")
except Exception as e:
    w("export_dna ERROR: %s" % e)

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)

# 4) list produced skeletal meshes + textures, then FBX-export each mesh to disk
ar = unreal.AssetRegistryHelpers.get_asset_registry()
meshes, textures = [], 0
for a in ar.get_assets_by_path("/Game/MHExport", recursive=True):
    cls = str(a.asset_class_path.asset_name)
    if cls == "SkeletalMesh": meshes.append(str(a.package_name))
    elif cls == "Texture2D": textures += 1
w("\n=== produced: %d skeletal meshes, %d textures ===" % (len(meshes), textures))
for m in meshes: w("  " + m)

def fbx_export(asset, path):
    t = unreal.AssetExportTask()
    t.set_editor_property("object", asset)
    t.set_editor_property("filename", path)
    t.set_editor_property("automated", True)
    t.set_editor_property("prompt", False)
    t.set_editor_property("replace_identical", True)
    return unreal.Exporter.run_asset_export_task(t)

w("\n=== FBX export to disk ===")
for m in meshes:
    asset = unreal.load_asset(m)
    fn = OUT + "/fbx/" + m.split("/")[-1] + ".fbx"
    try:
        ok = fbx_export(asset, fn)
        w("  %s -> %s (%s)" % (m.split("/")[-1], "ok" if ok else "FALSE",
                               os.path.getsize(fn) if os.path.exists(fn) else "no file"))
    except Exception as e:
        w("  %s FBX ERROR: %s" % (m.split("/")[-1], e))

# 5) report disk output
w("\n=== disk output %s ===" % OUT)
for root, dirs, files in os.walk(OUT):
    for f in files:
        p = os.path.join(root, f)
        w("  %9d  %s" % (os.path.getsize(p), p.replace(OUT, ".")))

w("\nSTAGE2B_DONE")
open(R, "w").write("\n".join(lines))
unreal.log("MH2B_DONE")
