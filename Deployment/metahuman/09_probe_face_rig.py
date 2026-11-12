import os, unreal
# Lists a skeletal mesh's morph targets and how many sampled names of each face profile it carries.
# Written for Nia, which is still the default target. To check the MetaHuman resident, run it in a
# project that has been authored with MHLLM_RESIDENT=metahuman and point MHLLM_PROBE_MESH at the
# face author_diorama.py imported (under /Game/Seo/Face). If the ARKit count is 0, the face needs a
# MetaHuman-specific FFaceProfile rather than ARKit.
MESH = os.environ.get("MHLLM_PROBE_MESH", "/Game/CC_ControlRig_Nia/RLContent/Nia/SK_Nia")
R = os.environ.get("MHLLM_PROBE_OUT", "/out/NIA_MORPHS.txt"); out=[]
def w(s): out.append(str(s)); open(R,"w").write("\n".join(out))
a = unreal.load_asset(MESH)
if a is None:
    # Say what IS there, so a wrong guess at the imported asset name costs one rerun, not a search.
    w("NOT FOUND: %s" % MESH)
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for d in ar.get_assets_by_path(MESH.rsplit("/", 1)[0], recursive=True):
        if str(d.asset_class_path.asset_name) == "SkeletalMesh":
            w("  skeletal mesh here: %s" % d.package_name)
else:
    names = [m.get_name() for m in a.get_editor_property("morph_targets")]
    w("MESH %s" % MESH)
    w("TOTAL %d" % len(names))
    ARKIT = ["jawOpen","mouthClose","mouthFunnel","mouthPucker","mouthSmileLeft","mouthSmileRight",
             "eyeBlinkLeft","browInnerUp","mouthShrugUpper","tongueOut"]
    have_ark = [k for k in ARKIT if k in names]
    w("ARKit-named present: %d/%d -> %s" % (len(have_ark), len(ARKIT), have_ark))
    CCVIS = ["V_Open","V_Wide","V_Tight","V_Tight_O","V_Explosive","V_Dental_Lip","V_Affricate",
             "V_Lip_Open","V_Tongue_up","V_Tongue_Raise","V_Tongue_Out","V_Tongue_Narrow",
             "V_Tongue_Lower","V_Tongue_Curl_U","V_Tongue_Curl_D"]
    have_cc = [k for k in CCVIS if k in names]
    w("CC viseme morphs present: %d/%d -> %s" % (len(have_cc), len(CCVIS), have_cc))
    w("--- ALL MORPH TARGETS ---")
    for n in sorted(names): w(n)
    w("MORPHS_DONE")
