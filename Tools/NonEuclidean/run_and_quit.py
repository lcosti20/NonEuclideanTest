"""Run an editor Python script inside the full editor, then close the editor.

  UnrealEditor.exe <project>.uproject -unattended -nosplash
      -ExecutePythonScript="<dir>/run_and_quit.py <script>.py"

<script>.py is resolved relative to this folder (the project path contains spaces,
so full paths can't be passed as arguments).

Some editor operations (spawning actors into a level) don't work in the
pythonscript commandlet, so level-building scripts run this way instead.
"""

import os
import runpy
import sys
import traceback

import unreal

HERE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Tools", "NonEuclidean")

try:
    sys.argv = sys.argv[1:]  # the target script sees its own name as argv[0]
    runpy.run_path(os.path.join(HERE, sys.argv[0]), run_name="__main__")
    unreal.log_warning("[NE] script finished OK")
except Exception:
    unreal.log_error("[NE] script failed:\n" + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
