import unreal
R="/out/NIA_MORPHS.txt"; out=[]
def w(s): out.append(str(s)); open(R,"w").write("\n".join(out))
a = unreal.load_asset("/Game/CC_ControlRig_Nia/RLContent/Nia/SK_Nia")
names = [m.get_name() for m in a.get_editor_property("morph_targets")]
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
