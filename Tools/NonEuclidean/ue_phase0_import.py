"""Phase 0, step 1: import the original NonEuclidean textures and converted meshes.

Run headless:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this file> -unattended -nullrhi

Creates /Game/NonEuclidean/Textures/T_* and /Game/NonEuclidean/Meshes/SM_*, and logs
the bounds of SM_AxisCalibration so the glTF -> Unreal axis mapping can be confirmed.
"""

import os
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJECT, "SourceArt", "NonEuclidean")
TEX_DIR = "/Game/NonEuclidean/Textures"
MESH_DIR = "/Game/NonEuclidean/Meshes"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log_warning("[NE] " + msg)  # warnings stand out in the commandlet output


def import_files(files, dest):
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = f
        t.destination_path = dest
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)
    asset_tools.import_asset_tasks(tasks)
    out = []
    for t in tasks:
        paths = list(t.imported_object_paths)
        log(f"imported {os.path.basename(t.filename)} -> {paths}")
        out.extend(paths)
    return out


# --- Textures: nearest filtering, no mips, uncompressed, raw (non-sRGB) values -------------
tex_src = os.path.join(SRC, "Original", "Textures")
tex_files = [os.path.join(tex_src, f) for f in sorted(os.listdir(tex_src)) if f.endswith(".bmp")]
import_files(tex_files, TEX_DIR)
for f in tex_files:
    base = os.path.splitext(os.path.basename(f))[0]
    old = f"{TEX_DIR}/{base}"
    new = f"{TEX_DIR}/T_" + "".join(p.capitalize() for p in base.split("_"))
    if eal.does_asset_exist(new):
        eal.delete_asset(new)
    if eal.does_asset_exist(old):
        eal.rename_asset(old, new)
    tex = eal.load_asset(new)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("srgb", False)  # material does the shading in display space, like the original
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
    eal.save_loaded_asset(tex)
    log(f"texture {new}: {tex.blueprint_get_size_x()}x{tex.blueprint_get_size_y()}")

# --- Meshes ------------------------------------------------------------------------------------
glb_src = os.path.join(SRC, "Generated")
glb_files = [os.path.join(glb_src, f) for f in sorted(os.listdir(glb_src)) if f.endswith(".glb")]
import_files(glb_files, MESH_DIR)

for asset_path in eal.list_assets(MESH_DIR, recursive=True, include_folder=False):
    obj = eal.load_asset(asset_path)
    if isinstance(obj, unreal.StaticMesh):
        # Materials, collision and Nanite are configured by ue_build_content.py.
        b = obj.get_bounding_box()
        log(f"mesh {obj.get_name()}: min={b.min} max={b.max}")
    else:
        log(f"other asset {asset_path} ({obj.get_class().get_name()})")
