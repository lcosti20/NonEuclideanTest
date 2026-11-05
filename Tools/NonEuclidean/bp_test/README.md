# Blueprint-only project test for PortalWorlds

Checks the plugin in a project with no C++ module, using the prebuilt plugin copy.

1. Create a folder with a `.uproject` that has no `Modules`, enabling `PortalWorlds`,
   `PythonScriptPlugin` and `EditorScriptingUtilities`. Set `r.AllowGlobalClipPlane=True`,
   `r.AllowOcclusionQueries=False`, and the game mode/default map (`/Game/Test/...`) in its config.
   Copy `Saved/PluginPackage/PortalWorlds` (from `../package_plugin.ps1`) into its `Plugins/`.
2. `UnrealEditor.exe <bp project>.uproject -unattended -nosplash -ExecutePythonScript="<this folder>/build_bp_test_content.py"`
   creates a Blueprint character (camera + Portal Traversal, Eye left empty), a controller using
   `PortalPlayerCameraManager`, a game mode, and `L_PortalFall`: two horizontal portals the player
   falls through forever.
3. `... -ExecutePythonScript="<this folder>/pie_test.py"` checks the Blueprint API is exposed, plays in
   editor, binds to **On Portal Traversed** and reports the event, traversal count and subsystem queries.
4. `... -ExecutePythonScript="<this folder>/build_bp_mirror_content.py"` adds the mirror tunnel:
   `BP_MirrorFlyer` (the engine's flying `DefaultPawn`, WASD/mouse, E/Q up/down, plus a Portal
   Traversal component), `BP_MirrorGameMode`, and `L_MirrorTunnel`, built from engine shapes with the
   project's default renderer. Two copies of a tunnel scene are linked by a portal across the middle
   of each tunnel; the copy's portal has X scale −1.05, so flying through mirrors the world. Add
   `+MapsToCook=(FilePath="/Game/Test/L_MirrorTunnel")` to `DefaultGame.ini` to package it, and run
   the packaged game with `/Game/Test/L_MirrorTunnel` to open it. The script can be re-run.
5. `... -ExecutePythonScript="<this folder>/mirror_pie_test.py"` plays the mirror tunnel in editor:
   screenshots for `../mirror_check.py` (also with Portal2 made a plain link, for comparison), a flight
   through and back, and yaw and strafe input while mirrored and not. It ends with `RESULT PASS/FAIL`.
6. Package with `RunUAT BuildCookRun -project=<bp project> -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory=<dir>`,
   run the game with `-log -LogCmds="LogPortalWorlds Verbose"`, and look for `Traversed` lines.

Results (UE 5.8, 4 Nov 2026):
- **Editor:** the prebuilt plugin loaded without compiling. The Blueprint API was all present. Eye defaulted to the camera. **On Portal Traversed** fired 9 times in 6 s of play-in-editor (matching **Get Traversal Count**), and the subsystem queries returned sensible values.
- **Packaging:** this needed `"EnabledByDefault": false` in the plugin descriptor. Without it, Unreal packaged the Blueprint project with the stock game executable, and the game logged "Plugin 'PortalWorlds' failed to load because module 'PortalWorlds' could not be found". With it, Unreal generated a temporary C++ target and compiled the plugin into the game.
- **Packaged game:** 4 portal captures rendered, and 54 traversals in 25 s.
- **Final check:** repeated with the exact output of `package_plugin.ps1` dropped into `Plugins/`. Unreal built the plugin into the game; 38 traversals in 20 s, no plugin errors.

Mirror tunnel (UE 5.8, 4 Nov 2026, plugin from commit a3a1475):
- **Play-in-editor:** `RESULT PASS`. Flying through logged "Portal1 -> Portal2 (now mirrored)"; Is Mirrored and Is View Mirrored were true and the current space switched to 1. While mirrored, Add Yaw Input +1 turned the control rotation −75° and strafing right moved −120 cm along its right (both reversed, so they look normal on the flipped screen); unmirrored, +75° and +121 cm. Flying back logged "no longer mirrored".
- **Image accuracy** (`mirror_check.py`, Lumen, auto-exposure and TSR on): inside the portal 4.1 and 4.9/255 mean difference at the two poses. A plain portal in the same scene, checked the same way, gives 5.7 and 3.9/255, so mirroring adds no error. In this renderer any portal differs slightly from the direct view, mostly along its outline (see the plugin README's limitations).
- **Packaged game:** builds, opens `L_MirrorTunnel` with no plugin errors, and its container holds `L_MirrorTunnel`, `BP_MirrorFlyer` and `M_PortalMirrorView`.
- Found on the way: a stationary sun shows the engine's "Preview" shadow text until lighting is built, which showed up (mirrored) through the portal; the map's lights are movable.
