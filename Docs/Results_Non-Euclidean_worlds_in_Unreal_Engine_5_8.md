# Results: Non-Euclidean Worlds in Unreal Engine 5.8

Outcome of the test planned in [Plan_Non-Euclidean_worlds_in_Unreal_Engine_5_8.md](./Plan_Non-Euclidean_worlds_in_Unreal_Engine_5_8.md).

## Verdict

**It works.** Every demo in CodeParade's "Non-Euclidean Worlds Engine" video (the VR part was out of scope) runs in Unreal Engine 5.8:
- It uses the original's meshes, textures and level layouts.
- It needs no engine modifications.
- It runs at several hundred fps at 1080p.

The portal technique is a direct port of the original engine: recursive render-to-texture, the same portal links and warps, and the same teleport rule. It uses scene captures in place of OpenGL framebuffers.

![Unreal vs video](./images/comparison_sheet.png)

## Demos

Keys **1–6** switch demos in the original's order, and key **7** opens the mirror tunnel, which isn't in the video (also the console command `NE.Demo <n>`). Playing in standalone starts at demo 1.

| Key | Map | Video | Result |
| --- | --- | --- | --- |
| 1 | `L_NE_Tunnels` | 0:10 | The long tunnel is 1.2 m inside; the short tunnel is 9.6 m inside (M1, M2) |
| 2 | `L_NE_House3` | 0:42 | The four-room house has three rooms: the loop goes blue → red → green → blue, and grey never appears (M3) |
| 3 | `L_NE_House6` | 1:20 | The same footprint holds six rooms: blue → red → green → grey → purple → orange → blue (M4) |
| 4 | `L_NE_Pillar` | 1:35 | Each lap round the pillar moves to the next room, both directions (M5) |
| 5 | `L_NE_Slope` | 2:10 | Walking down the tunnel brings you out on top of the hill, and uphill inside it on the way back (M6) |
| 6 | `L_NE_Scale` | 2:40 | Each lap halves you; the tiny tunnel only fits at ¼ size; the other way grows you back (M7) |
| 7 | `L_NE_Mirror` | — | Not in the video: the portal in the tunnel mirrors the world. After one pass every landmark is on the other side; walk round and through again and it's back. See [Mirror worlds](#mirror-worlds-beyond-the-video) |

The recursion works too (M8): up to `Portal.MaxDepth` 4, ending in pink as in the original. Two frames are matched against the video at known recursion depths (3:51 and 3:52.8).

## How close to the video

- **Look:**
  - Surface colours match the original shader formulas to within 1/255.
  - From a camera pose fitted to the video frame at 3:48 (0.9 px RMS), our render differs from the video by 4.6/255 on average, with 97.9% overlap of the portal surfaces.
  - The other demos match visually; see the sheet above.
- **Behaviour:**
  - Every traversal was checked with scripted walks: the expected portal, position, height and scale.
  - No visual pop on the crossing frame.
  - No portal lag while turning at 730°/s.

## Performance (1920×1080, RTX 5070, Ryzen 9 7900X, editor binary in `-game` mode)

| Demo | Mean frame | p95 | GPU mean | Portal captures (mean / max) |
| --- | --- | --- | --- | --- |
| 1 Tunnels | 2.94 ms | 5.13 ms | 0.93 ms | 2.3 / 6 |
| 2 House (3) | 1.79 ms | 2.70 ms | 0.47 ms | 0.7 / 2 |
| 3 House (6) | 1.76 ms | 3.21 ms | 0.46 ms | 0.7 / 3 |
| 4 Pillar | 1.63 ms | 2.28 ms | 0.40 ms | 0.4 / 1 |
| 5 Slope | 2.35 ms | 3.63 ms | 0.64 ms | 1.3 / 3 |
| 6 Scale | 2.10 ms | 3.54 ms | 0.60 ms | 1.1 / 3 |

- **Budget:** the worst p95 is about 195 fps, against a target of 60. The bottleneck is the render thread (CPU), not the GPU.
- **Hitches:** some runs had 3–4 frames of about 30 ms. They also happen with portals hidden, so they come from the engine or OS, not from the portals. Frame pacing should be confirmed in a packaged build.

## Mirror worlds (beyond the video)

The plugin also supports portals that flip chirality: one of two linked portals has a negative X scale, and the warp becomes a reflection. A camera can't render mirrored, so each portal view is rendered with the matching unmirrored camera and its image is flipped left-right. While the player is mirrored, their own view is flipped by a post-process pass, and mouse yaw and strafing are reversed so the controls feel unchanged. Details are in the plugin README.

`L_NE_Mirror` has two copies of a scene, linked by a portal in the middle of a tunnel. The tunnel runs along the sun's direction, so the shading and the sky are symmetric about the mirror plane and the seam doesn't show. Checks (commands in `Tools/NonEuclidean/README.md`):
- **Through the portal = the reflected world.** The view through the portal was compared with an independent prediction: the left-right flip of the normal world seen from the reflected camera pose. Inside the portal the mean difference is 0.17/255, at a centred pose and at an off-centre, turned, tilted one; outside it, 0.00–0.15/255.
- **Walking through:** "Portal1 -> Portal2 (now mirrored)", with no change spike beyond what the same walk shows without portals.
- **The loop:** through, around the outside of the tunnel, and through again: mirrored, then "no longer mirrored". Halfway round, on the same spot as the start, the view is the exact left-right flip of the start view (0.15/255, the run-to-run noise level).
- **Controls while mirrored:** strafe right moves to the right on screen, and turning right turns the view right.

## What differs from the original, and why

- **Spaces replace the far plane.** Alternative rooms are built far apart, as in the original. The original hid them with a 100 m far plane; here, actors are tagged `PortalSpace.<n>` and each view renders only its own space.
- **Visibility is tested on the CPU.** The original used GPU occlusion queries. This build tests facing and screen rectangles instead, so a portal hidden behind geometry is still rendered (Tunnels: up to 6 captures where fewer are needed). It's correct, just not minimal. Hardware occlusion queries are off project-wide, because stale per-view results would make geometry pop in captures.
- **The pillar room needs no recursion.** The video never shows two statues at once (checked frame by frame). "Both objects" refers to the pillar and the statue.
- **Small players hover slightly high.** Character movement keeps the capsule about 2 cm above the floor at any size, so at ¼ scale the eye is 39.8 cm instead of 37.5 cm.
- **Project-wide rendering changes.** Lumen, bloom, AO and auto-exposure are off for the original's unlit look, and the template maps are affected too. `r.AllowGlobalClipPlane` is on.

## Where things are

| What | Where |
| --- | --- |
| Portal rendering, traversal, spaces | The **PortalWorlds** plugin, `Plugins/PortalWorlds/` (`APortal`, `UPortalSubsystem`, `UPortalTraversalComponent`, `APortalPlayerCameraManager`); see its `README.md` |
| Player, game mode, test harness | `Source/NonEuclideanTest/NonEuclidean/` |
| Asset pipeline, map builder, every check with its command | `Tools/NonEuclidean/` and its `README.md` |
| Original assets and licence (MIT, © 2018 CodeParade) | `SourceArt/NonEuclidean/Original/` |

## Next steps, if this goes further

1. **GPU occlusion:** use the previous frame's per-portal visibility, or a custom occlusion pass, to stop rendering hidden portals.
2. **Packaged build:** test frame pacing outside the editor.
3. **Objects through portals:** physics props and a visible player body crossing portals (not in the video).
4. **VR:** the strategy's engine-level portal path, measured on a prototype first.
