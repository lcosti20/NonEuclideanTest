"""Builds the non-Euclidean test content: materials, mesh set-up and maps.

Run in the full editor after ue_phase0_import.py (spawning actors doesn't work in the commandlet):
  UnrealEditor.exe <project>.uproject -unattended -nosplash
      -ExecutePythonScript="<dir>/run_and_quit.py ue_build_content.py [stage ...]"

Stages: materials, meshes, maps (default: all three); "mirror" builds only L_NE_Mirror; "puzzle"
builds only L_NE_ObjectPuzzles with its materials and meshes (run make_puzzle_meshes.py first). Portals come from the PortalWorlds plugin
(/Script/PortalWorlds.Portal, /PortalWorlds/... assets).

Coordinate conversion from the original engine (metres, Y-up, right-handed), confirmed
with an imported calibration mesh:   UE (X, Y, Z) = original (X, Z, Y) * 100
An object yaw of theta (radians, about +Y) becomes UE yaw -theta (degrees); a camera looking
along the original's -Z at yaw theta becomes UE yaw -90 - theta.
"""

import math
import sys

import unreal

ROOT = "/Game/NonEuclidean"
MESH_DIR = f"{ROOT}/Meshes"
MAT_DIR = f"{ROOT}/Materials"
TEX_DIR = f"{ROOT}/Textures"
MAP_DIR = f"{ROOT}/Maps"

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Original LIGHT (0.36, 0.80, 0.48) in UE axes.
LIGHT_UE = "float3(0.36, 0.48, 0.80)"

SRGB_DECODE = "return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));"


def log(msg):
    unreal.log_warning("[NE] " + msg)


def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


def new_material(name, two_sided=False):
    path = f"{MAT_DIR}/{name}"
    fresh(path)
    mat = asset_tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", two_sided)
    return mat


def custom_node(mat, code, inputs, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for name in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    node.set_editor_property("inputs", ins)
    return node


def finish(mat):
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log(f"material {mat.get_path_name()}")


# --------------------------------------------------------------------------- materials
def build_textured():
    """Port of texture.frag: tex * (dot(N, LIGHT) * 0.5 + 0.5), computed in display space."""
    mat = new_material("M_NE_Textured")
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    tex.set_editor_property("parameter_name", "Tex")
    tex.set_editor_property("texture", eal.load_asset(f"{TEX_DIR}/T_CheckerGray"))
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    nrm = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -700, 300)
    code = (
        f"float s = dot(normalize(N), {LIGHT_UE}) * 0.5 + 0.5;\n"
        "float3 c = saturate(Tex * s);\n" + SRGB_DECODE
    )
    node = custom_node(mat, code, ["Tex", "N"], -300, 0)
    mel.connect_material_expressions(tex, "RGB", node, "Tex")
    mel.connect_material_expressions(nrm, "", node, "N")
    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)
    return mat


def build_sky():
    """Port of sky.frag: gradient from the world-space view ray, plus the sun disc."""
    mat = new_material("M_NE_Sky", two_sided=True)
    cam = mel.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -700, 0)
    code = (
        "float3 n = -normalize(V);\n"  # CameraVectorWS points from the pixel to the camera
        "float h = (1.0 - n.z) * (1.0 - n.z) * 0.5;\n"
        "float3 sky = float3(0.2 + h, 0.5 + h, 1.0);\n"
        f"float s = dot(n, {LIGHT_UE}) - 1.0 + 0.002;\n"
        "float sun = min(exp(s * 1.0 / 0.002), 1.0);\n"
        "float3 c = saturate(max(sky, sun));\n" + SRGB_DECODE
    )
    node = custom_node(mat, code, ["V"], -300, 0)
    mel.connect_material_expressions(cam, "", node, "V")
    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)
    return mat


def make_instance(name, parent, texture_name):
    path = f"{MAT_DIR}/{name}"
    fresh(path)
    mi = asset_tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "Tex", eal.load_asset(f"{TEX_DIR}/{texture_name}"))
    eal.save_loaded_asset(mi)
    return mi


# --------------------------------------------------------------------------- meshes
# Mesh -> material, as assigned by the original's Object subclasses.
MESH_MATERIALS = {
    "SM_Tunnel": "MI_NE_CheckerGray",
    "SM_TunnelScale": "MI_NE_CheckerGray",
    "SM_TunnelSlope": "MI_NE_CheckerGray",
    "SM_Ground": "MI_NE_CheckerGreen",
    "SM_GroundSlope": "MI_NE_CheckerGreen",
    "SM_SquareRooms": "MI_NE_ThreeRoom",
    "SM_PillarRoom": "MI_NE_ThreeRoom",
    "SM_Pillar": "MI_NE_White",
    "SM_Bunny": "MI_NE_Gold",
    "SM_Suzanne": "MI_NE_Gold",
    "SM_Teapot": "MI_NE_Gold",
}
# Calibration done; the sky uses a sphere; the portal quad comes from the PortalWorlds plugin.
DISCARD = ["SM_AxisCalibration", "SM_Quad", "SM_DoubleQuad"]


def configure_mesh(mesh, material):
    """Original material; no Nanite (tiny meshes); render triangles used as double-sided
    collision, like the original's sphere-vs-triangle collision."""
    mesh.set_material(0, material)
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.set_editor_property("enabled", False)
    mesh.set_editor_property("nanite_settings", nanite)
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property("double_sided_geometry", True)
    eal.save_loaded_asset(mesh)


def organise_meshes():
    """Move Interchange's /Meshes/SM_X/StaticMeshes/SM_X to /Meshes/SM_X and configure it.
    A freshly re-imported (nested) mesh replaces an existing flat one."""
    for name in DISCARD:
        if eal.does_directory_exist(f"{MESH_DIR}/{name}"):
            eal.delete_directory(f"{MESH_DIR}/{name}")
    for name, mat_name in MESH_MATERIALS.items():
        flat = f"{MESH_DIR}/{name}"
        nested = f"{MESH_DIR}/{name}/StaticMeshes/{name}"
        material = eal.load_asset(f"{MAT_DIR}/{mat_name}")
        if eal.does_asset_exist(nested):
            configure_mesh(eal.load_asset(nested), material)  # drop the reference to interchange's MI first
            if eal.does_asset_exist(flat):
                eal.delete_asset(flat)
            eal.rename_asset(nested, flat)
        if not eal.does_asset_exist(flat):
            log(f"MISSING mesh {name}")
            continue
        configure_mesh(eal.load_asset(flat), material)
        if eal.does_directory_exist(f"{MESH_DIR}/{name}"):
            eal.delete_directory(f"{MESH_DIR}/{name}")  # interchange's MI_* and leftover redirectors
        log(f"mesh {flat} -> {mat_name}")


# --------------------------------------------------------------------------- level helpers
def to_ue(p):
    return unreal.Vector(p[0] * 100.0, p[2] * 100.0, p[1] * 100.0)


def scale_to_ue(s):
    return unreal.Vector(s[0], s[2], s[1])


def rot_y(theta, v):
    """Original Matrix4::RotY applied to v."""
    c, s = math.cos(theta), math.sin(theta)
    return (c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2])


def local_to_world(pos, theta, scale, local):
    """Original Object::LocalToWorld().MulPoint(local) for yaw-only objects."""
    scaled = (local[0] * scale[0], local[1] * scale[1], local[2] * scale[2])
    r = rot_y(theta, scaled)
    return (pos[0] + r[0], pos[1] + r[1], pos[2] + r[2])


actor_sub = None


def spawn_mesh(mesh_name, label, pos, theta=0.0, scale=(1, 1, 1), folder="Geometry", collide=True):
    mesh = eal.load_asset(f"{MESH_DIR}/{mesh_name}")
    actor = actor_sub.spawn_actor_from_object(mesh, to_ue(pos), unreal.Rotator(0, 0, -math.degrees(theta)))
    actor.set_actor_label(label)
    actor.set_actor_scale3d(scale_to_ue(scale))
    actor.set_folder_path(folder)
    actor.static_mesh_component.set_editor_property("cast_shadow", False)
    if not collide:
        actor.static_mesh_component.set_collision_profile_name("NoCollision")
    return actor


def spawn_sky():
    sphere = eal.load_asset("/Engine/BasicShapes/Sphere")
    sky = actor_sub.spawn_actor_from_object(sphere, unreal.Vector(0, 0, 0))
    sky.set_actor_label("Sky")
    sky.set_actor_scale3d(unreal.Vector(4000, 4000, 4000))  # 2 km radius
    sky.set_folder_path("Environment")
    comp = sky.static_mesh_component
    comp.set_material(0, eal.load_asset(f"{MAT_DIR}/M_NE_Sky"))
    comp.set_editor_property("cast_shadow", False)
    comp.set_collision_profile_name("NoCollision")  # a solid 2 km sphere would push the player out
    return sky


def spawn_player_start(eye_pos, cam_theta=0.0):
    feet = (eye_pos[0], eye_pos[1] - 1.5, eye_pos[2])
    loc = to_ue(feet) + unreal.Vector(0, 0, 90)  # capsule centre (half height 85) plus a little clearance
    ps = actor_sub.spawn_actor_from_class(unreal.PlayerStart, loc, unreal.Rotator(0, 0, -90 - math.degrees(cam_theta)))
    ps.set_folder_path("Environment")
    return ps


def new_map(name):
    """Opens an empty map at MAP_DIR/name: an existing map is loaded and cleared (replacing the
    asset fails while it's registered), a missing one is created. Raises unless the open level is
    that map, so nothing is ever spawned into another level."""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    path = f"{MAP_DIR}/{name}"
    if eal.does_asset_exist(path):
        if not les.load_level(path):
            raise RuntimeError(f"could not load {path}")
        keep = (unreal.WorldSettings, unreal.Brush)
        doomed = [a for a in actor_sub.get_all_level_actors() if not isinstance(a, keep)]
        actor_sub.destroy_actors(doomed)
    elif not les.new_level(path):
        raise RuntimeError(f"could not create {path}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_outermost().get_name() != path:
        raise RuntimeError(f"expected {path} to be open, found {world.get_outermost().get_name()}")
    ws = world.get_world_settings()
    ws.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/NonEuclideanTest.NEGameMode"))
    return les


# --------------------------------------------------------------------------- portals
PORTAL_CLASS = "/Script/PortalWorlds.Portal"  # from the PortalWorlds plugin


def spawn_portal(label, loc, yaw, scale, folder="Portals"):
    """APortal at an Unreal-space location/yaw/scale."""
    actor = actor_sub.spawn_actor_from_class(unreal.load_class(None, PORTAL_CLASS), loc, unreal.Rotator(0, 0, yaw))
    actor.set_actor_label(label)
    actor.set_actor_scale3d(scale)
    actor.set_folder_path(folder)
    return actor


def spawn_portal_orig(label, pos, theta, scale):
    """APortal placed with the original engine's coordinates."""
    return spawn_portal(label, to_ue(pos), -math.degrees(theta), scale_to_ue(scale))


def link(portal, front, back):
    portal.set_editor_property("front_link", front)
    portal.set_editor_property("back_link", back)


def connect(a, b):
    """The original's Portal::Connect(a, b): both sides of a lead to b, and both sides of b to a."""
    link(a, b, b)
    link(b, a, a)


# --------------------------------------------------------------------------- Level1
def tunnel_door(tunnel_pos, tunnel_theta, tunnel_scale, door):
    """Tunnel::SetDoor1/SetDoor2 for the NORMAL tunnel type."""
    local = (0, 1, 1) if door == 1 else (0, 1, -1)
    pos = local_to_world(tunnel_pos, tunnel_theta, tunnel_scale, local)
    return pos, tunnel_theta, (0.6 * tunnel_scale[0], 0.999 * tunnel_scale[0], 1 * tunnel_scale[0])


def build_level1():
    """Original Level1.cpp: a long tunnel that is short inside, and a short one that is long."""
    les = new_map("L_NE_Tunnels")
    t1 = ((-2.4, 0, -1.8), 0.0, (1, 1, 4.8))
    t2 = ((2.4, 0, 0), 0.0, (1, 1, 0.6))
    spawn_mesh("SM_Tunnel", "Tunnel1_Long", *t1)
    spawn_mesh("SM_Tunnel", "Tunnel2_Short", *t2)
    spawn_mesh("SM_Ground", "Ground", (0, 0, 0), 0.0, (12, 1.2, 12))
    p = {}
    for label, t, door in (("Portal1", t1, 1), ("Portal2", t2, 1), ("Portal3", t1, 2), ("Portal4", t2, 2)):
        p[label] = spawn_portal_orig(label, *tunnel_door(*t, door))
    connect(p["Portal1"], p["Portal2"])  # Level1.cpp: Portal::Connect(portal1, portal2)
    connect(p["Portal3"], p["Portal4"])  #             Portal::Connect(portal3, portal4)
    spawn_sky()
    spawn_player_start((0, 1.5, 5))
    les.save_current_level()
    log("map L_NE_Tunnels saved")


# --------------------------------------------------------------------------- Level2 (houses)
def tag_space(actor, space):
    """Puts an actor in a portal space (see UPortalSubsystem)."""
    actor.set_editor_property("tags", [unreal.Name(f"PortalSpace.{space}")])
    return actor


def house_door(house_pos, house_theta, house_scale, door):
    """House::SetDoor1..4: door position (house-local), extra yaw, portal scale."""
    local, extra = {1: ((4, 0.5, 10), 0.0), 2: ((10, 0.5, 4), -math.pi / 2),
                    3: ((16, 0.5, 10), -math.pi), 4: ((10, 0.5, 16), -math.pi * 3 / 2)}[door]
    pos = local_to_world(house_pos, house_theta, house_scale, local)
    scale = (2 * house_scale[0], 0.5 * house_scale[1], 1 * house_scale[2])
    return pos, house_theta + extra, scale


def build_level2(num_rooms, map_name):
    """Original Level2.cpp. 3 rooms: a four-room house that only has three (red, green, blue).
    6 rooms: a second house (three_room2) joined by a ring of three portals."""
    les = new_map(map_name)
    houses = [((0, 0, -20), 0.0, (1, 3, 1))]
    if num_rooms > 4:
        houses.append(((200, 0, -20), 0.0, (1, 3, 1)))
    for i, h in enumerate(houses):
        actor = tag_space(spawn_mesh("SM_SquareRooms", f"House{i + 1}", *h), i)
        if i == 1:
            actor.static_mesh_component.set_material(0, eal.load_asset(f"{MAT_DIR}/MI_NE_ThreeRoom2"))

    def portal(label, house_index, door):
        return tag_space(spawn_portal_orig(label, *house_door(*houses[house_index], door)), house_index)

    if num_rooms == 3:
        connect(portal("Portal1", 0, 3), portal("Portal2", 0, 4))
    elif num_rooms == 6:
        p1, p2, p3 = portal("Portal1", 0, 4), portal("Portal2", 1, 3), portal("Portal3", 1, 1)
        # Portal::Connect(p1->front, p2->back), (p2->front, p3->back), (p3->front, p1->back)
        link(p1, p2, p3)
        link(p2, p3, p1)
        link(p3, p1, p2)
    else:
        raise ValueError(f"num_rooms {num_rooms} not used in the video")
    spawn_sky()
    spawn_player_start((3, 1.5, 3))
    les.save_current_level()
    log(f"map {map_name} saved")


# --------------------------------------------------------------------------- Level3 (pillar)
def build_level3():
    """Original Level3.cpp: walking around the pillar moves you through a ring of three rooms
    (teapot, bunny, Suzanne); looking past the pillar shows the next room's statue as well."""
    les = new_map("L_NE_Pillar")
    statues = (("SM_Teapot", (0, 0.5, 9), 0.5, math.pi / 2),
               ("SM_Bunny", (0, -0.4, 9), 14.0, math.pi),
               ("SM_Suzanne", (0, 0.9, 9), 1.2, math.pi))
    portals = []
    for i, (mesh, offset, scale, theta) in enumerate(statues):
        x = 200.0 * i  # rooms 200 m apart, as in the original
        tag_space(spawn_mesh("SM_Pillar", f"Pillar{i + 1}", (x, 0, 0), 0.0, (0.1, 0.1, 0.1)), i)
        tag_space(spawn_mesh("SM_PillarRoom", f"PillarRoom{i + 1}", (x, 0, 0), 0.0, (1.1, 1.1, 1.1)), i)
        tag_space(spawn_mesh("SM_Ground", f"Ground{i + 1}", (x, 0, 0), 0.0, (20, 2, 20)), i)
        tag_space(spawn_mesh(mesh, f"Statue{i + 1}", (x + offset[0], offset[1], offset[2]), theta, (scale, scale, scale)), i)
        # PillarRoom::SetPortal: between the pillar and the back wall.
        room_scale = (1.1, 1.1, 1.1)
        pos = local_to_world((x, 0, 0), 0.0, room_scale, (0, 1.5, -1))
        portals.append(tag_space(spawn_portal_orig(f"Portal{i + 1}", pos, -math.pi / 2, (1.1, 1.65, 1.1)), i))
    p1, p2, p3 = portals
    # Portal::Connect(p1->front, p2->back), (p2->front, p3->back), (p3->front, p1->back)
    link(p1, p2, p3)
    link(p2, p3, p1)
    link(p3, p1, p2)
    spawn_sky()
    spawn_player_start((0, 1.5, 3))
    les.save_current_level()
    log("map L_NE_Pillar saved")


# --------------------------------------------------------------------------- Level4 (slope)
def build_level4():
    """Original Level4.cpp: a hill with a tunnel through it. Going down the tunnel brings you
    out on top of the hill, so you can walk "uphill both ways"."""
    les = new_map("L_NE_Slope")
    t1 = ((0, 0, 0), math.pi, (1, 1, 5))
    t2 = ((200, 0, 0), 0.0, (1, 1, 5))
    tag_space(spawn_mesh("SM_TunnelSlope", "Tunnel1", *t1), 0)
    tag_space(spawn_mesh("SM_GroundSlope", "Ground1", (0, 0, 0), 0.0, (10, 2, 10)), 0)
    tag_space(spawn_mesh("SM_TunnelSlope", "Tunnel2", *t2), 1)
    tag_space(spawn_mesh("SM_GroundSlope", "Ground2", (200, 0, 0), math.pi, (10, 2, 10)), 1)

    def slope_door(tunnel, door, extra_theta=0.0):
        """Tunnel::SetDoor1 / SetDoor2 for the SLOPE type."""
        pos_t, theta, scale = tunnel
        local = (0, 1, 1) if door == 1 else (0, -1, -1)
        pos = local_to_world(pos_t, theta, scale, local)
        return pos, theta + extra_theta, (0.6 * scale[0], 0.999 * scale[0], 1 * scale[0])

    p1 = tag_space(spawn_portal_orig("Portal1", *slope_door(t1, 1)), 0)
    p2 = tag_space(spawn_portal_orig("Portal2", *slope_door(t1, 2)), 0)
    p3 = tag_space(spawn_portal_orig("Portal3", *slope_door(t2, 1, -math.pi)), 1)
    p4 = tag_space(spawn_portal_orig("Portal4", *slope_door(t2, 2, -math.pi)), 1)
    connect(p1, p4)
    connect(p2, p3)
    spawn_sky()
    spawn_player_start((0, 1.5 - 2, 8))
    les.save_current_level()
    log("map L_NE_Slope saved")


# --------------------------------------------------------------------------- Level5 (scale)
def build_level5():
    """Original Level5.cpp: a tunnel whose far exit is half the size of its entrance, linked to a
    normal tunnel, so each lap through it halves the player; a tiny tunnel only fits after two laps.
    Going the other way doubles the player."""
    les = new_map("L_NE_Scale")
    t1 = ((-1.2, 0, 0), 0.0, (1, 1, 2.4))     # SCALE tunnel (tapers to half size)
    t2 = ((201.2, 0, 0), 0.0, (1, 1, 2.4))    # NORMAL tunnel, in the other space
    tag_space(spawn_mesh("SM_TunnelScale", "Tunnel1_Scale", *t1), 0)
    tag_space(spawn_mesh("SM_Ground", "Ground1", (0, 0, 0), 0.0, (12, 1.2, 12)), 0)
    tag_space(spawn_mesh("SM_Tunnel", "Tunnel2", *t2), 1)
    tag_space(spawn_mesh("SM_Ground", "Ground2", (200, 0, 0), 0.0, (12, 1.2, 12)), 1)
    tag_space(spawn_mesh("SM_Tunnel", "Tunnel3_Tiny", (-1, 0, -4.2), math.pi / 2, (0.25, 0.25, 0.6)), 0)

    def scale_door2(tunnel):
        """Tunnel::SetDoor2 for the SCALE type: half-size door at the narrow end."""
        pos_t, theta, scale = tunnel
        pos = local_to_world(pos_t, theta, scale, (0, 0.5, -1))
        return pos, theta, (0.3 * scale[0], 0.499 * scale[0], 0.5 * scale[0])

    p1 = tag_space(spawn_portal_orig("Portal1", *tunnel_door(*t1, 1)), 0)
    p2 = tag_space(spawn_portal_orig("Portal2", *tunnel_door(*t2, 1)), 1)
    p3 = tag_space(spawn_portal_orig("Portal3", *scale_door2(t1)), 0)
    p4 = tag_space(spawn_portal_orig("Portal4", *tunnel_door(*t2, 2)), 1)
    connect(p1, p2)
    connect(p3, p4)
    spawn_sky()
    spawn_player_start((0, 1.5, 5))
    les.save_current_level()
    log("map L_NE_Scale saved")


# --------------------------------------------------------------------------- mirror tunnel
MIRROR_THETA = math.atan2(0.36, 0.48)  # tunnel along the sun's azimuth (original LIGHT x, z)


def build_mirror():
    """Not in the original: a tunnel whose middle portal mirrors the world. Walk through and every
    landmark is on the other side; walk around and through again and it's back.

    Two identical copies of the scene (spaces 0 and 1, 200 m apart) are linked by a portal in the
    middle of each tunnel; the copy's portal has X scale -0.6 instead of 0.6, which makes the warp
    a reflection across the tunnel's centre plane. The tunnel is left-right symmetric and runs
    along the sun's azimuth, so the shading (dot(N, LIGHT)) and the sky are symmetric about that
    plane too and the seam doesn't show. The landmarks are deliberately asymmetric.
    """
    les = new_map("L_NE_Mirror")
    theta = MIRROR_THETA
    # (mesh, tunnel-local position, scale, extra yaw): left is -x, right is +x, ahead is -z.
    landmarks = (("SM_Teapot", (-2.5, 0.5, -7), 0.5, math.pi / 2),
                 ("SM_Bunny", (3.2, -0.4, -9.5), 14.0, math.pi * 0.75),
                 ("SM_Suzanne", (-3.5, 0.9, 6.5), 1.2, -math.pi / 2),
                 ("SM_Pillar", (4.0, 0, 4), 0.1, 0.0),
                 ("SM_Pillar", (-6.0, 0, -4), 0.1, 0.0))
    portals = []
    for space, x in enumerate((0.0, 200.0)):
        base = (x, 0, 0)
        tag_space(spawn_mesh("SM_Ground", f"Ground{space + 1}", base, theta, (15, 1.2, 15)), space)
        tag_space(spawn_mesh("SM_Tunnel", f"Tunnel{space + 1}", base, theta, (1, 1, 4)), space)
        for i, (mesh, local, scale, yaw) in enumerate(landmarks):
            pos = local_to_world(base, theta, (1, 1, 1), local)
            tag_space(spawn_mesh(mesh, f"{mesh[3:]}{i + 1}_{space + 1}", pos, theta + yaw, (scale,) * 3), space)
        width = 0.6 if space == 0 else -0.6  # the copy's portal is mirrored
        pos = local_to_world(base, theta, (1, 1, 4), (0, 1, 0))
        portals.append(tag_space(spawn_portal_orig(f"Portal{space + 1}", pos, theta, (width, 0.999, 1)), space))
    connect(*portals)
    spawn_sky()
    spawn_player_start(local_to_world((0, 0, 0), theta, (1, 1, 1), (0, 1.5, 8)), theta)
    les.save_current_level()
    log("map L_NE_Mirror saved")


# --------------------------------------------------------------------------- Phase 1 spike
def build_portal_spike():
    """Two rendering tests.

    Identity portal (around the origin, seen from the PlayerStart): a portal linked to itself.
    Its capture must reproduce the player's own view exactly, so the portal should be invisible;
    compare screenshots with and without "Portal.Hide 1".

    Mirror corridor (UE x = 50 m): two door frames 4 m apart, linked to each other. Looking
    through one shows the corridor again, recursively, down to Portal.MaxDepth, ending in pink.
    Pose: -NEPose=5000,0,150,90,0
    """
    les = new_map("L_NE_PortalSpike")
    spawn_sky()

    # Identity portal in front of some textured geometry.
    spawn_mesh("SM_Ground", "Ground_Identity", (0, 0, 0), 0.0, (12, 1.2, 12))
    spawn_mesh("SM_Tunnel", "Tunnel_Identity", (0.8, 0, -4), 0.5, (1, 1, 2))
    spawn_mesh("SM_Pillar", "Pillar_Identity", (-1.5, 0, -2.5), 0.0, (0.1, 0.1, 0.1))
    identity = spawn_portal("Portal_Identity", unreal.Vector(0, 0, 120), 0.0, unreal.Vector(1.5, 1.0, 1.0))
    link(identity, identity, identity)
    spawn_player_start((0, 1.5, 6))

    # Mirror corridor.
    spawn_mesh("SM_Ground", "Ground_Corridor", (50, 0, 0), 0.0, (12, 1.2, 12))
    a_frame = ((50, 0, 2), 0.0, (1, 1, 0.1))
    b_frame = ((50, 0, -2), 0.0, (1, 1, 0.1))
    spawn_mesh("SM_Tunnel", "Frame_A", *a_frame)
    spawn_mesh("SM_Tunnel", "Frame_B", *b_frame)
    a = spawn_portal_orig("Portal_A", (50, 1, 2), 0.0, (0.6, 0.999, 1))
    b = spawn_portal_orig("Portal_B", (50, 1, -2), 0.0, (0.6, 0.999, 1))
    connect(a, b)

    les.save_current_level()
    log("map L_NE_PortalSpike saved")


# --------------------------------------------------------------------------- object puzzles
PUZZLE_DIR = f"{ROOT}/Puzzle"
PUZZLE_MESH_DIR = f"{PUZZLE_DIR}/Meshes"
PUZZLE_MAT_DIR = f"{PUZZLE_DIR}/Materials"
KEY_CELLS = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 1, 1)]  # as in make_puzzle_meshes.py
KEY_CELL = 20.0
CRATE_SIZE = 40.0
TUNNEL_YAW_ALONG_LIGHT = -math.degrees(MIRROR_THETA)  # walls lit the same on both sides


def puzzle_material(name, translucent=False):
    """Like M_NE_Textured (texture.frag's shading) with a colour parameter instead of a texture."""
    path = f"{PUZZLE_MAT_DIR}/{name}"
    fresh(path)
    mat = asset_tools.create_asset(name, PUZZLE_MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.8, 0.8, 0.8, 1.0))
    nrm = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -700, 300)
    code = (
        f"float s = dot(normalize(N), {LIGHT_UE}) * 0.5 + 0.5;\n"
        "float3 c = saturate(Color * s);\n" + SRGB_DECODE
    )
    node = custom_node(mat, code, ["Color", "N"], -300, 0)
    mel.connect_material_expressions(color, "", node, "Color")
    mel.connect_material_expressions(nrm, "", node, "N")
    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if translucent:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("two_sided", True)
        opacity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -300, 300)
        opacity.set_editor_property("parameter_name", "Opacity")
        opacity.set_editor_property("default_value", 0.3)
        mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log(f"material {path}")
    return mat


def puzzle_instance(name, parent, rgb):
    path = f"{PUZZLE_MAT_DIR}/{name}"
    fresh(path)
    mi = asset_tools.create_asset(name, PUZZLE_MAT_DIR, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    eal.save_loaded_asset(mi)
    return mi


def build_puzzle_materials():
    color = puzzle_material("M_PW_Color")
    ghost = puzzle_material("M_PW_Ghost", translucent=True)
    puzzle_instance("MI_PW_KeyHead", color, (0.9, 0.18, 0.12))
    puzzle_instance("MI_PW_Crate", color, (0.95, 0.55, 0.15))
    puzzle_instance("MI_PW_Gate", color, (0.55, 0.35, 0.8))
    puzzle_instance("MI_PW_Ghost", ghost, (0.35, 0.9, 1.0))


def import_puzzle_mesh(name):
    """Imports SourceArt/.../Generated/Puzzle/<name>.glb as PUZZLE_MESH_DIR/<name>."""
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    task = unreal.AssetImportTask()
    task.filename = f"{project}SourceArt/NonEuclidean/Generated/Puzzle/{name}.glb"
    task.destination_path = f"{PUZZLE_MESH_DIR}/Import_{name}"
    task.automated = True
    task.replace_existing = True
    task.save = True
    asset_tools.import_asset_tasks([task])
    mesh = None
    for path in task.imported_object_paths:
        asset = eal.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            mesh = asset
    if not mesh:
        raise RuntimeError(f"importing {task.filename} made no static mesh: {list(task.imported_object_paths)}")
    flat = f"{PUZZLE_MESH_DIR}/{name}"
    if eal.does_asset_exist(flat):
        eal.delete_asset(flat)
    eal.rename_asset(mesh.get_path_name().split(".")[0], flat)
    mesh = eal.load_asset(flat)
    return mesh


def configure_puzzle_mesh(mesh, slot_materials, boxes):
    """Materials by slot name, no Nanite, and simple box collision (boxes work mirrored)."""
    slots = mesh.get_editor_property("static_materials")
    for i, slot in enumerate(slots):
        slot_name = str(slot.get_editor_property("material_slot_name"))
        material = next((m for key, m in slot_materials.items() if key.lower() in slot_name.lower()), None)
        if material is None:
            raise RuntimeError(f"{mesh.get_name()}: no material for slot {slot_name}")
        mesh.set_material(i, material)
    nanite = mesh.get_editor_property("nanite_settings")
    nanite.set_editor_property("enabled", False)
    mesh.set_editor_property("nanite_settings", nanite)

    unreal.EditorStaticMeshLibrary.remove_collisions(mesh)
    body = mesh.get_editor_property("body_setup")
    agg = body.get_editor_property("agg_geom")
    elems = []
    for centre, size in boxes:
        box = unreal.KBoxElem()
        box.set_editor_property("center", unreal.Vector(*centre))
        box.set_editor_property("x", size)
        box.set_editor_property("y", size)
        box.set_editor_property("z", size)
        elems.append(box)
    agg.set_editor_property("box_elems", elems)
    body.set_editor_property("agg_geom", agg)
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
    eal.save_loaded_asset(mesh)
    b = mesh.get_bounding_box()
    log(f"puzzle mesh {mesh.get_name()}: bounds {b.min} .. {b.max}, {len(elems)} boxes")


def build_puzzle_meshes():
    def mat(path):
        return eal.load_asset(path)

    key = import_puzzle_mesh("SM_PW_Key")
    n = len(KEY_CELLS)
    centroid = [sum(c[i] for c in KEY_CELLS) / n for i in range(3)]
    key_boxes = [([(c[i] - centroid[i]) * KEY_CELL for i in range(3)], KEY_CELL) for c in KEY_CELLS]
    configure_puzzle_mesh(key, {"Head": mat(f"{PUZZLE_MAT_DIR}/MI_PW_KeyHead"), "Body": mat(f"{MAT_DIR}/MI_NE_Gold")}, key_boxes)

    crate = import_puzzle_mesh("SM_PW_Crate")
    configure_puzzle_mesh(crate, {"Crate": mat(f"{PUZZLE_MAT_DIR}/MI_PW_Crate")}, [((0, 0, 0), CRATE_SIZE)])
    for name in ("SM_PW_Key", "SM_PW_Crate"):
        if eal.does_directory_exist(f"{PUZZLE_MESH_DIR}/Import_{name}"):
            eal.delete_directory(f"{PUZZLE_MESH_DIR}/Import_{name}")  # interchange's own materials


def spawn_mesh_ue(mesh_path, label, loc, yaw=0.0, scale=(1, 1, 1), material=None, folder="Geometry"):
    """A static mesh actor placed in Unreal coordinates (cm)."""
    actor = actor_sub.spawn_actor_from_object(eal.load_asset(mesh_path), unreal.Vector(*loc), unreal.Rotator(0, 0, yaw))
    actor.set_actor_label(label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.set_folder_path(folder)
    comp = actor.static_mesh_component
    comp.set_editor_property("cast_shadow", False)
    if material:
        comp.set_material(0, eal.load_asset(material))
    return actor


def spawn_tunnel_with_portal(label, centre, yaw, size, half_length, portal_width_sign=1.0):
    """A SM_Tunnel (1.2 m x 2 m inside, times size) centred on centre, running along the yaw's
    local Y for +/-half_length cm, with a portal across its middle."""
    spawn_mesh_ue(f"{MESH_DIR}/SM_Tunnel", f"Tunnel_{label}", (centre[0], centre[1], 0), yaw,
                  (size, half_length / 100.0, size))
    loc = unreal.Vector(centre[0], centre[1], 100.0 * size)
    return spawn_portal(f"Portal_{label}", loc, yaw, unreal.Vector(0.6 * size * portal_width_sign, size, 0.999 * size))


def facing_yaw(src, dst):
    """Yaw that points an actor's +X from src towards dst."""
    return math.degrees(math.atan2(dst[1] - src[1], dst[0] - src[0]))


def build_puzzle_map():
    """L_NE_ObjectPuzzles: carrying and throwing objects through a mirror tunnel and a shrinking
    tunnel to fill four sockets. See the docstring of ANEPuzzleGameMode and the README."""
    les = new_map("L_NE_ObjectPuzzles")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode", unreal.load_class(None, "/Script/NonEuclideanTest.NEPuzzleGameMode"))

    spawn_mesh("SM_Ground", "Ground", (0, 0, 0), 0.0, (20, 1.2, 20))  # 40 m x 40 m
    spawn_sky()
    start = (0.0, 1600.0)
    ps = actor_sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(start[0], start[1], 92), unreal.Rotator(0, 0, -90))
    ps.set_folder_path("Environment")

    # Mirror station (west): walk into tunnel A and come out of tunnel B mirrored. Both tunnels run
    # along the light's direction, so the walls are lit the same on both sides and the seam doesn't show.
    yaw = TUNNEL_YAW_ALONG_LIGHT
    axis = (math.sin(MIRROR_THETA), math.cos(MIRROR_THETA))  # the tunnels' local Y in the world: (0.6, 0.8)
    # B runs parallel to A, 7 m to the side: in line with A, the view through A would show A again.
    side = (axis[1], -axis[0])
    mirror_a = (-900.0, 300.0)
    mirror_b = (mirror_a[0] - side[0] * 700.0, mirror_a[1] - side[1] * 700.0)
    pa = spawn_tunnel_with_portal("MirrorA", mirror_a, yaw, 1.0, 200.0)
    pb = spawn_tunnel_with_portal("MirrorB", mirror_b, yaw, 1.0, 200.0, portal_width_sign=-1.0)
    connect(pa, pb)

    # Shrink station (east): tunnel "Big" leads into the half-size tunnel "Small" (x0.5), and back (x2).
    pbig = spawn_tunnel_with_portal("ShrinkBig", (900.0, 300.0), 0.0, 1.0, 200.0)
    psmall = spawn_tunnel_with_portal("ShrinkSmall", (1400.0, -700.0), 0.0, 0.5, 100.0)
    connect(pbig, psmall)

    # Objects.
    obj_class = unreal.load_class(None, "/Script/NonEuclideanTest.NEPuzzleObject")
    key_mesh = eal.load_asset(f"{PUZZLE_MESH_DIR}/SM_PW_Key")
    crate_mesh = eal.load_asset(f"{PUZZLE_MESH_DIR}/SM_PW_Crate")
    objects = (("Mirror key", "Key", key_mesh, (-250.0, 1150.0, 30.0), 20.0),
               ("Spare key", "Key", key_mesh, (250.0, 1150.0, 30.0), -20.0),
               ("Crate A", "Crate", crate_mesh, (650.0, 1050.0, 25.0), 10.0),
               ("Crate B", "Crate", crate_mesh, (1150.0, 1050.0, 25.0), -15.0))
    for label, kind, mesh, loc, obj_yaw in objects:
        obj = actor_sub.spawn_actor_from_class(obj_class, unreal.Vector(*loc), unreal.Rotator(0, 0, obj_yaw))
        obj.set_actor_label(label.replace(" ", ""))
        obj.set_folder_path("Objects")
        obj.set_editor_property("label", label)
        obj.set_editor_property("kind", kind)
        obj.set_editor_property("object_mesh", mesh)

    # Sockets: (label, kind, size, mirrored, mesh, position).
    socket_class = unreal.load_class(None, "/Script/NonEuclideanTest.NEPuzzleSocket")
    sockets = []
    for label, kind, size, mirrored, mesh, loc in (
            ("Mirror socket", "Key", 1.0, True, key_mesh, (-350.0, 800.0)),
            ("Small mirror socket", "Key", 0.5, True, key_mesh, (250.0, -650.0)),
            ("Small socket", "Crate", 0.5, False, crate_mesh, (1400.0, -1150.0)),
            ("Big socket", "Crate", 2.0, False, crate_mesh, (900.0, 850.0))):
        sock = actor_sub.spawn_actor_from_class(socket_class, unreal.Vector(loc[0], loc[1], 0),
                                                unreal.Rotator(0, 0, facing_yaw(loc, start)))
        sock.set_actor_label(label.title().replace(" ", ""))
        sock.set_folder_path("Sockets")
        for prop, value in (("label", label), ("required_kind", kind), ("required_size", size),
                            ("require_mirrored", mirrored), ("ghost_mesh", mesh),
                            ("ghost_material", eal.load_asset(f"{PUZZLE_MAT_DIR}/MI_PW_Ghost")),
                            ("pedestal_material", eal.load_asset(f"{MAT_DIR}/MI_NE_White")),
                            ("lamp_material", eal.load_asset(f"{PUZZLE_MAT_DIR}/M_PW_Color"))):
            sock.set_editor_property(prop, value)
        sockets.append(sock)

    # The reward: a pen with Suzanne in it, closed by a gate that opens when every socket is solved.
    white = f"{MAT_DIR}/MI_NE_White"
    cube = "/Engine/BasicShapes/Cube"
    pen_y, pen_depth, pen_width, height = -1100.0, 300.0, 340.0, 250.0
    for label, loc, scale in (
            ("PenWallLeft", (-pen_width / 2, pen_y - pen_depth / 2, height / 2), (0.2, pen_depth / 100, height / 100)),
            ("PenWallRight", (pen_width / 2, pen_y - pen_depth / 2, height / 2), (0.2, pen_depth / 100, height / 100)),
            ("PenWallBack", (0.0, pen_y - pen_depth, height / 2), ((pen_width + 20) / 100, 0.2, height / 100))):
        spawn_mesh_ue(cube, label, loc, 0.0, scale, white, folder="Pen")
    spawn_mesh("SM_Suzanne", "Trophy", (0.0, 0.9, (pen_y - pen_depth / 2) / 100.0), math.pi, (1.2, 1.2, 1.2), folder="Pen")
    gate = actor_sub.spawn_actor_from_class(unreal.load_class(None, "/Script/NonEuclideanTest.NEPuzzleGate"),
                                            unreal.Vector(0.0, pen_y, height / 2), unreal.Rotator(0, 0, 0))
    gate.set_actor_label("Gate")
    gate.set_folder_path("Pen")
    gate.set_actor_scale3d(unreal.Vector((pen_width - 20) / 100, 0.2, height / 100))
    gate.get_component_by_class(unreal.StaticMeshComponent).set_material(0, eal.load_asset(f"{PUZZLE_MAT_DIR}/MI_PW_Gate"))
    gate.set_editor_property("sockets", sockets)

    les.save_current_level()
    log("map L_NE_ObjectPuzzles saved")


def build_puzzle():
    build_puzzle_materials()
    build_puzzle_meshes()
    build_puzzle_map()


# --------------------------------------------------------------------------- main
def main():
    global actor_sub
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    stages = sys.argv[1:] or ["materials", "meshes", "maps"]
    log(f"stages: {stages}")

    if "materials" in stages:
        textured = build_textured()
        build_sky()
        # The portal material, mesh and render target live in the PortalWorlds plugin.
        for name, tex in (("MI_NE_CheckerGray", "T_CheckerGray"), ("MI_NE_CheckerGreen", "T_CheckerGreen"),
                          ("MI_NE_ThreeRoom", "T_ThreeRoom"), ("MI_NE_ThreeRoom2", "T_ThreeRoom2"),
                          ("MI_NE_White", "T_White"), ("MI_NE_Gold", "T_Gold")):
            make_instance(name, textured, tex)
    if "meshes" in stages:
        organise_meshes()
    if "maps" in stages:
        build_level1()
        build_portal_spike()
        build_level2(3, "L_NE_House3")
        build_level2(6, "L_NE_House6")
        build_level3()
        build_level4()
        build_level5()
    if "maps" in stages or "mirror" in stages:
        build_mirror()
    if "maps" in stages or "puzzle" in stages:
        build_puzzle()


main()
