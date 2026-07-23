# Headless FBX export of the ASSEMBLED MHC_Seo skeletal meshes + baked textures. Plain asset
# export (UExporter) — synchronous, no deadlock (unlike the MetaHuman build/export pipeline).
import unreal, os
OUT = "C:/devel/_mh-convert/out/seo"
os.makedirs(OUT + "/fbx", exist_ok=True)
os.makedirs(OUT + "/textures", exist_ok=True)
R = "C:/devel/_mh-convert/EXPORT_FBX.txt"
lines = []
def w(s):
    lines.append(str(s)); unreal.log("FBX: " + str(s)); open(R, "w").write("\n".join(lines))

ar = unreal.AssetRegistryHelpers.get_asset_registry()
# scope to the assembled character (+ the shared Face archetype in case Body/Face ref it)
sk, tex = [], []
for base in ("/Game/MetaHumans/MHC_Seo",):
    for a in ar.get_assets_by_path(base, recursive=True):
        cls = str(a.asset_class_path.asset_name); pn = str(a.package_name)
        if cls == "SkeletalMesh": sk.append(pn)
        elif cls == "Texture2D": tex.append(pn)
w("MHC_Seo: %d SkeletalMesh, %d Texture2D" % (len(sk), len(tex)))
for m in sk: w("  SK: " + m)

def export(asset_path, filename):
    asset = unreal.load_asset(asset_path)
    if asset is None:
        w("  load FAIL " + asset_path); return
    t = unreal.AssetExportTask()
    t.set_editor_property("object", asset)
    t.set_editor_property("filename", filename)
    t.set_editor_property("automated", True)
    t.set_editor_property("prompt", False)
    t.set_editor_property("replace_identical", True)
    ok = unreal.Exporter.run_asset_export_task(t)
    sz = os.path.getsize(filename) if os.path.exists(filename) else 0
    w("  %-32s -> %s (%d)" % (asset_path.split('/')[-1], "OK" if ok else "FALSE", sz))

w("=== skeletal meshes -> FBX ===")
for m in sk:
    export(m, OUT + "/fbx/" + m.split("/")[-1] + ".fbx")

w("=== baked textures -> TGA ===")
for tpath in tex:
    export(tpath, OUT + "/textures/" + tpath.split("/")[-1] + ".tga")

w("\n=== disk output ===")
for root, _, files in os.walk(OUT):
    for f in sorted(files):
        p = os.path.join(root, f); w("  %10d  %s" % (os.path.getsize(p), p.replace(OUT + "/", "")))
w("\nEXPORT_FBX_DONE")
unreal.log("FBX_DONE")
