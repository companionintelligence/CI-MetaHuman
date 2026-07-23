# Headless UE Python commandlet: build a lit "CompanionStage" level with a character the
# streamed camera can see, and save it. Run via:
#   UnrealEditor-Cmd <project> -run=pythonscript -script=Deployment/author_companion_stage.py
# Then set GameDefaultMap=/Game/Maps/CompanionStage and cook. Uses the engine's MoverTests
# mannequin (SKM_Manny) so no external asset download is needed; swap in a MetaHuman later.
import unreal

MAP = "/Game/Maps/CompanionStage"
MANNY = "/MoverTests/Characters/Mannequins/Meshes/SKM_Manny"
IDLE = "/MoverTests/Characters/Mannequins/Animations/Manny/MM_Idle"
PLANE = "/Engine/BasicShapes/Plane"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Fresh empty level, then populate it.
if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    unreal.EditorAssetLibrary.delete_asset(MAP)

les.new_level(MAP)

def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0)):
    return eas.spawn_actor_from_class(cls, unreal.Vector(*loc), unreal.Rotator(*rot))

# --- lighting / sky (so it isn't black) ---
spawn(unreal.DirectionalLight, (0, 0, 600), (-46, -30, 0))
spawn(unreal.SkyAtmosphere)
sky_light = spawn(unreal.SkyLight, (0, 0, 300))
spawn(unreal.ExponentialHeightFog)

# --- floor ---
floor = spawn(unreal.StaticMeshActor, (0, 0, 0))
plane = unreal.load_asset(PLANE)
if plane:
    floor.static_mesh_component.set_static_mesh(plane)
    floor.set_actor_scale3d(unreal.Vector(40, 40, 1))

# --- character: use the project companion where it is compatible, otherwise use Manny ---
unreal.load_asset("/Game/BP_CompanionCharacter")
bp_class = unreal.load_object(None, "/Game/BP_CompanionCharacter.BP_CompanionCharacter_C")
if bp_class:
    char = eas.spawn_actor_from_class(bp_class, unreal.Vector(400, 0, 96), unreal.Rotator(0, 180, 0))
    unreal.log("author_companion_stage: Spawned BP_CompanionCharacter")
else:
    mannequin = unreal.load_asset(MANNY)
    if mannequin:
        char = eas.spawn_actor_from_class(
            unreal.SkeletalMeshActor,
            unreal.Vector(400, 0, 0),
            unreal.Rotator(0, 180, 0),
        )
        char.skeletal_mesh_component.set_skeletal_mesh(mannequin)
        unreal.log_warning("author_companion_stage: BP_CompanionCharacter is unavailable; spawned UE mannequin fallback")
    else:
        unreal.log_error("author_companion_stage: Failed to load companion character or mannequin fallback")

# --- a PlayerStart at the streamed-camera pose, framing the character ---
spawn(unreal.PlayerStart, (-150, 0, 130), (-5, 0, 0))

les.save_current_level()
unreal.log("author_companion_stage: saved {} with mannequin".format(MAP))
