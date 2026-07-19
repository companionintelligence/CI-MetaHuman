import unreal
MEL=unreal.MaterialEditingLibrary
R="/out/MATDIAG.txt"; out=[]
def w(s): out.append(str(s)); unreal.log("MD: "+str(s)); open(R,"w").write("\n".join(out))
skin=unreal.load_asset("/Game/Seo/Mat/M_SeoSkin")
mic=unreal.load_asset("/Game/Seo/Mat/MI_SeoBody")
htex=unreal.load_asset("/Game/Seo/Tex/T_Head_BC")
btex=unreal.load_asset("/Game/Seo/Tex/T_Body_BC")
w("assets: M_SeoSkin=%s MI_SeoBody=%s T_Head_BC=%s T_Body_BC=%s"%(skin is not None,mic is not None,htex is not None,btex is not None))
if htex:
    try: w("T_Head_BC size=%dx%d srgb=%s comp=%s"%(htex.blueprint_get_size_x(),htex.blueprint_get_size_y(),htex.get_editor_property("srgb"),htex.get_editor_property("compression_settings")))
    except Exception as e: w("tex prop err %s"%e)
# master texture-parameter names
try: w("M_SeoSkin texture params: %s"%list(MEL.get_texture_parameter_names(skin)))
except Exception as e: w("tex param names err: %s"%e)
try: w("M_SeoSkin scalar params: %s"%list(MEL.get_scalar_parameter_names(skin)))
except Exception as e: w("scalar names err: %s"%e)
# default texture on master param
try: w("master default 'BaseColor' tex: %s"%MEL.get_material_default_texture_parameter_value(skin,"BaseColor"))
except Exception as e: w("master default err: %s"%e)
# MIC override
try:
    v=MEL.get_material_instance_texture_parameter_value(mic,"BaseColor")
    w("MI_SeoBody 'BaseColor' override: %s"%(v.get_name() if v else "NONE"))
except Exception as e: w("mic override err: %s"%e)
w("DONE")
