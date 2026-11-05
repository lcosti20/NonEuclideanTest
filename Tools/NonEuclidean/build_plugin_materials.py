"""Builds the PortalWorlds plugin's materials.

  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this file> -unattended

M_Portal (port of portal.frag / pink.frag): the linked view's render target sampled at
screen-space UV, U flipped when MirrorU = 1 (mirroring portals), or flat magenta when Pink = 1.
It's rebuilt in place, so portals and maps that reference it keep working.

M_PortalMirrorView: post-process material, after tonemapping, that shows the scene flipped
left-right when its Mirror parameter is 1. UPortalSubsystem blends it into the player's view in
worlds with mirroring portals (Mirror = 1 while the player is mirrored). It's only refreshed if it
exists; delete the .uasset (editor closed) to rebuild its graph.
"""

import unreal

DIR = "/PortalWorlds/Materials"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

FLIP_CODE = "return float2(lerp(UV.x, 1.0 - UV.x, Mirror), UV.y);"


def log(msg):
    unreal.log_warning("[NE] " + msg)


def flip_node(mat, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", FLIP_CODE)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    inputs = []
    for name in ("UV", "Mirror"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    node.set_editor_property("inputs", inputs)
    return node


def save(mat):
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log(f"material {mat.get_path_name()}")


def build_portal():
    """Inserts the MirrorU flip between ScreenPosition and the PortalTex sample. The graph is
    edited rather than rebuilt: M_Portal is loaded at engine start (APortal's constructor), and
    its existing nodes can't be deleted in that state."""
    mat = eal.load_asset(f"{DIR}/M_Portal")
    lerp = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    tex = next(n for n in mel.get_inputs_for_material_expression(mat, lerp)
               if isinstance(n, unreal.MaterialExpressionTextureSampleParameter2D))
    uv_source = mel.get_inputs_for_material_expression(mat, tex)[0]
    if isinstance(uv_source, unreal.MaterialExpressionCustom):
        log("M_Portal already has the MirrorU flip")
        return
    spos = uv_source  # ScreenPosition (ViewportUV)
    mirror = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1100, 200)
    mirror.set_editor_property("parameter_name", "MirrorU")
    mirror.set_editor_property("default_value", 0.0)
    flip = flip_node(mat, -850, 0)
    mel.connect_material_expressions(spos, "ViewportUV", flip, "UV")
    mel.connect_material_expressions(mirror, "", flip, "Mirror")
    mel.connect_material_expressions(flip, "", tex, "UVs")
    spos.set_editor_property("material_expression_editor_x", -1100)
    save(mat)


def build_mirror_view():
    name = "M_PortalMirrorView"
    path = f"{DIR}/{name}"
    if eal.does_asset_exist(path):
        # Loaded at engine start (UPortalSubsystem's constructor), so its nodes can't be deleted:
        # only the flip code is refreshed.
        mat = eal.load_asset(path)
        scene = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        flip = mel.get_inputs_for_material_expression(mat, scene)[0]
        flip.set_editor_property("code", "// flips the player's view left-right while mirrored (UPortalSubsystem)\n" + FLIP_CODE)
        save(mat)
        return
    mat = asset_tools.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)

    spos = mel.create_material_expression(mat, unreal.MaterialExpressionScreenPosition, -900, 0)
    mirror = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 200)
    mirror.set_editor_property("parameter_name", "Mirror")
    mirror.set_editor_property("default_value", 1.0)
    flip = flip_node(mat, -650, 0)
    flip.set_editor_property("code", "// flips the player's view left-right while mirrored (UPortalSubsystem)\n" + FLIP_CODE)
    mel.connect_material_expressions(spos, "ViewportUV", flip, "UV")
    mel.connect_material_expressions(mirror, "", flip, "Mirror")
    scene = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -350, 0)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    mel.connect_material_expressions(flip, "", scene, "UVs")  # viewport UV
    mel.connect_material_property(scene, "Color", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    save(mat)


build_portal()
build_mirror_view()
