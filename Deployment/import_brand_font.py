"""Import Montserrat as a Slate-usable UFont, so the HUD can meet the CI style guide.

CI-Common's UI-STYLE-GUIDE.md mandates Montserrat. The web surfaces pull it from Google Fonts,
which Slate cannot do, so the HUD shipped with the engine's Roboto as a documented substitute. The
TTFs are vendored in the org already (CI-Capture/assets/fonts, SIL Open Font License, so embedding
and redistribution are permitted) and are copied into Content/Fonts by this repo.

A .ttf on disk is not enough on its own: Slate needs a UFont asset whose typefaces name the faces,
because FSlateFontInfo resolves a (font object, typeface name) pair. Pointing FSlateFontInfo at a
raw file path instead does work in the editor and then fails in a cooked container, where that path
does not exist -- exactly the class of bug this project has been bitten by before, so it is not
worth the shortcut.

Weight ladder follows the guide (200/400/500/600/700) as closely as three faces allow:
    Light -> the 200 slot, Regular -> 400, SemiBold -> 600/700.
The 500 slot resolves to Regular; SemiBold is closer to 600 than Bold is, which is why SemiBold was
chosen over Bold when only three faces could be vendored.

Run:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=Deployment/import_brand_font.py \
      -unattended -nullrhi -nosplash -stdout -utf8output
Writes Saved/FONT_IMPORT.txt.
"""
import os
import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "FONT_IMPORT.txt")
lines = []


def w(m):
    lines.append(str(m))
    unreal.log("FONTIMPORT: %s" % m)


DEST = "/Game/Fonts"
SRC_DIR = os.path.join(unreal.Paths.project_content_dir(), "Fonts")
FACES = [("Montserrat-Light", "Light"),
         ("Montserrat-Regular", "Regular"),
         ("Montserrat-SemiBold", "SemiBold")]

tools = unreal.AssetToolsHelpers.get_asset_tools()
imported = {}

for filename, typeface in FACES:
    src = os.path.join(SRC_DIR, filename + ".ttf")
    if not os.path.exists(src):
        w("MISSING source %s -- skipping" % src)
        continue

    pkg = "%s/%s" % (DEST, filename)
    existing = unreal.load_asset(pkg)
    if existing:
        w("already imported: %s" % pkg)
        imported[typeface] = existing
        continue

    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = DEST
    task.destination_name = filename
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])

    obj = unreal.load_asset(pkg)
    if obj:
        w("imported %s -> %s (%s)" % (filename, pkg, type(obj).__name__))
        imported[typeface] = obj
    else:
        w("IMPORT FAILED for %s" % filename)

w("faces available: %s" % ", ".join(sorted(imported.keys())))

# The composite Font. Runtime cache type is required for Slate; Offline bakes a texture atlas at a
# fixed size, which is wrong for a HUD that is rendered at several sizes and captured into a
# Pixel Streaming back buffer.
font_pkg = "%s/F_Montserrat" % DEST
font = unreal.load_asset(font_pkg)
if not font:
    font = tools.create_asset("F_Montserrat", DEST, unreal.Font, unreal.FontFactory())
    w("created %s" % font_pkg)

if font:
    try:
        font.set_editor_property("font_cache_type", unreal.FontCacheType.RUNTIME)
    except Exception as e:
        w("could not set font_cache_type: %s" % e)

    typefaces = []
    for _, typeface in FACES:
        face = imported.get(typeface)
        if not face:
            continue
        entry = unreal.TypefaceEntry()
        try:
            entry.set_editor_property("name", typeface)
            entry.set_editor_property("font", unreal.FontData(font_face_asset=face))
            typefaces.append(entry)
        except Exception as e:
            w("typeface entry '%s' failed: %s" % (typeface, e))

    if typefaces:
        try:
            comp = font.get_editor_property("composite_font")
            default = comp.get_editor_property("default_typeface")
            default.set_editor_property("fonts", typefaces)
            comp.set_editor_property("default_typeface", default)
            font.set_editor_property("composite_font", comp)
            unreal.EditorAssetLibrary.save_asset(font_pkg)
            w("F_Montserrat typefaces: %s" % ", ".join(t.get_editor_property("name") for t in typefaces))
        except Exception as e:
            w("composite font wiring FAILED: %s" % e)
    else:
        w("no typeface entries built -- F_Montserrat left empty")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
unreal.log("FONTIMPORT: wrote %s" % OUT)

# Font import needs a live Slate application, which the -run=pythonscript COMMANDLET never has --
# it asserts CurrentApplication.IsValid() and takes the editor down. So this script is meant to be
# run by the full editor via -ExecutePythonScript, and has to close it again itself or the run
# never returns.
if unreal.SystemLibrary.get_command_line().find("ExecutePythonScript") != -1:
    unreal.log("FONTIMPORT: quitting editor")
    unreal.SystemLibrary.quit_editor()
