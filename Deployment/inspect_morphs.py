"""Inspect the resident's morph targets: do they exist, and do they carry vertex deltas?

The runtime logs "42/42 profile curves resolved", which only proves the NAMES resolve.
USkeletalMeshComponent::SetMorphTarget on a name that exists but whose MorphLODModel has zero
deltas is indistinguishable at runtime from a working write: no error, no warning, no movement.
This dumps both facts per target so the two cases can be told apart.

Run headlessly:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=Deployment/inspect_morphs.py \
      -unattended -nullrhi -nosplash -stdout -utf8output
Writes Saved/MORPH_REPORT.txt (stdout does not come back through the MCP/host boundary).
"""
import os
import unreal

OUT = os.path.join(unreal.Paths.project_saved_dir(), "MORPH_REPORT.txt")
lines = []


def w(msg):
    lines.append(str(msg))
    unreal.log("MORPHCHK: %s" % msg)


# The viseme + jaw curves the CC profile actually drives, plus the blink pair.
INTEREST = [
    "V_Open", "V_Explosive", "V_Dental_Lip", "V_Tight_O", "V_Tight", "V_Wide", "V_Affricate",
    "V_Tongue_Out", "V_Tongue_up", "V_Tongue_Lower", "V_Tongue_Raise", "V_Tongue_Curl_U",
    "Jaw_Open", "Mouth_Close", "Eye_Blink_L", "Eye_Blink_R",
]

ar = unreal.AssetRegistryHelpers.get_asset_registry()
found = []
for a in ar.get_assets_by_path("/Game", recursive=True):
    if str(a.asset_class_path.asset_name) == "SkeletalMesh":
        found.append(str(a.package_name))

w("skeletal meshes in /Game: %d" % len(found))
for p in found:
    w("  %s" % p)

for p in found:
    mesh = unreal.load_asset(p)
    if not mesh:
        w("!! could not load %s" % p)
        continue
    w("")
    w("=== %s ===" % p)
    try:
        w("  LODs: %d" % mesh.get_lod_num())
    except Exception as e:
        w("  LODs: <%s>" % e)

    # Morph target list. Exposure varies by engine version, so try the documented paths in order
    # rather than assuming one works.
    targets = None
    for accessor in ("morph_targets",):
        try:
            targets = mesh.get_editor_property(accessor)
            break
        except Exception:
            pass
    if targets is None:
        try:
            targets = mesh.morph_targets
        except Exception as e:
            w("  !! morph target list not reachable from Python: %s" % e)
            targets = []

    names = []
    for t in targets or []:
        try:
            names.append(str(t.get_name()))
        except Exception:
            names.append(str(t))
    w("  morph target COUNT: %d" % len(names))

    present = [n for n in INTEREST if n in names]
    missing = [n for n in INTEREST if n not in names]
    w("  of the %d curves the CC profile drives: %d present, %d MISSING" %
      (len(INTEREST), len(present), len(missing)))
    if missing:
        w("  MISSING: %s" % ", ".join(missing))

    # Vertex-delta counts. This is the load-bearing number: a target with 0 deltas is a name with
    # no geometry behind it, which is exactly what a silently-dead face looks like.
    for t in targets or []:
        try:
            n = str(t.get_name())
        except Exception:
            continue
        if n not in INTEREST:
            continue
        cnt = None
        for attr in ("get_morph_lod_models", "morph_lod_models"):
            try:
                mlm = getattr(t, attr)
                mlm = mlm() if callable(mlm) else mlm
                if mlm:
                    first = mlm[0]
                    for vattr in ("vertices", "get_vertices"):
                        try:
                            v = getattr(first, vattr)
                            v = v() if callable(v) else v
                            cnt = len(v)
                            break
                        except Exception:
                            pass
                break
            except Exception:
                pass
        w("    %-20s deltas@LOD0 = %s" % (n, cnt if cnt is not None else "<not reachable>"))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
unreal.log("MORPHCHK: wrote %s" % OUT)
