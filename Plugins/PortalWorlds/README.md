# Portal Worlds (Unreal Engine 5.8)

Seamless, recursive portals for non-Euclidean spaces: tunnels longer inside than outside, houses with missing or extra rooms, loops around a pillar, slopes that lead the "wrong" way, tunnels that change your size, and portals into a mirror world. It's a port of the technique in CodeParade's [NonEuclidean](https://github.com/HackerPoet/NonEuclidean) engine (see `THIRD_PARTY_NOTICES.md`).

- **Rendering:** portals render the scene through other portals, up to a set depth, using scene captures. The edge of a portal is invisible and there's no frame lag.
- **Walking through:** the player passes through portals with no visible jump. A portal of a different size scales the player, and a mirroring portal flips the world left-right.
- **Objects:** physics objects pass through too, thrown or carried. A scaling portal resizes them and a mirroring portal flips their handedness.
- **Spaces:** keep alternative rooms out of sight of each other.

Tested on Windows (D3D12) with UE 5.8 only.

## Install

1. Copy the `PortalWorlds` folder into your project's `Plugins/` folder and enable **Portal Worlds** in Edit > Plugins (or add `{ "Name": "PortalWorlds", "Enabled": true }` to your `.uproject`). It's off by default, so every project has to enable it.
2. In **Project Settings > Rendering**, turn on **Support global clip plane for Planar Reflections** (`r.AllowGlobalClipPlane=True` under `[/Script/Engine.RendererSettings]` in `DefaultEngine.ini`). Restart the editor. Without it, portal views show geometry that's behind the exit portal, and the plugin logs a warning.
3. Recommended: turn off occlusion queries (`r.AllowOcclusionQueries=False`). Portal views can jump between very different viewpoints, and stale occlusion results can make geometry pop for a frame.
4. If your game module calls the plugin's C++ classes, add `"PortalWorlds"` to its `Build.cs` dependencies.

### Blueprint-only projects

Everything works from Blueprint; no C++ project is needed.

- **In the editor:** use a prebuilt copy of the plugin, one that includes `Binaries/`. Make it with the source project's `Tools/NonEuclidean/package_plugin.ps1`. The editor loads it without compiling anything. Without binaries, the editor would have to compile the plugin, which needs Visual Studio.
- **Where to put it:** either the project's `Plugins/` folder, or `Engine/Plugins/Marketplace/PortalWorlds` to share it between projects. Use the script's output rather than raw `RunUAT BuildPlugin`. BuildPlugin drops `"EnabledByDefault": false`, and without that a copy in a project's `Plugins/` folder packages a Blueprint-only game *without* the plugin's code ("module 'PortalWorlds' could not be found" at runtime).
- **Packaging:** packaging a Blueprint-only project that uses any code plugin (Fab plugins too) needs **Visual Studio with the "Game development with C++" workload**. Unreal notices the plugin, generates a temporary C++ target and builds a game executable containing it. Your project stays Blueprint-only. Check the packaging log for "being treated as a code-based project ... because: PortalWorlds plugin is enabled".

## Set up the player

- **Camera manager:** set your player controller's **Player Camera Manager Class** to `PortalPlayerCameraManager`, or derive yours from it. If you already have a camera manager, add this after `Super::UpdateCamera(DeltaTime)` in its `UpdateCamera`:
  ```cpp
  if (UPortalSubsystem* Portals = GetWorld()->GetSubsystem<UPortalSubsystem>())
  {
      Portals->RenderPortals(PCOwner, GetCameraCacheView());
  }
  ```
  This runs after the camera's final position is known, which is what keeps portal views lag-free. For mirroring portals, also add this at the start of its `ProcessViewRotation`, so the mouse turns the right way while mirrored:
  ```cpp
  UPortalSubsystem::MirrorViewRotationInput(PCOwner, OutDeltaRot);
  ```
- **Traversal:** add a **Portal Traversal** component (`UPortalTraversalComponent`) to the player pawn. It finds the pawn's camera automatically; set **Eye** only to track a different component. It ticks after character movement and teleports the pawn when the camera crosses a portal.
- **Carrying objects (optional):** add a **Portal Grab** component (`UPortalGrabComponent`) to the pawn as well, and bind keys to **Toggle Grab** and **Throw**. See [Objects](#objects).

## Place portals

1. Drag **Portal** actors (`APortal`) into the level. The surface is a 2 m × 2 m quad in the actor's local XZ plane; scale the actor to fit a doorway. Its front faces local −Y.
2. Link them. On each portal, **Front Link** is where you go when you enter from the front side and **Back Link** when you enter from the back. To link two portals both ways, set A's two links to B and B's to A (`APortal::Connect(A, B)` in C++).
3. The exit keeps your position relative to the portal. You come out of the target's back side, facing the same way relative to it. Rotate the target 180° to come out facing the other way.
4. **Scale:** if the target portal is smaller or larger (uniform scale), whatever passes through is scaled by the same factor. That includes the character's capsule, camera height, speeds and gravity.
5. **Mirroring:** give one of the two portals a negative X scale (for example −1, 1, 1). See [Mirror worlds](#mirror-worlds).

**Custom surfaces:** you can give a portal any mesh in its local XZ plane. Set **Surface Half Extent** to match it (the default is 100 × 100 cm). A custom material needs:
- a texture parameter `PortalTex`, sampled at `ScreenPosition` → `ViewportUV`;
- a scalar parameter `Pink` (1 = flat colour, shown where recursion stops);
- for mirroring portals, a scalar parameter `MirrorU` that flips the sampling U (`1 − U` when it's 1).

Instancing `/PortalWorlds/Materials/M_Portal` is the easiest way.

## Mirror worlds

A mirroring portal reflects space across its width: everything seen through it is mirrored, and whoever walks through comes out mirrored relative to the world. The next mirroring portal (or the same one, walking back) restores them. Linked to a copy of the same scene, it gives a non-orientable world: walk through, and every landmark is on the other side; walk around and through again, and it's back.

**Setting one up:** two linked portals where one has a negative X scale (−1, 1, 1, or −0.6, 1, 1 to match a 0.6-wide doorway). **Get Is Mirrored Link To** tells you whether a link mirrors. The reflection plane runs through the portal's centre, perpendicular to its width.

**Keeping the seam invisible:** what surrounds the two portals must look the same mirrored, because the image through the portal is the far side mirrored:
- left-right symmetric geometry around the portals, such as a straight tunnel or corridor with the portal across it;
- symmetric textures: a checker that's centred on the portal's centre line, not split by it;
- symmetric lighting: a light whose direction lies in the reflection plane, or lighting that doesn't vary between the left and right walls. With directional lighting, run the tunnel along the light's horizontal direction.

**What the player experiences:** a camera can't render mirrored, so the plugin renders the world normally and flips the image instead:
- The player's view is flipped left-right on screen by a post-process material (`/PortalWorlds/Materials/M_PortalMirrorView`, after tonemapping, so UI and HUD aren't flipped). In a world with any mirroring portal, this pass runs from the start (unflipped until needed), so the renderer's first use of it doesn't show up as a glitch when the player walks through.
- Mouse yaw is reversed (`APortalPlayerCameraManager`), so turning right still turns the view right.
- Movement input is reflected left-right before the movement component uses it (**Mirror Movement Input** on the traversal component), so strafing right moves right on screen.
- **Is Mirrored** on the traversal component tells you the player's state; **Set Mirrored** sets it (for respawns).

**Not handled:**
- **Stereo sound** stays unmirrored: a sound on the player's left on screen is heard on the right. Swap the audio listener's left and right while mirrored if that matters.
- **A visible player body or first-person arms** is flipped along with the world, so a right-handed weapon appears in the left hand. Give the body a negative scale while mirrored to cancel it.
- The flip assumes a centred (symmetric) projection, which the player's camera normally has.

## Objects

Anything that isn't a pawn can pass through portals too: add a **Portal Traversal** component to it. The point that has to cross is the root component's origin, so give the object a centred pivot.

**What changes:** when the object crosses, its whole transform goes through the portal.
- **Size:** a portal of a different size resizes the object.
- **Handedness:** a mirroring portal gives the object a negative scale, so its shape is really mirrored in the world. A left-handed object comes out right-handed. **Is Mirrored** on its traversal component tells you which way round it is.
- **Motion:** a simulating root keeps moving. Linear and angular velocity go through the portal with it, and its mass is recomputed after a size change.
- **Spaces:** an object tagged with a space moves to the exit portal's space.

**Partway through:** while an object straddles a portal, a copy of its static meshes is shown at the other side (**Show Copy While Crossing**, on by default). The copy shows the part that's already through, so the object doesn't vanish into the portal surface. The portal surface hides the part of each that's on the wrong side.

**Rendering mirrored objects:** a mesh decides which faces to cull when its render state is created, from the sign of its scale. It keeps that choice when only its transform changes, so an object mirrored after it first appears would render inside out. Traversal rebuilds the render state whenever an object's handedness changes. If you mirror an actor yourself (e.g. set a negative scale at runtime), call **Refresh Mirrored Rendering** on it afterwards.

**Collision:** give mirrored objects simple collision made of boxes, spheres or convex hulls on a single body. Chaos handles those at a negative scale. Bodies welded from several components don't rescale correctly when their scale changes.

### Carrying objects

The **Portal Grab** component picks up the physics object under the crosshair, carries it with a physics handle and throws it. It works out the hold point in the player's own frame (position, heading, size, mirrored state), so carrying works through portals:
- The object usually reaches the portal first. Its own traversal component sends it through, resized or mirrored, and the hold point follows it to the other side.
- The player then crosses, the two warps cancel, and the object is back in front of them.

So **an object carried through a portal changes together with the player**: carry a key through a mirror and both are mirrored, and to the player it still looks the same. To change an object *relative to the player*, throw it through, or carry it through and walk back round.

Aiming looks through portals too, so an object seen through a portal can be picked up. Objects larger than **Max Object Radius** (relative to the player's size) can't be lifted. A held object ignores collision with pawns. It's dropped if it stays out of reach for half a second, for example caught on a wall.

## Spaces

The alternative versions of a room have to exist somewhere. The usual approach is to build them far apart, for example 200–500 m. To keep each version from showing on the horizon of the others, put each one in a **space**: tag every actor of space *n* (geometry and portals) with `PortalSpace.<n>`. Untagged actors (sky, player) appear in every space.

Each view renders only its own space and looks only through portals in that space. The player's current space is the one whose bounds contain the camera.

A portal belongs to its space too: from any other space it's invisible, and so is everything through it, though it can still be walked through. If you move a portal into another space's area, change its tag to match. The plugin logs a warning (`LogPortalWorlds`) at play for each portal that stands in another space's area but outside its own.

## Blueprint nodes and events

**Portal Traversal component**

| Name | What it does |
| --- | --- |
| **On Portal Traversed** (event) | Called after each trip through a portal, with **From Portal**, **To Portal** and **Scale Factor** (1 for a plain portal) |
| **Get Traversal Count** | Portals passed through since play began |
| **Reset Tracking** | Call after teleporting or respawning the pawn yourself, so the jump doesn't count as walking through a portal |
| **Apply Scale Factor** | Resize the pawn as a scaled portal would |
| **Is Mirrored** | True after an odd number of mirroring portals |
| **Set Mirrored** | Set the mirrored state yourself (e.g. on respawn) |
| **Mirror Movement Input** | While mirrored, reflect movement input left-right (default on) |
| **Eye** | The tracked component (defaults to the camera, or the root for objects) |
| **Show Copy While Crossing** | Objects: show a copy at the far side while partway through a portal (default on) |
| **Refresh Mirrored Rendering** | Rebuild an actor's render state after flipping the sign of its scale yourself, so it doesn't render inside out |

**Portal Grab component**

| Name | What it does |
| --- | --- |
| **Grab** / **Release** / **Toggle Grab** | Pick up the object under the crosshair (also through portals), drop it, or whichever applies |
| **Throw** | Throw the held object forwards at **Throw Speed** |
| **Is Holding**, **Get Held Component** | What's held |
| **Get Aimed Component** | What **Grab** would pick up now (for prompts) |
| **Get Held Relative Scale** | The held object's size relative to the player |
| **Grab Range**, **Hold Gap**, **Max Object Radius**, **Throw Speed**, **Break Distance** | Tuning, in cm at the player's normal size |

**Portal Subsystem** (the **Get PortalSubsystem** node)

| Name | What it does |
| --- | --- |
| **Get Portals** | All portals in play |
| **Get Current Space** | The player's space, or −1 if the map doesn't use spaces |
| **Get Actor Space** | An actor's space from its tag, or −1 if it's in every space |
| **Set Actor Space** | Tag an actor into a space (−1 removes the tag) |
| **Refresh Spaces** | Re-read space tags after spawning or re-tagging actors at runtime |
| **Get Nearest Portal Distance** | Distance from a point to the nearest portal |
| **Get Last Capture Count** | Portal views rendered last frame |
| **Is View Mirrored** | The player's view was shown mirrored last frame |

**Portal actor**

| Name | What it does |
| --- | --- |
| **Front Link**, **Back Link** | Where each side leads (settable at runtime) |
| **Connect** | Link two portals both ways |
| **Get Normal** | The front side's direction |
| **Get Link For** | Which portal a viewer at a given point would see through |
| **Is Mirrored Link To** | True if passing to the given portal mirrors space |
| **Surface Half Extent** | The portal rectangle's size |

## Console variables

| Variable | Default | Effect |
| --- | --- | --- |
| `Portal.MaxDepth` | 4 | Recursion depth (portals seen through 3 levels of portals; deeper ones show `Pink`) |
| `Portal.MaxCaptures` | 32 | Cap on captures per frame |
| `Portal.ResolutionFalloff` | 1.0 | Render-target resolution multiplier per extra depth level |
| `Portal.DynamicNearPlane` | 1 | Pulls the engine's global near plane in (down to 1 mm) near portals, so the camera never clips through one. Set 0 to leave the near plane alone |
| `Portal.Spaces` | 1 | Space filtering (0 = show everything, for debugging) |
| `Portal.Debug` | 0 | Draw all portals flat pink, no captures |
| `Portal.Hide` | 0 | Hide portal surfaces, no captures |
| `Portal.ShowStats` | 0 | On-screen view, capture and near-plane info |

Log category: `LogPortalWorlds` (traversals at Log, render-target creation at Verbose).

## How it works

Every frame, after the camera update, `UPortalSubsystem` builds a tree of views. The player's view is the root. Each portal visible in a view adds a child view: the camera warped through the portal, clipped at the exit portal. Visibility is tested on the CPU (facing, clip plane, overlap with the parent portal's screen rectangle).

The views are captured deepest-first into render targets, using the player's exact projection and post-process settings. Before each capture, every visible portal's material is pointed at that view's child image.

Traversal tests the camera's path each frame against every portal rectangle. The plane is moved 2 mm toward the side the camera came from, so the camera never reaches the surface. On a crossing, location, view rotation, velocity and size are warped to the linked portal.

An object (any owner that isn't a pawn) has its whole transform multiplied by the warp. FTransform stores a negative determinant as a negative X scale. A held object's hold point is the player's frame times a "carry warp", which collects the portals between the player and the object: each trip the object makes is appended, and each trip the player makes is cancelled out. When the object goes through, the physics handle re-grabs it in place. Otherwise the handle's kinematic anchor would sweep across the jump in one physics step and fling the object.

A mirroring warp (negative determinant) can't be applied to a camera, so each view carries a mirrored flag. It's captured with the matching unmirrored camera, whose image is the true view flipped left-right. A portal shows its child view with `MirrorU` = parent flipped XOR child flipped, and the player's own view is flipped by the post-process pass while they're mirrored.

## Limitations

- **Every capture is a full scene render.** Keep portal scenes light, or lower `Portal.MaxDepth` / `Portal.ResolutionFalloff`.
- **Portals hidden behind geometry still get captured,** because visibility is tested on the CPU rather than with GPU occlusion queries.
- **The look was verified with unlit, fixed-exposure scenes.** With Lumen, auto-exposure or temporal anti-aliasing (TSR/TAA), check the portal edge in your own scenes. A portal linked to itself should be invisible.
- **Single player, single view.** No split-screen, no VR stereo.
- **Objects partway through a portal are shown, not simulated, on the far side.** The copy has no collision, so the part that's through doesn't collide with anything until the object's centre crosses.
- **Held objects are pulled by a physics handle.** A fast-moving player can outrun one. It then trails behind, and may reach a portal after the player, which is still correct, just less tidy.
- **Uniform portal scale only,** apart from the sign of X for mirroring portals.
