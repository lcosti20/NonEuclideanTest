"""Adds the mirror tunnel to the Blueprint-only PortalWorlds test project (run in that project's editor).

  UnrealEditor.exe PortalWorldsBPTest.uproject -unattended -nosplash -ExecutePythonScript="<this file>"

Run after build_bp_test_content.py (it uses BP_TestController). Creates, without C++:
  BP_MirrorFlyer      DefaultPawn (flying; WASD/mouse, E/Q up/down) + PortalTraversal component
  BP_MirrorGameMode   BP_MirrorFlyer + BP_TestController
  MI_Shape_<colour>   coloured instances of the engine's basic shape material
  L_MirrorTunnel      two identical copies of a scene (spaces 0 and 1, 200 m apart), each with a
                      tunnel along X and a portal across its middle. The copy's portal has X scale
                      -1.05 instead of 1.05, so walking through mirrors the world. The scene uses
                      the project's default renderer (movable sun and sky light, atmosphere, fog);
                      the sun shines along the tunnel, so the lighting is symmetric about the
                      mirror plane.
"""

import traceback

import unreal

DIR = "/Game/Test"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
SPACE_OFFSET = 20000.0  # copy B is 200 m along +Y

COLOURS = {"Red": (0.8, 0.05, 0.05), "Blue": (0.05, 0.15, 0.8), "Yellow": (0.9, 0.7, 0.05), "Green": (0.1, 0.6, 0.1)}


def log(msg):
    unreal.log_warning("[BPTEST] " + msg)


def make_blueprint(name, parent_class):
    """Creates the Blueprint, or reuses it on a rebuild (it's referenced, so it can't be deleted)."""
    path = f"{DIR}/{name}"
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return asset_tools.create_asset(name, DIR, None, factory)


def bp_class(bp):
    return eal.load_blueprint_class(bp.get_path_name())


def build_blueprints():
    flyer = make_blueprint("BP_MirrorFlyer", unreal.DefaultPawn)
    handles = sds.k2_gather_subobject_data_for_blueprint(flyer)
    has_traversal = any(isinstance(unreal.SubobjectDataBlueprintFunctionLibrary.get_object(
        unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)), unreal.PortalTraversalComponent) for h in handles)
    params = None if has_traversal else unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.PortalTraversalComponent, blueprint_context=flyer)
    if params:
        handle, fail = sds.add_new_subobject(params)
        if not fail.is_empty():
            raise RuntimeError(f"add PortalTraversal: {fail}")
        sds.rename_subobject(handle, unreal.Text("PortalTraversal"))
    unreal.BlueprintEditorLibrary.compile_blueprint(flyer)
    eal.save_loaded_asset(flyer)

    mode = make_blueprint("BP_MirrorGameMode", unreal.GameModeBase)
    unreal.BlueprintEditorLibrary.compile_blueprint(mode)
    cdo = unreal.get_default_object(bp_class(mode))
    cdo.set_editor_property("default_pawn_class", bp_class(flyer))
    cdo.set_editor_property("player_controller_class", eal.load_blueprint_class(f"{DIR}/BP_TestController"))
    eal.save_loaded_asset(mode)
    log("BP_MirrorFlyer and BP_MirrorGameMode created")
    return mode


def build_materials():
    parent = eal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
    log(f"BasicShapeMaterial vector parameters: {mel.get_vector_parameter_names(parent)}")
    mats = {}
    for name, (r, g, b) in COLOURS.items():
        path = f"{DIR}/MI_Shape_{name}"
        mi = eal.load_asset(path) if eal.does_asset_exist(path) else asset_tools.create_asset(
            f"MI_Shape_{name}", DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, parent)
        mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(r, g, b, 1))
        eal.save_loaded_asset(mi)
        mats[name] = mi
    return mats


def build_level(mode, mats):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    path = f"{DIR}/L_MirrorTunnel"
    if eal.does_asset_exist(path):  # rebuild: load it and clear it
        if not les.load_level(path):
            raise RuntimeError(f"could not load {path}")
        actors.destroy_actors([a for a in actors.get_all_level_actors()
                               if not isinstance(a, (unreal.WorldSettings, unreal.Brush))])
    elif not les.new_level(path):
        raise RuntimeError(f"could not create {path}")

    shapes = {n: eal.load_asset(f"/Engine/BasicShapes/{n}") for n in ("Cube", "Cylinder", "Sphere", "Cone")}

    def shape(kind, label, loc, scale, space, material=None):
        a = actors.spawn_actor_from_object(shapes[kind], unreal.Vector(*loc))
        a.set_actor_label(label)
        a.set_actor_scale3d(unreal.Vector(*scale))
        a.set_folder_path(f"Space{space}")
        if material:
            a.static_mesh_component.set_material(0, material)
        a.set_editor_property("tags", [unreal.Name(f"PortalSpace.{space}")])
        return a

    portals = []
    for space in (0, 1):
        y = SPACE_OFFSET * space
        s = f"_{space}"
        shape("Cube", "Ground" + s, (0, y, -50), (150, 150, 1), space)  # 150 m, under the horizon
        # Tunnel along X: interior 2 m wide (|y| < 100), 3 m high, 8 m long.
        shape("Cube", "WallLeft" + s, (0, y - 125, 150), (8, 0.5, 3), space)
        shape("Cube", "WallRight" + s, (0, y + 125, 150), (8, 0.5, 3), space)
        shape("Cube", "Roof" + s, (0, y, 325), (8, 3, 0.5), space)
        # Asymmetric landmarks: walking in from +X, the red column is on the left beyond the tunnel.
        shape("Cylinder", "RedColumn" + s, (-900, y - 450, 150), (1, 1, 3), space, mats["Red"])
        shape("Sphere", "BlueBall" + s, (-1100, y + 500, 100), (2, 2, 2), space, mats["Blue"])
        shape("Cone", "YellowCone" + s, (-1500, y - 50, 100), (2, 2, 2), space, mats["Yellow"])
        shape("Cube", "GreenBlock" + s, (900, y + 450, 100), (1, 1, 2), space, mats["Green"])
        # Portal across the middle: width along Y (tucked 5 cm into the walls), height 0..3 m.
        p = actors.spawn_actor_from_class(unreal.Portal, unreal.Vector(0, y, 150), unreal.Rotator(0, 0, 90))
        p.set_actor_label(f"Portal{space + 1}")
        p.set_actor_scale3d(unreal.Vector(1.05 if space == 0 else -1.05, 1, 1.55))
        p.set_folder_path(f"Space{space}")
        p.set_editor_property("tags", [unreal.Name(f"PortalSpace.{space}")])
        portals.append(p)
    unreal.Portal.connect(*portals)

    # Sun along the tunnel (yaw 0): its direction lies in the mirror plane (XZ).
    # Movable lights: stationary ones show "Preview" shadows until lighting is built.
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000), unreal.Rotator(0, -40, 0))
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 3000))
    for light in (sun, sky):
        light.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(1000, 0, 150), unreal.Rotator(0, 0, 180))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", bp_class(mode))
    les.save_current_level()
    log(f"level L_MirrorTunnel saved; mirrored link: {portals[0].is_mirrored_link_to(portals[1])}")


try:
    build_level(build_blueprints(), build_materials())
    log("OK")
except Exception:
    unreal.log_error("[BPTEST] failed:\n" + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
