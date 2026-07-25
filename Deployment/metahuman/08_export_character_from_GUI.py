# Generalized character exporter — SkeletalMesh -> FBX, Texture2D -> TGA, AnimSequence -> FBX.
#
# Generalizes 03_export_fbx_from_GUI.py (which was hard-coded to MHC_Seo) so any purchased /
# downloaded character can be lifted OUT of its own project and into the Linux 5.5 cook.
#
# WHY FBX rather than migrating the uassets: it strips the character's plugin dependencies. A
# Fab "complete project" character (e.g. Reallusion Character Creator) relies on a Windows-
# oriented setup plugin for its materials; that plugin is not in the Linux cook container, and
# enabling plugins there has a blast radius (see HANDOFF.md Lesson 7). Baked textures + our own
# materials sidestep it entirely — the exact trick that got Seo rendering.
#
# ANIMATION: AnimSequences export too. A Control Rig is a Blueprint and will NOT survive to the
# Linux cook, but a baked idle AnimSequence played on the SkeletalMeshComponent will — that is
# how to keep "she moves" without the plugin.
#
# RUN IT INSIDE THE INTERACTIVE EDITOR (Tools > Execute Python). A headless AssetExportTask FBX
# export faults in a commandlet (HANDOFF-METAHUMAN-SKIN.md §1).
#
#   Set SRC_ROOTS + OUT below, then run.
import unreal, os

SRC_ROOTS = ("/Game/Nia",)                       # content path(s) to sweep, recursive
OUT       = "C:/devel/_mh-convert/out/nia"       # mirrors the out/seo layout
REPORT    = OUT + "/EXPORT.txt"

for sub in ("fbx", "textures", "anim"):
    os.makedirs(OUT + "/" + sub, exist_ok=True)
lines = []
def w(s):
    lines.append(str(s)); unreal.log("EXPORT: " + str(s)); open(REPORT, "w").write("\n".join(lines))

ar = unreal.AssetRegistryHelpers.get_asset_registry()
sk, tex, anim = [], [], []
for base in SRC_ROOTS:
    found = ar.get_assets_by_path(base, recursive=True)
    w("scan %-24s -> %d assets" % (base, len(found)))
    for a in found:
        cls, pn = str(a.asset_class_path.asset_name), str(a.package_name)
        if   cls == "SkeletalMesh": sk.append(pn)
        elif cls == "Texture2D":    tex.append(pn)
        elif cls == "AnimSequence": anim.append(pn)
w("found: %d SkeletalMesh, %d Texture2D, %d AnimSequence" % (len(sk), len(tex), len(anim)))
for m in sk:
    w("  SK:   " + m)
for m in anim:
    w("  ANIM: " + m)

def export(asset_path, filename):
    asset = unreal.load_asset(asset_path)
    if asset is None:
        w("  load FAIL " + asset_path); return False
    t = unreal.AssetExportTask()
    t.set_editor_property("object", asset)
    t.set_editor_property("filename", filename)
    t.set_editor_property("automated", True)
    t.set_editor_property("prompt", False)
    t.set_editor_property("replace_identical", True)
    ok = unreal.Exporter.run_asset_export_task(t)
    sz = os.path.getsize(filename) if os.path.exists(filename) else 0
    w("  %-40s -> %-5s (%d)" % (asset_path.split("/")[-1], "OK" if ok else "FALSE", sz))
    return bool(ok) and sz > 0

w("=== skeletal meshes -> FBX ===")
for m in sk:
    export(m, OUT + "/fbx/" + m.split("/")[-1] + ".fbx")
w("=== textures -> TGA ===")
for t in tex:
    export(t, OUT + "/textures/" + t.split("/")[-1] + ".tga")
w("=== anim sequences -> FBX ===")
for a in anim:
    export(a, OUT + "/anim/" + a.split("/")[-1] + ".fbx")

w("\n=== disk output ===")
total = 0
for root, _, files in os.walk(OUT):
    for f in sorted(files):
        p = os.path.join(root, f); s = os.path.getsize(p); total += s
        w("  %10d  %s" % (s, p.replace(OUT + "/", "")))
w("total bytes: %d" % total)
w("\nEXPORT_DONE")
unreal.log("EXPORT_DONE")
