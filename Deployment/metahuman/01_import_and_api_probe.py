# Stage 1: import mhc_seo.mhpkg, confirm the MetaHumanCharacter asset lands, and capture the
# EXACT signatures + param-struct fields for build/export so stage 2 calls them correctly.
import unreal, os
R = "C:/devel/_mh-convert/STAGE1.txt"
lines = []
def w(s):
    lines.append(str(s)); unreal.log("MH1: " + str(s))

MHPKG = "C:/devel/llm-metahuman/mhc_seo.mhpkg"

# --- import the .mhpkg ---
task = unreal.AssetImportTask()
task.filename = MHPKG
task.destination_path = "/Game/EditableCharacters/Seo"
task.automated = True
task.save = True
task.replace_existing = True
try:
    task.factory = unreal.MetaHumanPackageFactory()
    w("set MetaHumanPackageFactory")
except Exception as e:
    w("factory set warn: %s" % e)
try:
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    w("import_asset_tasks returned; imported_object_paths=%s" % list(task.imported_object_paths))
except Exception as e:
    w("IMPORT ERROR: %s" % e)

# --- find any MetaHumanCharacter in the project ---
ar = unreal.AssetRegistryHelpers.get_asset_registry()
found = []
for a in ar.get_assets_by_path("/Game", recursive=True):
    cls = str(a.asset_class_path.asset_name)
    if "MetaHumanCharacter" in cls or cls == "MetaHumanCharacter":
        found.append((str(a.package_name), cls))
w("=== MetaHumanCharacter assets in /Game (%d) ===" % len(found))
for pn, cls in found:
    w("  %s [%s]" % (pn, cls))

# --- signatures we need for stage 2 ---
def doc(obj, name):
    m = getattr(obj, name, None)
    w("\n--- %s ---" % name)
    w((getattr(m, "__doc__", None) or "(no doc)").strip()[:700])

sub = unreal.MetaHumanCharacterEditorSubsystem
doc(sub, "can_build_meta_human")
doc(sub, "build_meta_human")
exp = unreal.MetaHumanCharacterExportBlueprintLibrary
for m in ("export_geometry", "export_dcc", "export_dna", "export_materials", "export_posed_dna"):
    doc(exp, m)

# --- param struct fields ---
def struct_fields(cls_name):
    cls = getattr(unreal, cls_name, None)
    w("\n=== %s fields ===" % cls_name)
    if not cls:
        w("  ABSENT"); return
    try:
        inst = cls()
        d = inst.to_dict() if hasattr(inst, "to_dict") else {}
        for k, v in d.items():
            w("  %s = %r" % (k, v))
    except Exception as e:
        w("  instantiate err: %s" % e)
for s in ("MetaHumanCharacterEditorBuildParameters", "MetaHumanDCCExportParams",
          "MetaHumanGeometryExportParams", "MetaHumanDNAExportParams",
          "MetaHumanMaterialsExportParams", "MetaHumanCharacterAssemblySettings"):
    struct_fields(s)

# --- can we build this character offline? ---
if found:
    ch = unreal.load_asset(found[0][0])
    w("\n=== can_build_meta_human check on %s ===" % found[0][0])
    try:
        ss = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
        # add to edit first (many ops require it)
        try:
            ss.try_add_object_to_edit(ch); w("added to edit")
        except Exception as e:
            w("try_add_object_to_edit warn: %s" % e)
        can = ss.can_build_meta_human(ch)
        w("can_build_meta_human = %r" % (can,))
    except Exception as e:
        w("can_build check err: %s" % e)

open(R, "w").write("\n".join(lines))
unreal.log("MH1_DONE " + R)
