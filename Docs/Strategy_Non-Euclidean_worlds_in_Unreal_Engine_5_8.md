# Strategy: Non-Euclidean Worlds in Unreal Engine 5.8

Based on the [CodeParade "Non-Euclidean Worlds Engine" video play-by-play](./Video_play-by-play_Non-Euclidean_Worlds_Engine.md).

The core idea from the video is to separate geometry, traversal, and rendering, so that none of them has to describe one consistent Euclidean space. Unreal's architecture suits this well. The engine has no built-in portal system, so you build the portal layer yourself. Unreal 5.8 is mostly a refinement release, so none of the work below depends on a 5.8-specific feature.

## 1. Core model: everything is a portal pair with a transform

Almost every mechanic in the video (M1, M2, M5, M6, M7, M8) reduces to one data structure: two portals, A and B, joined by a 4x4 transform T that maps points in A's space to B's space.

- **Rigid transform only:** an ordinary portal, as in the game Portal. It covers teleport, elevation inversion (M6), and column looping (M5).
- **Transform with uniform scale:** the shrinking and growing tunnel (M7).
- **Distance compression and expansion (M1, M2):** these need no special code. Place portal B far from the apparent end of the tunnel, or right next to it. The "tunnel" is just two portal frames with the right placement. A long-looking tunnel is a short walk if its two openings sit close together in world space.

Build a `UPortalComponent` or `APortal` actor holding a pointer to its linked partner. It computes `T = B_world * Flip * inverse(A_world)` (and a scale factor for M7). Everything else uses that matrix.

## 2. Topology: let spaces overlap

The missing and extra rooms (M3, M4) and the looping column (M5) come from rooms that are not in one coordinate system. In practice:

- Author each "impossible" region as its own chunk, placed in a separate area of the level or in a separate sublevel or World Partition cell. Rooms may physically overlap in world space as long as they are never rendered or collided together.
- Connect chunks only through portals. A doorway that "should" lead to the missing room just links to a different chunk.
- For the column, put a portal in the gap between the column and the viewer's path, so that completing a lap lands the player in a different chunk. Use several linked chunks for the "multiple configurations at once" effect.

If chunks do overlap, assign each a collision and visibility layer (see section 4) so they don't interact.

## 3. Rendering: three tiers, built in this order

### Tier 1: Scene Capture portals (prototype and most of the project)

- Each portal has a `USceneCaptureComponent2D` and a render target.
- Every frame, place the capture camera at `T * playerCamera`, using the partner's transform.
- The portal surface material samples the render target using screen-space UVs.
- Use an oblique near-plane clip so geometry behind the exit portal doesn't appear in the view. Set the capture's custom projection matrix, or its clip plane, to the exit portal plane.
- Update the capture after the player camera moves, using a late tick or the camera manager's post-update, to avoid one-frame lag.
- Scale (M7) comes free, because the capture camera's transform carries the scale. Remember that FOV and near-plane distances may need adjusting.

### Tier 2: Recursion (M8)

- Real recursion is the hard part, as the video says. With scene captures, you have to render portals-in-portals yourself.
- Start with a fixed depth budget of 2 or 3. For a portal seen through a portal, render only the portal's own capture at reduced resolution.
- At the depth limit, show a flat color or a cheap cached image.
- A common trick is to feed the previous frame's render target into the next portal level, which gives infinite-looking recursion at a cost of one frame of latency per level.

### Tier 3: Engine-level portals (only if Tier 1 hits a wall)

- The video's engine renders portals inside a single scene pass using stencil masking. The Unreal equivalent is a source-build engine modification or a custom `SceneViewExtension` that re-renders the scene per portal inside one view family.
- This is a large commitment, so take it only if you need pixel-exact recursion, correct per-view lighting and post-processing, or VR performance.
- A reasonable halfway house is a `SceneViewExtension` that injects a custom pass drawing the portal interiors with stencil, without modifying the whole renderer.

## 4. Traversal and gameplay

- **Crossing detection:** each frame, check whether the player's camera or capsule center crossed the portal plane in the A-to-B direction. If so, apply T to the capsule, the controller rotation, and the velocity.
- **Scale change (M7):** scale the capsule, movement speed, jump height, gravity, and step height by the same factor. Physics tuned for one scale breaks quickly at another, so test extremes early.
- **Objects straddling a portal:** when a physics object or character is half through, spawn a proxy clone on the other side that mirrors the original's transform through T. Swap ownership at the plane. This is also required for the player's own body.
- **Collision isolation:** give each chunk its own object channel or collision profile so overlapping rooms never collide. Portals should also filter out the wall surface behind them.
- **Camera near plane:** the player's camera can poke through a portal plane before the capsule does. Make the portal trigger volume slightly thick, or move the player's pivot early, to avoid seeing behind the wall.

## 5. Unreal-specific performance considerations

- Each scene capture is effectively another full scene render. Lumen, Nanite, and virtual shadow maps all run for it, so cost grows quickly.
- Disable or reduce expensive features on captures with show flags and the capture's own post-process settings. Skip fog, motion blur, and screen-space effects where possible.
- Lower capture resolution as depth rises, and only render captures for portals that are on screen and within range. Use frustum and distance checks, plus a portal visibility test.
- Lumen Lite, which the 5.8 release notes describe as a faster medium-quality GI mode, and the now production-ready MegaLights could both help your frame budget. Verify them against your target hardware before relying on them.
- Lighting is per capture, so it can mismatch at the portal seam. Keep ambient lighting and exposure consistent between linked chunks. Lock the exposure to manual to avoid auto-exposure pops at the portal.
- Consider baked or simpler lighting in "impossible" chunks if Lumen cost becomes the bottleneck.

## 6. VR extension (M9)

The video's final idea is fitting a large virtual space into a small tracked area. In Unreal VR:

- Use portal-linked chunks laid out so that physical walking maps to a larger virtual route (the column-loop trick, applied at room scale).
- Adjust for stereo: each eye needs its own capture, or a stereo-aware pass, so Tier 1 is expensive in VR. Tier 3 becomes much more attractive here.
- Scale transforms in VR change the effective IPD relative to the world, so decide deliberately whether to scale it.

## 7. Suggested roadmap

1. **Sprint 1:** one portal pair with a Scene Capture, oblique clipping, and crossing detection. This covers M1, M2, and M6.
2. **Sprint 2:** add the player body proxy, physics-object cloning, and collision layers. Add the scale transform (M7).
3. **Sprint 3:** build the topology tests. Make the missing-room and extra-room layouts (M3, M4) and the looping column (M5) from overlapping chunks.
4. **Sprint 4:** add recursion with depth budgeting (M8), plus profiling and capture-cost reduction.
5. **Later:** decide whether to invest in the engine-level or `SceneViewExtension` path, and add VR support (M9).

## 8. Main risks

- **Render cost** is the biggest one. Plan the depth budget and capture resolution before building content.
- **Frame-lag artifacts** at portal edges. Get the update ordering right early.
- **Physics at non-uniform or extreme scale.** Keep scale factors modest at first.
- **Level-authoring complexity.** Overlapping chunks are confusing in the editor. Build editor visualization tools (color-coded chunks, portal link lines) early.
- **Source of truth:** the source document is a secondhand reconstruction of the video, and the original C++/OpenGL code (HackerPoet's NonEuclidean repository on GitHub) is the best reference for the stencil-based approach. Read it before attempting Tier 3.
