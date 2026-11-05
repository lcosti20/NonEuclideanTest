# Plan: Non-Euclidean Worlds in Unreal Engine 5.8

Inputs: [video play-by-play](./Video_play-by-play_Non-Euclidean_Worlds_Engine.md), [strategy](./Strategy_Non-Euclidean_worlds_in_Unreal_Engine_5_8.md), the video file (sampled frame-by-frame), the narration transcript, the original source ([HackerPoet/NonEuclidean](https://github.com/HackerPoet/NonEuclidean), MIT licence, © 2018 CodeParade) and this repo (UE 5.8 first-person C++ template).

## 0. Scope (decided)

| Decision | Choice |
| --- | --- |
| Platform | **Desktop only.** VR (M9, the 20×20 ft floor-plan demo) is out of scope |
| Look | **Match the video**: the original's unlit checker textures, flat sky, no shadows/GI |
| Goal | **Video parity**: reproduce each demo as closely as possible, including recursive portals |

## 1. Verdict: doable

Everything in scope can be built in UE 5.8 **without modifying the engine**, in C++ in this project's module.

The original engine is closer to Unreal's Scene Capture approach than the strategy assumed. It doesn't use stencil buffers. Each portal **renders the scene into an off-screen texture** from a warped camera, recursively, up to 4 levels deep, and then draws that texture on the portal surface using screen-space coordinates. Unreal's `USceneCaptureComponent2D` + render target does the same job, so the strategy's Tier 1 approach reproduces the original's design rather than approximating it. Tier 3 (engine-level portals) is not needed.

| Demo (transcript) | Original level | Feasibility |
| --- | --- | --- |
| Long tunnel that's short inside; short tunnel that's long (M1, M2) | `Level1` | Easy once portals work |
| 4-room house with only 3 rooms (M3) | `Level2` (3 rooms) | Easy |
| House that has 6 rooms (M4) | `Level2` (6 rooms) | Easy |
| Walk around the pillar → another room; "you can see both objects at once" (M5) | `Level3` | Easy–medium (Phase 4 found it needs no recursion) |
| Up the hill by going down the tunnel (M6) | `Level4` | Easy |
| Tunnel that shrinks you each lap until you fit the tiny tunnel (M7) | `Level5` | Medium; scale compounds each lap |
| VR floor plan (M9) | `Level6` | Out of scope |

## 2. What the original engine actually does

From the source code (`Portal.cpp`, `Engine.cpp`, `Camera.cpp`, `Physical.cpp`, `GameHeader.h`, `LevelN.cpp`):

| Aspect | Original implementation |
| --- | --- |
| Portal shape | A flat, **double-sided quad** (`double_quad.obj`), sized to fill a door or tunnel mouth |
| Portal sides | Each portal has a **front** warp and a **back** warp, which can lead to *different* portals. The pillar room relies on this: `p1.front→p2.back`, `p2.front→p3.back`, `p3.front→p1.back` |
| Warp | `delta = A.LocalToWorld * B.WorldToLocal`. Scale is part of the transform: a portal quad half the size of its partner makes a ×0.5 scale change |
| Rendering | `Render(cam)`: draw sky, draw all objects, then for each portal: build a camera warped through it, add an **oblique near-plane clip** at the portal plane, recursively `Render` into that recursion level's framebuffer, then draw the portal quad sampling that framebuffer at **screen-space UV** |
| Recursion | `GH_MAX_RECURSION = 4`. At the limit, the portal is drawn with a flat **pink** shader. This is the magenta seen in the video's explanation |
| "Conditionally depending on if it's occluded" | Hardware **occlusion queries**: portals are drawn invisibly first, and only portals with visible pixels recurse |
| Near plane | **Dynamic**: `near = clamp(distance to nearest portal × 0.5, 1 mm, 10 cm)`, plus a small extra clip offset |
| Teleport | Segment test (last position → this position) against the portal rectangle, with a small **bump** (2 mm × player scale) toward the side the player came from. On crossing: transform position, velocity and yaw, and multiply player scale by the warp's scale |
| Separate "spaces" | **Not overlapping**: alternative rooms are simply built 200 m apart (house 2 at x = 200; pillar rooms at x = 0/200/400). The far plane is 100 m, so they never see each other |
| Player | Height 1.5 m, radius 0.2 m, walk 2.9 m/s, vertical FOV 60°, gravity −9.8, head bob |
| Assets | The repo contains every mesh (`tunnel*.obj`, `square_rooms.obj`, `pillar_room.obj`, `pillar.obj`, `ground*.obj`, `suzanne/bunny/teapot.obj`) and texture (`checker_gray/green.bmp`, `three_room*.bmp`, `gold.bmp`) |

### Corrections to my earlier analysis

- **The play-by-play's timeline is broadly correct.** I previously said it was wrong after about 2:30. That came from a miscount of my frame sheets. The corrected timeline is in section 3.
- **Portals are flat double-sided quads, not boxes.** The magenta in the video (≈3:30–3:50) is the portal surfaces drawn with the pink shader, not box volumes. The original handles the near-plane problem with a dynamic near plane and the teleport bump.
- **Chunks don't need to overlap.** Building alternative rooms far apart removes the need for per-chunk collision channels.

## 3. Video timeline (corrected; matches the transcript)

| Time | Content |
| --- | --- |
| 0:00–0:10 | Intro |
| 0:10–0:42 | Long tunnel short inside; short tunnel long inside |
| 0:42–1:35 | House: 4 rooms that are really 3 (red, green, blue); then the 6-room house (adds a purple room) |
| 1:35–2:10 | Pillar room: teapot → bunny → Suzanne. A frame scan found no frame with two statues; "both objects" is most likely the pillar and the statue |
| 2:10–2:40 | Sloped hill; going down the tunnel lands you on top |
| 2:40–3:15 | Scale tunnel: each lap halves the player until they fit the tiny tunnel; reverse to grow |
| 3:15–3:30 | Escher, *Portal* comparison |
| 3:30–3:55 | Explanation with portal surfaces drawn magenta |
| 3:55–4:10 | Why not Unity |
| 4:10–5:14 | VR use case (out of scope) |

## 4. Mapping to Unreal

### 4.1 Approach

Port the original's design almost one-to-one:

| Original | Unreal 5.8 |
| --- | --- |
| Off-screen framebuffer per recursion level | `UTextureRenderTarget2D` (`RTF_RGBA16f`) per **recursion-tree node**, pooled (see 4.3) |
| `Render(portalCam)` | `USceneCaptureComponent2D::CaptureScene()`, `bCaptureEveryFrame = false`, `SCS_SceneColorHDR` |
| Oblique clip | Capture `bEnableClipPlane` + `ClipPlaneBase/Normal` (needs `r.AllowGlobalClipPlane=1`) |
| Portal shader (screen-space UV) | Unlit material sampling a texture parameter at `ScreenPosition.ViewportUV`; `Pink` switch parameter |
| Occlusion queries | CPU culling: portal faces the viewer, lies in the view frustum, and its screen rectangle overlaps the parent portal's screen rectangle (see 4.3) |
| Dynamic near plane | Near clip set every frame from the distance to the nearest portal × player scale. `UCameraComponent` has no near-clip override in 5.8, so the main view uses the global near plane (`r.SetNearClipPlane` / `GNearClippingPlane`); captures use `CustomNearClippingPlane` |
| Spaces 200 m apart | Same idea, with 500 m spacing. Each space is tagged with a **space id**; views only render their own space (`ShowOnlyActors`), so distant spaces never show on the horizon |
| Player teleport | `UPortalTraversalComponent` on the character; same segment + bump test on the **camera** position |

Rendering settings for the whole project (to match the video and keep captures cheap): unlit materials; Lumen, shadows, fog, bloom, motion blur, auto-exposure and TSR/TAA **off** (`r.AntiAliasingMethod=0` is already set); manual exposure; the camera cut flag set on every teleport.

### 4.2 Classes (now in the `PortalWorlds` plugin: `Plugins/PortalWorlds/`)

| Class | Responsibility |
| --- | --- |
| `APortal` | Double-sided quad mesh with a dynamic material instance; `FrontLink`/`BackLink` (each = target portal + target side); `SpaceId`; `GetWarp(Side)` returns `FTransform` including scale |
| `UPortalSubsystem` (`UWorldSubsystem`) | Portal registry; builds the recursion tree each frame; owns the render-target pool and a pool of capture components; issues captures; console vars `Portal.MaxDepth` (default 4), `Portal.Debug` (draw all portals pink, like the video) |
| `ANonEuclideanTestCameraManager` (exists) | Override `UpdateCamera()`: after `Super`, call `UPortalSubsystem::Render(FinalPOV)`, so captures always use this frame's camera (no lag) |
| `UPortalTraversalComponent` | Crossing test, teleport (location, control rotation, velocity), player scale, camera cut, current `SpaceId` for the main view |
| `ANonEuclideanTestCharacter` (exists) | Tune to the original: capsule radius 20 cm, eye height 150 cm, walk 290 cm/s, horizontal FOV ≈ 91.5° at 16:9 (= 60° vertical); optional head bob |

### 4.3 Recursion

The main view is the root. For a view `V` at depth `d` (main view: `d = 0`):

1. For each portal `P` in `V`'s space: cull if it's back-facing for both sides, outside `V`'s frustum, or (for `d > 0`) its screen rectangle doesn't overlap the parent portal's rectangle. This plays the role of the original's occlusion test.
2. If `d + 1 = MaxDepth`: `P` is drawn pink in `V`.
3. Otherwise create child node `C`: camera = `V`'s camera warped through `P`'s visible side, clip plane at the exit portal, space = the exit portal's space. Recurse into `C`.

Then capture **deepest nodes first** (post-order), so every capture finds its children's textures already rendered. Before each capture, point each portal's material at the render target of the child node for *this* view (or switch it to pink). Finally, set the main view's portal materials to the depth-1 targets.

Each node needs its own render target, because a parent is captured *after* all of its children. Targets are pooled by node index, so allocation happens once. The same portal can appear in several views with different contents, so the material parameter is swapped between captures. Game-thread parameter updates and `CaptureScene()` calls are queued to the render thread in order, so this should work, but **it's the main technical unknown and Phase 1 spikes it first**. Fallback: give each portal a small pool of proxy quads and show one per node via per-capture `ShowOnly` lists.

Render targets use the viewport's aspect ratio (the screen-space UV mapping assumes the child projection matches the parent's), at full size for depth 1 and half per extra level. Expected load in the video's levels: about 2–8 captures per frame, and cheap ones because the scenes are unlit and tiny. The cap and culling keep the worst case bounded.

### 4.4 Assets

Import the original meshes and textures from the repo (MIT; keep the licence notice in `Content/NonEuclidean/THIRD_PARTY.md`):

- The `.obj` files use a custom dialect, so they're converted to `.glb` first, then imported; Nanite off, complex-as-simple double-sided collision. Pipeline and axis conversion: `Tools/NonEuclidean/README.md`. *(Done in Phase 0.)*
- Use the textures with an unlit `M_Textured` material and nearest filtering, as in the original.
- Sky: an unlit gradient sky sphere, included in every space.

Suzanne, the Stanford bunny and the Utah teapot are third-party models redistributed in the repo. That's fine for an internal test; check their terms before shipping anything.

## 5. Phased plan

Each phase ends with a pass/fail check against the video.

### Phase 0: Setup and look

- Import the meshes and textures; create `M_Textured`, `M_Portal` and the sky.
- Project settings: `r.AllowGlobalClipPlane=1`, the rendering features listed in 4.1 turned off, manual exposure.
- Tune the character to the original's dimensions and FOV.
- **Done when:** a static screenshot of the tunnel scene lines up with the 0:15 frame of the video.

### Phase 1: One portal + recursion spike

- `APortal`, `UPortalSubsystem` with a single depth level, camera-manager hook, clip plane, `Portal.Debug` pink mode.
- **Spike:** two portals facing each other at depth 4, with the material parameter swapped between captures. Confirm that each level shows the right image (no flicker, no image from the wrong level).
- Dynamic near plane.
- **Done when:** the portal edge is invisible while strafing and turning fast, and the spike passes (otherwise switch to the proxy-quad fallback now).

### Phase 2: Traversal + tunnels (Level1: M1, M2)

- `UPortalTraversalComponent` with the bump, transformed rotation and velocity, camera cut.
- Map `L_Tunnels`: the original `Level1` layout (normal tunnel, and a 0.6-length tunnel with crossed portals).
- **Done when:** walking or backing through either tunnel shows no pop, and turning around inside looks right.

### Phase 3: Houses (Level2: M3, M4)

- Space ids and per-view `ShowOnly` lists.
- Maps `L_House3` (one house; door 3 linked to door 4) and `L_House6` (a second house 500 m away, with `three_room2` texture).
- **Done when:** both houses can be walked in loops with no visible seams, as in the video.

### Phase 4: Pillar room (Level3: M5)

- Front/back links (done in Phase 1); spaces (done in Phase 3).
- Map `L_NE_Pillar`: three pillar rooms 200 m apart, as in the original (teapot, bunny, Suzanne), linked in a ring.
- **Done when:** walking round the pillar cycles teapot → bunny → Suzanne both clockwise and counter-clockwise, with no pop and no pink.
- *Finding:* the portal sits in the gap behind the pillar, and the pillar always hides its seam, so laps need at most one capture per frame. The video never shows two statues in one frame (checked frame by frame), so the "both objects at once" line doesn't call for recursion. Recursion (M8) is covered by the Phase 1 corridor test and Level1.

### Phase 5: Slope (Level4: M6)

- Map `L_Slope`: sloped ground, tunnel exit portal lower than the entrance, mirrored copy 500 m away.
- **Done when:** walking down the tunnel puts you on top of the hill.

### Phase 6: Scale (Level5: M7)

- Scale in the warp; on crossing, multiply actor scale, `MaxWalkSpeed`, `JumpZVelocity`, `GravityScale`, `MaxStepHeight` and the near-plane limits.
- Map `L_Scale`: the scale tunnel (half-size exit) looping with a normal tunnel, plus the tiny 0.25-scale tunnel.
- **Done when:** after two laps the player fits through the tiny tunnel; reversing grows the player back, exactly to the original scale; movement feels the same at every scale.

### Phase 7: Showcase and comparison

- `L_Showcase` or a level switcher on keys 1–6, like the original.
- Record each demo next to the matching video segment, and profile (`stat unit`, `stat gpu`, `stat scenerendering`).
- **Done when:** every demo matches the video, at 60+ fps at 1080p on the dev machine.

## 6. Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Swapping a portal's texture between captures doesn't sequence correctly | Spiked in Phase 1; fallback is per-node proxy quads |
| Cost of many captures | Unlit tiny scenes; screen-rect culling; depth cap 4; half resolution per level |
| Near plane at small player scale | Dynamic near plane, as in the original; verify the per-camera override in Phase 1 |
| One-frame lag at portal edges | Captures driven from `UpdateCamera()`, never from actor tick |
| Character movement at 1/4 scale | Scale every movement parameter together; test in Phase 6 |
| Brightness mismatch through portals | HDR capture + manual exposure; unlit materials |
