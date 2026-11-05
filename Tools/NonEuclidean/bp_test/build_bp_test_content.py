"""Builds the content of the Blueprint-only PortalWorlds test project (run in that project's editor).

  UnrealEditor.exe PortalWorldsBPTest.uproject -unattended -nosplash -ExecutePythonScript="<this file>"

Creates, as a Blueprint user would (no C++):
  BP_TestCharacter  Character + Camera + PortalTraversal component (Eye left empty: auto camera)
  BP_TestController PlayerController using PortalPlayerCameraManager
  BP_TestGameMode   uses the two above
  L_PortalFall      a lit level with two horizontal portals 15 m apart, linked both ways: the
                    player spawns above the lower one, falls through it, comes out of the upper
                    one, and keeps falling (traversal without any input or Blueprint graph).
"""

import traceback

import unreal

DIR = "/Game/Test"
eal = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
sbfl = unreal.SubobjectDataBlueprintFunctionLibrary


def log(msg):
    unreal.log_warning("[BPTEST] " + msg)


def make_blueprint(name, parent_class):
    path = f"{DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return asset_tools.create_asset(name, DIR, None, factory)


def add_component(bp, component_class, parent_handle, name):
    params = unreal.AddNewSubobjectParams(parent_handle=parent_handle, new_class=component_class, blueprint_context=bp)
    handle, fail = sds.add_new_subobject(params)
    if not fail.is_empty():
        raise RuntimeError(f"add {component_class}: {fail}")
    sds.rename_subobject(handle, unreal.Text(name))
    return handle, sbfl.get_object(sbfl.get_data(handle))


def cdo(bp):
    return unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(bp.get_path_name()))


def build_blueprints():
    # Character: camera at eye height, traversal component with Eye left unset.
    char = make_blueprint("BP_TestCharacter", unreal.Character)
    handles = sds.k2_gather_subobject_data_for_blueprint(char)
    capsule = next(h for h in handles if isinstance(sbfl.get_object(sbfl.get_data(h)), unreal.CapsuleComponent))
    _, camera = add_component(char, unreal.CameraComponent, capsule, "Camera")
    camera.set_editor_property("relative_location", unreal.Vector(0, 0, 64))
    camera.set_editor_property("use_pawn_control_rotation", True)
    root = handles[0]
    add_component(char, unreal.PortalTraversalComponent, root, "PortalTraversal")
    unreal.BlueprintEditorLibrary.compile_blueprint(char)
    eal.save_loaded_asset(char)

    controller = make_blueprint("BP_TestController", unreal.PlayerController)
    unreal.BlueprintEditorLibrary.compile_blueprint(controller)
    cdo(controller).set_editor_property("player_camera_manager_class", unreal.PortalPlayerCameraManager)
    eal.save_loaded_asset(controller)

    mode = make_blueprint("BP_TestGameMode", unreal.GameModeBase)
    unreal.BlueprintEditorLibrary.compile_blueprint(mode)
    mode_cdo = cdo(mode)
    mode_cdo.set_editor_property("default_pawn_class", unreal.EditorAssetLibrary.load_blueprint_class(char.get_path_name()))
    mode_cdo.set_editor_property("player_controller_class", unreal.EditorAssetLibrary.load_blueprint_class(controller.get_path_name()))
    eal.save_loaded_asset(mode)
    log("blueprints created")
    return mode


def build_level(mode):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    path = f"{DIR}/L_PortalFall"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    les.new_level(path)

    cube = eal.load_asset("/Engine/BasicShapes/Cube")
    for x in (-500, 500):  # ground on either side, nothing under the portal
        ground = actors.spawn_actor_from_object(cube, unreal.Vector(x, 0, -50))
        ground.set_actor_scale3d(unreal.Vector(6, 10, 1))
    pillar = actors.spawn_actor_from_object(cube, unreal.Vector(0, 400, 200))  # something to see
    pillar.set_actor_scale3d(unreal.Vector(1, 1, 4))

    # Two horizontal portals (rolled -90 so the surface lies flat), 15 m apart, linked both ways.
    lower = actors.spawn_actor_from_class(unreal.Portal, unreal.Vector(0, 0, 0), unreal.Rotator(-90, 0, 0))
    upper = actors.spawn_actor_from_class(unreal.Portal, unreal.Vector(0, 0, 1500), unreal.Rotator(-90, 0, 0))
    lower.set_actor_label("PortalLower")
    upper.set_actor_label("PortalUpper")
    unreal.Portal.connect(lower, upper)

    actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000), unreal.Rotator(0, -50, 30))
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 3000))
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 400), unreal.Rotator(0, -60, 90))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(mode.get_path_name()))
    les.save_current_level()
    log("level L_PortalFall saved")


try:
    build_level(build_blueprints())
    log("OK")
except Exception:
    unreal.log_error("[BPTEST] failed:\n" + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
