"""Creates the start menu map, L_NE_StartMenu: an empty level whose game mode is ANEStartMenuGameMode.

The menu itself is code (NEStartMenuPlayerController), so the map has nothing in it. Run once, in the
full editor, after the module is built; it fails if the map already exists:
  UnrealEditor.exe <project>.uproject -unattended -nosplash
      -ExecutePythonScript="<dir>/run_and_quit.py make_start_menu.py"
"""

import unreal

MAP_PATH = "/Game/NonEuclidean/Maps/L_NE_StartMenu"
GAME_MODE_PATH = "/Script/NonEuclideanTest.NEStartMenuGameMode"

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_editor.new_level(MAP_PATH):
    raise RuntimeError(f"could not create {MAP_PATH} (does it already exist?)")

world = unreal.EditorLevelLibrary.get_editor_world()
game_mode = unreal.load_class(None, GAME_MODE_PATH)
if game_mode is None:
    raise RuntimeError(f"{GAME_MODE_PATH} not found: build the editor module first")
world.get_world_settings().set_editor_property("default_game_mode", game_mode)

if not level_editor.save_current_level():
    raise RuntimeError(f"could not save {MAP_PATH}")
unreal.log_warning(f"[NE] saved {MAP_PATH} with game mode {GAME_MODE_PATH}")
