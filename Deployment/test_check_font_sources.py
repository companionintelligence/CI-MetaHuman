"""Tests for check_font_sources.py, on made-up packages: no engine, no Git LFS, no network.

    python -I Deployment/test_check_font_sources.py

Each fake .uasset has what the check reads in a real one, in the order a saved UFontFace has it:
the package file tag, a name map, the AssetImportData tag's JSON, the SourceFilename value and
the inline font bytes, all as serialized FStrings or byte arrays.
"""
import contextlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_font_sources as cfs  # noqa: E402

# Font bytes with the kind of text a real font's name table holds: a URL must not count as a
# drive letter, even where the inline bytes cannot be stripped.
FONT = (b"\x00\x01\x00\x00" + b"Copyright 2011 The Montserrat Project Authors; "
        b"licensed under the SIL Open Font License, http://scripts.sil.org/OFL\x00" * 3)
OTHER_FONT = FONT.replace(b"2011", b"2017")
CLIMBING = "../../../../../../Users/alice/OneDrive/Documents/Unreal Projects/MetaHumanLocalLLM/Content/Fonts/Montserrat-Light.ttf"


def fstring(text):
    if all(ord(c) < 0x80 for c in text):
        raw = text.encode("ascii") + b"\0"
        return struct.pack("<i", len(raw)) + raw
    raw = text.encode("utf-16-le") + b"\0\0"
    return struct.pack("<i", -(len(raw) // 2)) + raw


def face_asset(source="Montserrat-Light.ttf", tag=None, font=FONT, extra=(), names=None):
    """A saved UFontFace as the check sees it. SOURCE None leaves the property out."""
    tag = source if tag is None else tag
    if names is None:
        names = ["None", "SourceFilename", "StrProperty", "/Game/Fonts/Montserrat-Light",
                 "/Script/Engine", "FontFace", "Montserrat-Light"] if source is not None else \
                ["None", "/Game/Fonts/F_Montserrat", "/Script/Engine", "Font", "F_Montserrat"]
    body = cfs.PACKAGE_TAG + b"\xf6\xff\xff\xff" + b"\x00" * 24
    body += b"".join(fstring(n) + b"\x00\x00\x00\x00" for n in names)
    for text in extra:
        body += fstring(text)
    if tag:
        info = [{"RelativeFilename": tag, "Timestamp": "-62135596800", "FileMD5": "", "DisplayLabelName": ""}]
        body += fstring("AssetImportData") + fstring(json.dumps(info).replace("{", "{ ").replace("}", " }"))
    if source is not None:
        body += b"\x00" * 8 + fstring(source)
    if font is not None:
        body += b"\x01\x00\x00\x00" + struct.pack("<i", len(font)) + font
    return body + b"\x00" * 16


class Checkout:
    """A temporary checkout with Content/Fonts/<name> files."""

    def __init__(self, files):
        self.root = tempfile.mkdtemp(prefix="check-font-sources-")
        fonts = os.path.join(self.root, "Content", "Fonts")
        os.makedirs(fonts)
        for name, data in files.items():
            with open(os.path.join(fonts, name), "wb") as handle:
                handle.write(data)

    def run(self, env=None):
        out = io.StringIO()
        old = dict(os.environ)
        os.environ.pop("GITHUB_ACTIONS", None)
        os.environ.update(env or {})
        try:
            with contextlib.redirect_stdout(out):
                code = cfs.main(["check_font_sources.py", self.root])
        finally:
            os.environ.clear()
            os.environ.update(old)
            shutil.rmtree(self.root, ignore_errors=True)
        return code, out.getvalue()


def run(assets, fonts=None):
    files = {"Montserrat-Light.ttf": FONT, "OFL-Montserrat.txt": b"SIL Open Font License\n"}
    files.update(fonts or {})
    files.update(assets)
    return Checkout(files).run()


class CheckFontSources(unittest.TestCase):
    def assertFails(self, assets, *expected, fonts=None):
        code, out = run(assets, fonts)
        self.assertEqual(code, 1, out)
        self.assertIn(": FAIL, ", out)
        for text in expected:
            self.assertIn(text, out)
        self.assertNotIn("alice", out.lower())
        return out

    def test_a_face_recording_its_file_name_passes(self):
        code, out = run({"Montserrat-Light.uasset": face_asset()})
        self.assertEqual(code, 0, out)
        self.assertIn("Content/Fonts/Montserrat-Light.uasset: OK, records Montserrat-Light.ttf", out)
        self.assertIn("OK: 1 font asset(s)", out)

    def test_the_path_f1e06c4_recorded_fails_without_printing_the_user(self):
        out = self.assertFails({"Montserrat-Light.uasset": face_asset(source=CLIMBING)},
                               "SourceFilename is a path that climbs out of Content/Fonts through a user's profile folder",
                               "the AssetImportData tag is a path that climbs out",
                               "Users/<user>/OneDrive", "user-profile folder", "parent-folder step")
        self.assertIn("FAILED: 1 of 1", out)

    def test_an_absolute_windows_path_fails(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(source="C:\\Users\\alice\\Fonts\\Montserrat-Light.ttf")},
                         "SourceFilename is an absolute path through a user's profile folder",
                         "C:\\Users\\<user>\\Fonts", "drive-letter path")

    def test_an_absolute_path_outside_a_profile_fails(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(source="D:/build/fonts/Montserrat-Light.ttf")},
                         "SourceFilename is an absolute path: D:/build/fonts/Montserrat-Light.ttf")

    def test_a_home_folder_path_fails(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(source="/home/alice/fonts/Montserrat-Light.ttf")},
                         "an absolute path through a user's profile folder: /home/<user>/fonts", "home folder")

    def test_a_unc_path_fails(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(source="\\\\buildhost\\share\\Montserrat-Light.ttf")},
                         "an absolute path: \\\\<host>\\share", "UNC share")

    def test_a_relative_folder_fails(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(source="Fonts/Montserrat-Light.ttf")},
                         "SourceFilename is a path, not a file name: Fonts/Montserrat-Light.ttf")

    def test_a_utf16_path_fails(self):
        out = self.assertFails({"Montserrat-Light.uasset": face_asset(source="C:/Users/Ålice/Montserrat-Light.ttf")},
                               "an absolute path through a user's profile folder", "C:/Users/<user>/Montserrat-Light.ttf")
        self.assertNotIn("Ålice", out)

    def test_the_tag_must_name_the_same_file(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(tag="E:/fonts/Montserrat-Light.ttf")},
                         "the AssetImportData tag is an absolute path", "the AssetImportData tag names")
        self.assertFails({"Montserrat-Light.uasset": face_asset(tag="Montserrat-Regular.ttf")},
                         "the AssetImportData tag names ['Montserrat-Regular.ttf'], not just Montserrat-Light.ttf")

    def test_the_inline_bytes_must_be_the_named_file(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(font=OTHER_FONT)},
                         "the inline font bytes are not Content/Fonts/Montserrat-Light.ttf")

    def test_the_named_file_must_be_beside_the_asset(self):
        self.assertFails({"Montserrat-Thin.uasset": face_asset(source="Montserrat-Thin.ttf")},
                         "SourceFilename Montserrat-Thin.ttf names no font file in Content/Fonts")

    def test_a_named_property_without_a_value_fails(self):
        self.assertFails({"Montserrat-Light.uasset": face_asset(source="Montserrat-Light", tag="")},
                         "SourceFilename: expected one value, found none")

    def test_a_path_left_elsewhere_fails_and_is_counted_not_printed(self):
        out = self.assertFails({"Montserrat-Light.uasset": face_asset(extra=["X:/secret-place/x"])},
                               "left in the file: 1 x drive-letter path")
        self.assertNotIn("secret-place", out)

    def test_an_asset_without_a_source_is_held_to_the_path_rule(self):
        code, out = run({"F_Montserrat.uasset": face_asset(source=None, tag="", font=None)})
        self.assertEqual(code, 0, out)
        self.assertIn("F_Montserrat.uasset: OK, records no source file", out)
        self.assertFails({"F_Montserrat.uasset": face_asset(source=None, tag="", font=None,
                                                            extra=["../../../Users/alice/x"])},
                         "user-profile folder", "parent-folder step")

    def test_an_lfs_pointer_is_never_clean(self):
        pointer = (b"version https://git-lfs.github.com/spec/v1\n"
                   b"oid sha256:" + b"0" * 64 + b"\nsize 375667\n")
        code, out = run({"Montserrat-Light.uasset": pointer})
        self.assertEqual(code, 2, out)
        self.assertIn("Montserrat-Light.uasset: NOT CHECKED, a Git LFS pointer", out)
        self.assertNotIn(": OK", out)
        code, out = run({"Montserrat-Light.uasset": pointer, "Montserrat-Regular.uasset": face_asset(source=CLIMBING)})
        self.assertEqual(code, 1, out)

    def test_nothing_to_check_is_not_a_pass(self):
        code, out = run({})
        self.assertEqual(code, 2, out)
        self.assertIn("NOT CHECKED: no Content/Fonts/*.uasset", out)

    def test_a_file_that_is_not_a_package_fails(self):
        self.assertFails({"Montserrat-Light.uasset": b"\x00" * 64}, "not an Unreal package")

    def test_github_annotations(self):
        code, out = Checkout({"Montserrat-Light.ttf": FONT,
                              "Montserrat-Light.uasset": face_asset(source=CLIMBING)}).run({"GITHUB_ACTIONS": "true"})
        self.assertEqual(code, 1, out)
        self.assertIn("::error file=Content/Fonts/Montserrat-Light.uasset::FAIL: SourceFilename is", out)
        self.assertNotIn("alice", out.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
