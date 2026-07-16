import unreal
import os

glbs = ["C:/devel/llm-metahuman/scene/glb/ibm-1401-cpu.glb"]
task = unreal.AssetImportTask()
task.set_editor_property('filename', glbs[0])
task.set_editor_property('destination_path', "/Game/Diorama")
task.set_editor_property('automated', True)
task.set_editor_property('replace_existing', True)
task.set_editor_property('save', True)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_tools.import_asset_tasks([task])

for asset in unreal.EditorAssetLibrary.list_assets("/Game/Diorama"):
    unreal.log(f"Imported asset: {asset}")
