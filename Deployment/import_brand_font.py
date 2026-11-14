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

Each face records only "<Face>.ttf" as the file it came from (record_source below), never the path
this machine imported it from, and the run ends by checking the saved files with
Deployment/check_font_sources.py, the check CI runs on the repository.

Run it in the FULL editor, headless (see the end of this file for why not the commandlet):
  UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript=<this file, a path without spaces> \
      -unattended -nullrhi -nosplash -nosound -stdout -utf8output
Writes Saved/FONT_IMPORT.txt.
"""
import os
import sys

import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "FONT_IMPORT.txt")
lines = []


def w(m):
    lines.append(str(m))
    unreal.log("FONTIMPORT: %s" % m)


DEST = "/Game/Fonts"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC_DIR = os.path.join(PROJECT_DIR, "Content", "Fonts")
FACES = [("Montserrat-Light", "Light"),
         ("Montserrat-Regular", "Regular"),
         ("Montserrat-SemiBold", "SemiBold")]

tools = unreal.AssetToolsHelpers.get_asset_tools()
imported = {}


def record_source(face, value):
    """Make FACE record VALUE as the file it came from, then save it.

    The font factory sets UFontFace::SourceFilename to the path it imported from, and saving repeats
    it in the package's AssetImportData tag. That path is this machine's (f1e06c4's import recorded
    one that climbed to the drive root and through the user's profile folder), and a cook ships
    SourceFilename with the face. VALUE is "<Face>.ttf", the form UAssetImportData stores for a
    source file beside the package: Content/Fonts/<Face>.ttf on every machine. It is set without a
    change notification, because UFontFace::PostEditChangeProperty flushes Slate's font cache
    without checking that there is one."""
    try:
        never = unreal.PropertyAccessChangeNotifyMode.NEVER
        face.set_editor_property("source_filename", value, notify_mode=never)
    except (AttributeError, TypeError):
        # Older Python API without notify_mode: the full editor can take the notification.
        face.set_editor_property("source_filename", value)
    face.modify(True)
    return unreal.EditorAssetLibrary.save_loaded_asset(face, False)


for filename, typeface in FACES:
    source = filename + ".ttf"
    src = os.path.join(SRC_DIR, source)
    if not os.path.exists(src):
        w("MISSING source Content/Fonts/%s -- skipping" % source)
        continue

    pkg = "%s/%s" % (DEST, filename)
    face = unreal.load_asset(pkg)
    if face:
        action = "already imported"
    else:
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = DEST
        task.destination_name = filename
        task.automated = True
        task.replace_existing = True
        task.save = False  # saved by record_source(), once SourceFilename names only the file
        tools.import_asset_tasks([task])
        face = unreal.load_asset(pkg)
        if not face:
            w("IMPORT FAILED for %s" % filename)
            continue
        action = "imported from Content/Fonts/%s" % source

    # A face imported earlier may record a path, as f1e06c4's did: it is re-saved the same way.
    if face.get_editor_property("source_filename") != source:
        if not record_source(face, source):
            w("SAVE FAILED for %s" % pkg)
            continue
        action += ", saved with SourceFilename %s" % source
    w("%s -> %s (%s): %s" % (filename, pkg, type(face).__name__, action))
    imported[typeface] = face

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
        try:
            # UE 5.8's Python API has no TypefaceEntry: this fails there, F_Montserrat is left
            # unsaved, and the run goes on to the check below (CIBrandStyle.h composes the font).
            entry = unreal.TypefaceEntry()
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

# What reached disk, by the engine-free check CI runs on the repository: every Content/Fonts asset
# must record its source as a bare file name beside it, with nothing path-like left in it.
sys.path.insert(0, os.path.join(PROJECT_DIR, "Deployment"))
import check_font_sources  # noqa: E402

for path, status, message in check_font_sources.check(PROJECT_DIR):
    w("%s: %s, %s" % (path, status, message))

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
