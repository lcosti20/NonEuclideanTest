# Non-Euclidean test: asset pipeline and tools

Recreates the assets and test maps from CodeParade's NonEuclidean engine
([HackerPoet/NonEuclidean](https://github.com/HackerPoet/NonEuclidean), MIT; see
`SourceArt/NonEuclidean/Original/LICENSE`). Plan: `Docs/Plan_Non-Euclidean_worlds_in_Unreal_Engine_5_8.md`.

All commands run from the project root in Git Bash, with the editor closed.
`UE` = `/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64`, `P` = `$(cygpath -m "$PWD")`.

## Rebuild the assets

1. **Convert meshes.** The original `.obj` files use a custom dialect (`f *` wildcard faces,
   `c` collider lines), so they're converted to `.glb`:

   ```sh
   python -I Tools/NonEuclidean/convert_original_meshes.py SourceArt/NonEuclidean/Original/Meshes SourceArt/NonEuclidean/Generated
   ```

2. **Import textures and meshes** (headless):

   ```sh
   "$UE/UnrealEditor-Cmd.exe" "$P/NonEuclideanTest.uproject" -run=pythonscript -script="$P/Tools/NonEuclidean/ue_phase0_import.py" -unattended -nullrhi
   ```

3. **Build materials and maps (optionally only some stages: `materials`, `meshes`, `maps`).** This step needs the full editor, because spawning actors
   doesn't work in the commandlet. The editor opens, runs the script and closes:

   ```sh
   "$UE/UnrealEditor.exe" "$P/NonEuclideanTest.uproject" -unattended -nosplash -ExecutePythonScript="$P/Tools/NonEuclidean/run_and_quit.py ue_build_content.py"
   ```

`dump_map.py` (run like step 3) logs every actor and transform in a map, for debugging.

## Coordinate conversion

Confirmed with an imported calibration mesh:

| Original (m, Y-up, right-handed) | Unreal (cm, Z-up, left-handed) |
| --- | --- |
| position `(x, y, z)` | `(x, z, y) * 100` |
| scale `(sx, sy, sz)` | `(sx, sz, sy)` |
| object yaw `theta` (rad, about +Y) | yaw `-theta` (deg) |
| camera yaw `theta` (looks along -Z at 0) | yaw `-90 - theta` (deg) |
| light `LIGHT (0.36, 0.80, 0.48)` | `(0.36, 0.48, 0.80)` |

## Comparing against the video

`ANEGameMode` supports automated screenshots:

```sh
MSYS_NO_PATHCONV=1 "$UE/UnrealEditor.exe" "$P/NonEuclideanTest.uproject" /Game/NonEuclidean/Maps/L_NE_Tunnels \
  -game -windowed -ResX=1280 -ResY=720 -NEScreenshot=2 -NEPose=138.2,810.2,144.7,-108.62,-2.28
```

This writes `Saved/Screenshots/WindowsEditor/NE_<map>.png`. `MSYS_NO_PATHCONV=1` stops Git Bash
rewriting `/Game/...`. The pose above was fitted to the video frame at 3:48.5 (0.9 px RMS on the
portal corners). From that pose the render matches the frame to 4.6/255 mean absolute difference,
with 97.9% overlap of the magenta portal surfaces.

`shot.sh` wraps this: `Tools/NonEuclidean/shot.sh <map> <name> [args]` writes `Saved/Screenshots/NE/<name>.png`.
Extra options: `-NESpin=<deg/s> -NEStrafe=<cm/s>` move the camera deterministically for 30 frames before
the shot, and `-ExecCmds="Portal.Hide 1"` etc. set console variables.

## Portal rendering checks (Phase 1)

Portal console variables: `Portal.MaxDepth` (default 4, as in the original), `Portal.MaxCaptures`,
`Portal.Debug` (all pink), `Portal.Hide`, `Portal.DynamicNearPlane`, `Portal.ResolutionFalloff`,
`Portal.ShowStats`.

| Check | Command (map `L_NE_PortalSpike` unless noted) | Result |
| --- | --- | --- |
| Identity portal is invisible | `shot.sh L_NE_PortalSpike a` vs `... b -ExecCmds="Portal.Hide 1"` | max difference 1/255 over the 18.6% of the screen covered by the portal |
| No lag while moving | both of the above with `-NEPose=0,600,150,-90,0 -NESpin=730 -NEStrafe=400` | identical except 1 edge pixel (portal covers 7.8% of the screen) |
| Recursion | `-NEPose=5000,-150,150,90,0`, with and without `-ExecCmds="Portal.MaxDepth 8"` | nested corridor ending in pink after 3 levels (3 captures) or 7 levels (7 captures) |
| Dynamic near plane | `-NEPose=0,2,150,-90,0 -ExecCmds="Portal.Debug 1"`, and with `Portal.DynamicNearPlane 0` | portal fills 100% of the screen at 2 cm, versus 0% with a fixed 10 cm near plane |
| Level1 vs video | `L_NE_Tunnels`, video pose, `Portal.MaxDepth 2` and default | matches the video at 3:51 / 3:52.8 (mean difference 5/255; same pink regions at depth 2) |

## Traversal checks (Phase 2)

Motion options for `shot.sh`: `-NEForward=<cm/s>`, `-NEMotionFrames=<n>`, `-NESequence` (save every
frame to `Saved/Screenshots/NE/<name>/`), and `-NEUseInput` (walk with real character movement and
collision instead of moving the pawn directly; frame-rate dependent, so `-NEMotionFrames` is frames,
not time). `seq_continuity.py <folder> [frames]` reports the frame-to-frame change, to spot pops.

| Check (map `L_NE_Tunnels`) | Command | Result |
| --- | --- | --- |
| Long tunnel is short inside (M1) | `-NEPose=-240,500,150,-90,0 -NEForward=290 -NEMotionFrames=120 -NESequence` | Portal1→Portal2, 1.2 m inside, Portal4→Portal3; no change spike at either traversal frame |
| Short tunnel is long inside (M2) | `-NEPose=240,500,150,-90,0 -NEForward=290 -NEMotionFrames=330 -NESequence` | Portal2→Portal1, 9.6 m inside, Portal3→Portal4; no spike |
| Walking backwards through | `-NEPose=-240,-800,150,-90,0 -NEForward=-290 -NEMotionFrames=150 -NESequence` | Portal3→Portal4→Portal2→Portal1; no spike |
| Real character movement | `-NEPose=240,500,150,-90,0 -NEForward=1 -NEMotionFrames=200 -NEUseInput -NESequence` | walks through both portals with collision, head bob intact |
| Turned around inside | `-NEPose=-240,-200,150,90,0` | the doorway shows the outside of the other tunnel; floor checkers continue across it |

## House checks (Phase 3)

Spaces: tag actors `PortalSpace.<n>` to put them in space n (the build script does this for the houses);
`Portal.Spaces 0` turns the filtering off for debugging. Scripted paths: `-NEScript=<frames>:<cm/s>:<deg/s>;...`
(positive deg/s turns right), `-NESequenceEvery=<k>`. `floor_rooms.py <folder>` names the room under the
camera in each frame by its floor colour.

Loop script used below (start outside the entrance, 6 m/s):
`SCRIPT="180:600:0;30:0:180;130:600:0;30:0:180;110:600:0;30:0:180;110:600:0;30:0:180;130:600:0;30:0:180;110:600:0"`

| Check | Command | Result |
| --- | --- | --- |
| Missing room (M3), `L_NE_House3` | `-NEPose=300,300,150,-90,0 "-NEScript=$SCRIPT" -NESequenceEvery=10` | blue → red → green → blue → red → green → blue: the grey room never appears; the D3 doorway shows the blue room |
| Extra rooms (M4), `L_NE_House6` | same | blue → red → green → grey → purple → orange → blue: six rooms in a four-room footprint |
| Other space hidden, `L_NE_House6` | `-NEPose=2600,300,150,0,0`, with and without `-ExecCmds="Portal.Spaces 0"` | horizon band empty with spaces on; house 2 visible (867 px) with them off |

## Pillar room checks (Phase 4)

`-NEPoses=<pose>|<pose>|...` takes one screenshot per pose in a single run
(`Saved/Screenshots/NE/<name>/..._poseNN.png`).

| Check (map `L_NE_Pillar`) | Command | Result |
| --- | --- | --- |
| Ring, counter-clockwise | `-NEPose=120,0,150,-90,0 "-NEScript=680:200:-95.49"` (three laps of a 1.2 m circle) | Portal1→2→3→1: teapot → bunny → Suzanne → teapot |
| Ring, clockwise | `-NEPose=-120,0,150,-90,0 "-NEScript=680:200:95.49"` | Portal1→3→2→1 |
| No pops, no pink | the counter-clockwise run with `-NESequence` | one traversal per lap (every 226 frames), no change spike, 0 pink pixels, at most 1 capture per frame |

## Slope checks (Phase 5)

For real-input walks of a fixed duration, add `-benchmark -fps=60` (fixed 1/60 s time step), so
`-NEMotionFrames` counts sixtieths of a second.

| Check (map `L_NE_Slope`) | Command | Result |
| --- | --- | --- |
| Down the tunnel to the top (M6) | `-NEPose=0,800,-50,-90,0 -NEForward=1 -NEMotionFrames=320 -NEUseInput -benchmark -fps=60` | eye −47 cm at the foot of the hill → Portal2→3 → walking downhill inside (129 → −24 cm) → Portal4→1 → eye 152 cm on the plateau |
| Uphill both ways | `-NEPose=0,-800,150,90,0` with the same options | from the top: Portal1→4 → uphill inside → Portal3→2 → eye −46 cm at the foot |

## Scale checks (Phase 6)

`-NEScale=<s>` starts the player at size s (actor scale and movement scaled, as after portals).

| Check (map `L_NE_Scale`) | Command | Result |
| --- | --- | --- |
| Two laps shrink to ¼ (M7) | `-NEPose=-120,450,150,-90,0 "-NEScript=158:300:0;30:0:180;48:300:0;30:0:180;296:300:0;30:0:180;48:300:0;30:0:180;316:300:0" -NESequenceEvery=10` | Portal4→3 ×0.5 each lap: scale 1 → 0.5 → 0.25, eye 150 → 75 → 37.5 cm |
| Only fits at ¼ | `-NEScale=<s> -NEPose=100,-420,<150*s>,180,0 -NEForward=1 -NEMotionFrames=420 -NEUseInput -benchmark -fps=60` | s=1 and s=0.5 stop at the tiny tunnel's mouth (too wide / too tall); s=0.25 walks through |
| Other way grows | `-NEScale=0.5 -NEPose=-120,-350,75,90,0 -NEForward=300 -NEMotionFrames=200` | Portal3→4 ×2: back to exactly 1.0 |

## Showcase and profiling (Phase 7)

Keys 1–7 (or `NE.Demo <n>`) switch demos; standalone play starts at `L_NE_Tunnels`. `-NEProfile` logs
frame / game / render / GPU times and captures (mean, p95, max) over a motion, plus every frame over
16.7 ms. Profile at 1080p with `RESX=1920 RESY=1080 Tools/NonEuclidean/shot.sh <map> <name> -NEProfile
"-ExecCmds=r.VSync 0, t.MaxFPS 0" <motion options from the checks above>`. Results and the comparison
sheet: `Docs/Results_Non-Euclidean_worlds_in_Unreal_Engine_5_8.md`.

## PortalWorlds plugin

The portal system now lives in the `PortalWorlds` plugin (`Plugins/PortalWorlds/`, see its README);
this project uses it like any game would. Traversals log to `LogPortalWorlds` ("Traversed A -> B").
`move_portal_assets_to_plugin.py` was the one-off migration of the portal assets into the plugin.
Build a shareable, prebuilt copy (into `Saved/PluginPackage/PortalWorlds`) with:

```sh
powershell -ExecutionPolicy Bypass -File Tools/NonEuclidean/package_plugin.ps1
```

It runs `RunUAT BuildPlugin`, then restores `"EnabledByDefault": false`. BuildPlugin removes it, and
Blueprint-only projects need it to package the plugin into their game. It also removes the
`HostProject` scratch folder. The Blueprint-only project test is in `bp_test/`.

Re-checked after the move (all as before): identity portal invisible (max 1/255), corridor recursion
(3 captures at depth 4), Level1 vs video (5.0/255 mean), both tunnels, the 6-room loop, the pillar
ring, the slope (ends on top), the scale laps (0.25).

The plugin's materials (`M_Portal`, `M_PortalMirrorView`) are built by `build_plugin_materials.py`:

```sh
"$UE/UnrealEditor-Cmd.exe" "$P/NonEuclideanTest.uproject" -run=pythonscript -script="$P/Tools/NonEuclidean/build_plugin_materials.py" -unattended
```

Both are loaded when the engine starts (from C++ constructors), so their nodes can't be deleted then.
The script edits `M_Portal` in place. It only refreshes `M_PortalMirrorView` if it already exists;
delete that `.uasset` with the editor closed to rebuild it from scratch.

## Mirror tunnel checks (`L_NE_Mirror`)

Built by `ue_build_content.py mirror` (also part of the `maps` stage). Poses are in world A; the
tunnel runs along yaw −126.87°, through the origin.

| Check | Command | Result |
| --- | --- | --- |
| The view through the portal is the reflected world | `mirror_check.py`, below | inside the portal 0.17/255 mean, 0.01% of pixels off by > 16 (antialiased outline); outside 0.00–0.15/255 |
| Walking through | `-NEPose=180,240,150,-126.87,0 -NEForward=290 -NEMotionFrames=150 -NESequence` | "Portal1 -> Portal2 (now mirrored)"; the change spikes (63, 64, 124, 131–133) are the same as for the walk with `Portal.Hide 1` |
| There and back | `"-NEScript=80:290:0;60:0:180;80:290:0;60:0:180;80:290:0" -NESequence`, same pose | now mirrored, no longer mirrored, now mirrored; no broken frames (the shader is compiled cold) |
| The non-orientable loop | `"-NEScript=186:290:0;30:0:180;52:290:0;30:0:180;248:290:0;30:0:180;52:290:0;30:0:180;186:290:0"` | through, around the tunnel, through again: mirrored, then no longer mirrored. Stopping the last leg after 63 frames (back on the start spot, still mirrored) gives exactly the flipped start view: 0.15/255 against the start pose shot one 4.83 cm step further on (the `pawn=` log line is a frame behind) |
| Strafe while mirrored | `-NEPose=480,640,150,-126.87,0 -NEUseInput -NEStrafe=1 -NEMotionFrames=60 [-NEMirrored]` | normal: moves along the camera's right (+0.8, −0.6); mirrored: along (−0.8, +0.6), the right of the flipped view |
| Turn while mirrored | same pose, `-NEUseInput -NESpin=90 -NEMotionFrames=60 [-NEMirrored]` | normal: yaw +88.5°; mirrored: −88.5°, so the flipped view turns right |
| The idle flip pass changes nothing | the start pose, before and after the pass was made permanent | 35 pixels differ, the same as two runs of one build |

`mirror_check.py` predicts a mirroring portal's image independently: the reflected world seen from a
camera is the left-right flip of the normal world seen from the reflected camera. For a pose, take
the shot, the same pose with `-ExecCmds="Portal.Hide 1"`, the reflected pose with portals hidden, and
the pose with `-ExecCmds="Portal.Debug 1"` (the portal mask):

```sh
python -I Tools/NonEuclidean/mirror_check.py shot.png hidden.png hidden_reflected.png pink.png
```

Poses used: `90,120,150,-126.87,0` (on the mirror plane, so it reflects to itself) and
`174,182,150,-111.87,-5`, reflected `126,218,150,-141.87,-5`. `-NEMirrored` starts the player
mirrored. With `-NEUseInput`, `-NESpin` turns through `ProcessViewRotation`, the mouse's path.

Found and fixed along the way: the first frame that used the flip pass in a session showed a blurred
brown border. In a world with mirroring portals the pass now runs from the first frame (unflipped
until needed). In editor builds the subsystem also waits for the material's shaders at begin play.
A Windows cook (564 packages, 0 errors) compiles `M_PortalMirrorView`'s game shaders, so it's
packaged through the subsystem's constructor reference.
