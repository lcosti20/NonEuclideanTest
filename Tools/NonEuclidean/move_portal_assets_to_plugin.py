"""One-off migration: move the portal assets from the project into the PortalWorlds plugin.

  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this file> -unattended -nullrhi

Moves the portal mesh, material and default render target to /PortalWorlds/..., fixes up
everything that referenced them, and deletes the redirectors left behind.
"""

import unreal

eal = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

MOVES = [
    ("/Game/NonEuclidean/Materials/RT_NE_PortalDefault", "/PortalWorlds/Materials/RT_PortalDefault"),
    ("/Game/NonEuclidean/Materials/M_NE_Portal", "/PortalWorlds/Materials/M_Portal"),
    ("/Game/NonEuclidean/Meshes/SM_DoubleQuad", "/PortalWorlds/Meshes/SM_PortalQuad"),
]


def log(msg):
    unreal.log_warning("[NE] " + msg)


for old, new in MOVES:
    if eal.does_asset_exist(new):
        log(f"already moved: {new}")
        continue
    if not eal.does_asset_exist(old):
        log(f"MISSING {old}")
        continue
    ok = eal.rename_asset(old, new)
    log(f"move {old} -> {new}: {'ok' if ok else 'FAILED'}")

# Point everything at the new locations and remove the redirectors left in the project.
redirectors = []
for path in eal.list_assets("/Game/NonEuclidean", recursive=True, include_folder=False):
    data = eal.find_asset_data(path)
    if data.asset_class_path.asset_name == "ObjectRedirector":
        redirectors.append(eal.load_asset(path))
if redirectors:
    asset_tools.fix_up_referencers(redirectors)
log(f"fixed up {len(redirectors)} redirector(s)")
for path in eal.list_assets("/Game/NonEuclidean", recursive=True, include_folder=False):
    if eal.find_asset_data(path).asset_class_path.asset_name == "ObjectRedirector":
        log(f"redirector still present: {path} (deleting: {eal.delete_asset(path)})")

for _, new in MOVES:
    log(f"{new}: {'present' if eal.does_asset_exist(new) else 'MISSING'}")
quad = eal.load_asset("/PortalWorlds/Meshes/SM_PortalQuad")
if quad:
    log(f"SM_PortalQuad material: {quad.get_material(0).get_path_name()}")
