import unreal

bp_path = "/Game/BP_CompanionCharacter"
parent_class = unreal.load_class(None, "/Script/CICompanion.MetaHumanCompanionCharacter")

if not parent_class:
    unreal.log_error("Failed to load parent class AMetaHumanCompanionCharacter")
    exit(1)

# Delete if it already exists to ensure a clean creation
if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
    unreal.EditorAssetLibrary.delete_asset(bp_path)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", parent_class)

bp = asset_tools.create_asset("BP_CompanionCharacter", "/Game", unreal.Blueprint, factory)

if bp:
    unreal.log("BP_CompanionCharacter created successfully!")
    # Compile
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    # Save
    unreal.EditorAssetLibrary.save_asset(bp_path)
    unreal.log("BP_CompanionCharacter saved successfully!")
else:
    unreal.log_error("Failed to create BP_CompanionCharacter!")
    exit(1)
