"""Fail when a font asset in Content/Fonts records a machine path as the file it came from.

    python -I Deployment/check_font_sources.py [<checkout>]      (default: the current folder)

An imported UFontFace records its source font file twice: in its SourceFilename property, and
again in the package's asset-registry block as the hidden "AssetImportData" tag
("RelativeFilename"), which UFontFace::GetAssetRegistryTags builds from SourceFilename.
SourceFilename is not editor-only data, so a cook ships it with the face. An editor import
records whatever path it was given: f1e06c4's import recorded one that climbed from the editor's
working folder to the drive root and through the importing user's profile folder.
Deployment/import_brand_font.py now records "<Face>.ttf", the form UAssetImportData stores for a
source file beside the package.

Every Content/Fonts/*.uasset is read without the engine. A face passes when
  - SourceFilename is a bare file name (no drive, folder or parent step) of a font file beside it
    in Content/Fonts, and the AssetImportData tag names exactly that file;
  - its inline font bytes are that file, byte for byte (an uncooked face keeps the font inline,
    and the editor and -game render from those bytes, not from SourceFilename);
  - nothing path-like is left anywhere else in it, as Latin-1 or as UTF-16 text: a drive letter,
    a UNC share, a Users, home or OneDrive folder, a parent-folder step.
An asset that records no SourceFilename (a composite UFont, say) is held to the last rule only.
Recorded values are printed with user, host and organisation folder names masked; anything
path-like elsewhere is counted, never printed.

Git LFS: these .uasset files are LFS objects, and a checkout that skips LFS (GitHub Actions'
checkout does by default) has pointer files in their place. A pointer is reported as NOT CHECKED
and never passes; fetch the objects first: git lfs pull --include="Content/Fonts/*.uasset".

Exit 0: every asset was checked and is clean. 1: an asset records a path, or is not what it
should be. 2: nothing failed, but something was not checked (an LFS pointer, or no asset at all).
"""
import json
import os
import re
import struct
import sys

FONTS_DIR = os.path.join("Content", "Fonts")
FONT_SUFFIXES = (".ttf", ".otf", ".ttc", ".otc")
PACKAGE_TAG = struct.pack("<I", 0x9E2A83C1)  # PACKAGE_FILE_TAG, first in every .uasset
LFS_POINTER = b"version https://git-lfs"
LONGEST_STRING = 4096  # characters; longer runs are not taken for serialized FStrings

OK, FAIL, NOT_CHECKED = "OK", "FAIL", "NOT CHECKED"

# Anywhere in an asset outside its inline font bytes; UTF-16 strings are checked as ASCII with
# every other character turned into "?". Matches are counted, never printed, so the output cannot
# carry a user name. Each needs a printable tail, so that a stray byte pair in a GUID or a hash
# cannot pass for a path, and a drive letter must not follow a letter or digit ("http://").
LEAKS = {
    "drive-letter path": re.compile(rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/][\x20-\x7e]{3}"),
    "UNC share": re.compile(rb"\\\\[A-Za-z0-9][\x20-\x5b\x5d-\x7e]*\\[\x20-\x7e]"),
    "user-profile folder": re.compile(rb"(?i)(?:^|[^A-Za-z0-9])users[\\/][\x20-\x7e]{2}"),
    "home folder": re.compile(rb"/home/[\x20-\x7e]{2}"),
    "OneDrive folder": re.compile(rb"(?i)onedrive"),
    "parent-folder step": re.compile(rb"\.\.[\\/][\x20-\x7e]"),
}

# For printing a recorded value: the folder after Users/home, a OneDrive organisation and a UNC
# host are replaced.
MASKS = (
    (re.compile(r"(?i)((?:^|[\\/])(?:users|home|documents and settings)[\\/])[^\\/]+"), r"\1<user>"),
    (re.compile(r"(?i)(onedrive)\s*-\s*[^\\/]+"), r"\1 - <org>"),
    (re.compile(r"^([\\/]{2})[^\\/]+"), r"\1<host>"),
)


def mask(value):
    for pattern, replacement in MASKS:
        value = pattern.sub(replacement, value)
    return value


def fstrings(data):
    """Every serialized FString in DATA: an int32 length, then that many Latin-1 characters (or,
    for a negative length, UTF-16 code units), the last one NUL. Yields (text, is_utf16)."""
    end = len(data)
    for i in range(end - 5):
        (n,) = struct.unpack_from("<i", data, i)
        if 2 <= n <= LONGEST_STRING:
            stop = i + 4 + n
            if stop <= end and data[stop - 1] == 0:
                raw = data[i + 4:stop - 1]
                if all(0x20 <= b < 0x7F for b in raw):
                    yield raw.decode("ascii"), False
        elif -LONGEST_STRING <= n <= -2:
            stop = i + 4 - 2 * n
            if stop <= end and data[stop - 2:stop] == b"\0\0":
                try:
                    text = data[i + 4:stop - 2].decode("utf-16-le")
                except UnicodeDecodeError:
                    continue
                if text.isprintable():
                    yield text, True


def import_tag_files(texts):
    """The RelativeFilename of each source file named in an AssetImportData tag (FAssetImportInfo
    JSON: [{ "RelativeFilename" : ..., "Timestamp" : ..., ... }])."""
    files = []
    for text in texts:
        if not (text.startswith("[") and "RelativeFilename" in text):
            continue
        try:
            entries = json.loads(text)
            files.extend(str(entry.get("RelativeFilename", "")) for entry in entries)
        except (ValueError, AttributeError, TypeError):
            for escaped in re.findall(r'"RelativeFilename"\s*:\s*"((?:[^"\\]|\\.)*)"', text):
                try:
                    files.append(json.loads('"%s"' % escaped))
                except ValueError:
                    files.append(escaped)
    return files


def path_problem(value):
    """None when VALUE is a bare file name; otherwise what kind of path it is."""
    path = value.replace("\\", "/")
    if re.match(r"[A-Za-z]:", path) or path.startswith("/"):
        kind = "an absolute path"
    elif re.search(r"(^|/)\.\.(/|$)", path):
        kind = "a path that climbs out of Content/Fonts"
    elif "/" in path or path in ("", ".", ".."):
        kind = "a path, not a file name"
    else:
        return None
    if re.search(r"(?i)(^|/)(users|home|documents and settings)/", path) or "onedrive" in path.lower():
        kind += " through a user's profile folder"
    return kind


def leaks(outside, utf16_texts):
    blob = outside + b"\n" + b"\n".join(t.encode("ascii", "replace") for t in utf16_texts)
    counts = {name: len(pattern.findall(blob)) for name, pattern in LEAKS.items()}
    return {name: n for name, n in counts.items() if n}


def check_asset(path, fonts):
    """(status, message) for one .uasset; FONTS maps each font file beside it to its bytes."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data.startswith(LFS_POINTER):
        return NOT_CHECKED, ('a Git LFS pointer, not the asset; fetch it with '
                             'git lfs pull --include="Content/Fonts/*.uasset"')
    if not data.startswith(PACKAGE_TAG):
        return FAIL, "not an Unreal package (no package file tag)"

    inline = [name for name, font in sorted(fonts.items()) if font and font in data]
    outside = data
    for name in inline:
        outside = outside.replace(fonts[name], b"")
    strings = list(fstrings(outside))
    texts = [text for text, _ in strings]
    sources = [t for t in texts if t.lower().endswith(FONT_SUFFIXES) and not t.startswith("[")]
    tagged = import_tag_files(texts)
    named = "SourceFilename" in texts

    reasons = []
    for label, values in (("SourceFilename", sources), ("the AssetImportData tag", tagged)):
        for value in values:
            problem = path_problem(value)
            if problem:
                reasons.append("%s is %s: %s" % (label, problem, mask(value)))
    source = None
    if named or sources or tagged:
        if len(sources) != 1:
            reasons.append("SourceFilename: expected one value, found %s"
                           % ([mask(v) for v in sources] or "none"))
        elif not path_problem(sources[0]):
            source = sources[0]
            if source not in fonts:
                reasons.append("SourceFilename %s names no font file in Content/Fonts" % source)
            elif source not in inline:
                reasons.append("the inline font bytes are not Content/Fonts/%s" % source)
        if source is not None and tagged != [source]:
            reasons.append("the AssetImportData tag names %s, not just %s"
                           % ([mask(v) for v in tagged] or "nothing", source))
    left = leaks(outside, [text for text, utf16 in strings if utf16])
    if left:
        reasons.append("left in the file: " + ", ".join("%d x %s" % (n, name) for name, n in left.items()))

    if reasons:
        return FAIL, "; ".join(dict.fromkeys(reasons))
    if source is None:
        return OK, "records no source file, and nothing path-like is left in it"
    return OK, ("records %s (SourceFilename and the AssetImportData tag); inline bytes = "
                "Content/Fonts/%s (%s bytes); nothing path-like is left in it"
                % (source, source, format(len(fonts[source]), ",")))


def check(checkout):
    """[(path relative to CHECKOUT, status, message)] for every Content/Fonts/*.uasset."""
    folder = os.path.join(checkout, FONTS_DIR)
    names = sorted(os.listdir(folder)) if os.path.isdir(folder) else []
    fonts = {}
    for name in names:
        if name.lower().endswith(FONT_SUFFIXES) and os.path.isfile(os.path.join(folder, name)):
            with open(os.path.join(folder, name), "rb") as handle:
                fonts[name] = handle.read()
    results = []
    for name in names:
        if name.lower().endswith(".uasset"):
            status, message = check_asset(os.path.join(folder, name), fonts)
            results.append(("Content/Fonts/" + name, status, message))
    return results


def annotate(path, message):
    """A GitHub Actions error annotation on PATH."""
    for char, code in (("%", "%25"), ("\r", "%0D"), ("\n", "%0A")):
        message = message.replace(char, code)
    print("::error file=%s::%s" % (path, message))


def main(argv):
    if len(argv) > 2 or (len(argv) == 2 and argv[1].startswith("-")):
        print(__doc__, file=sys.stderr)
        return 2
    checkout = argv[1] if len(argv) == 2 else "."
    results = check(checkout)
    github = os.environ.get("GITHUB_ACTIONS") == "true"
    for path, status, message in results:
        print("%s: %s, %s" % (path, status, message))
        if github and status != OK:
            annotate(path, "%s: %s" % (status, message))
    failed = [path for path, status, _ in results if status == FAIL]
    unchecked = [path for path, status, _ in results if status == NOT_CHECKED]
    if failed:
        print("FAILED: %d of %d font asset(s) record a path or are not what they should be"
              % (len(failed), len(results)))
        return 1
    if not results:
        print("NOT CHECKED: no %s/*.uasset under %s" % (FONTS_DIR.replace(os.sep, "/"), checkout))
        return 2
    if unchecked:
        print("NOT CHECKED: %d of %d font asset(s) are Git LFS pointers; nothing about them passed"
              % (len(unchecked), len(results)))
        return 2
    print("OK: %d font asset(s), none records a machine path" % len(results))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
