# NonEuclideanTest

A test of whether non-Euclidean worlds can be built in **Unreal Engine 5.8**, by recreating CodeParade's video [Non-Euclidean Worlds Engine](https://www.youtube.com/watch?v=kEB11PQ9Eo8) and its open-source engine ([HackerPoet/NonEuclidean](https://github.com/HackerPoet/NonEuclidean), MIT).

**Result: it works.** Every demo in the video (apart from its VR section, which was out of scope) runs in UE 5.8 without engine modifications:
- it uses the original's meshes, textures and level layouts;
- it matches the video's look;
- it runs at several hundred fps at 1080p.

The portal system that makes it work is packaged as a reusable plugin, **PortalWorlds**, which also works in Blueprint-only projects.

## The demos

Open `NonEuclideanTest.uproject` and press Play, or run it standalone. Walk with WASD and the mouse, and press a number key to switch demo:

| Key | Map | What happens |
| --- | --- | --- |
| 1 | `L_NE_Tunnels` | A long tunnel that's short inside, and a short tunnel that's long inside |
| 2 | `L_NE_House3` | A four-room house that only has three rooms |
| 3 | `L_NE_House6` | The same house with six rooms |
| 4 | `L_NE_Pillar` | Each lap round a pillar takes you to a different room |
| 5 | `L_NE_Slope` | Go down a tunnel and come out on top of the hill |
| 6 | `L_NE_Scale` | A tunnel that shrinks you, and one that grows you back |
| 7 | `L_NE_Mirror` | Not in the video: a tunnel that mirrors the world. Go through and every landmark is on the other side; go round and through again and it's back |
| 8 | `L_NE_ObjectPuzzles` | Not in the video: puzzles about carrying objects through portals. A shrinking tunnel changes their size and a mirror tunnel flips their handedness; fill four sockets to open the gate |

In `L_NE_ObjectPuzzles`, **E** picks up or drops an object and the **left mouse button** throws it. The HUD lists what each socket needs. See [The object puzzle map](#the-object-puzzle-map).

The console command `NE.Demo <n>` does the same as the keys.

## The object puzzle map

`L_NE_ObjectPuzzles` tests the mechanic of bringing objects through portals to change their size and handedness (chirality). It has two portal stations and four objects:

- **Mirror tunnel** (west): walk into tunnel A and come out of tunnel B, 7 m away, mirrored.
- **Shrink tunnel** (east): walk into the full-size tunnel and come out of the half-size one at half size. Go back the other way to double in size.
- **Objects:** two keys and two crates. Each key is four cubes in a twist, with a red cube at one end. It's chiral: its mirror image can't be turned to match it.

Each socket shows a translucent ghost of exactly what it wants. A matching object dropped on it locks in and turns the lamp green. A wrong one turns it red, and the HUD says why ("wrong size", "wrong way round").

| Socket | Wants | One solution |
| --- | --- | --- |
| Mirror socket | a mirrored key at full size | Carry the key through the mirror tunnel, walk back, drop it on the socket |
| Small mirror socket | a mirrored key at half size | Send the spare key through both tunnels, in either order |
| Small socket | a crate at half size | Throw or carry a crate through the shrink tunnel |
| Big socket | a crate at double size | Throw a crate into the half-size tunnel's mouth (it's too low for you to walk into), or shrink yourself first and carry it back |

All four open the gate to the trophy.

Carrying matters: an object you carry through a portal changes together with you. A key carried through the mirror is mirrored in the world, and so are you, so it still looks the same to you. Throw it through, or carry it and walk back round, and you see it flipped. The same goes for size.

`-NEPuzzleTest` plays through all of this by script and logs a PASS/FAIL line per check (see the [tools README](Tools/NonEuclidean/README.md#object-puzzle-checks-l_ne_objectpuzzles)).

## What's where

| What | Where |
| --- | --- |
| The plan, and the results with measurements and screenshots | `Docs/` (start with `Docs/Results_Non-Euclidean_worlds_in_Unreal_Engine_5_8.md`) |
| The PortalWorlds plugin | `Plugins/PortalWorlds/` and its [README](Plugins/PortalWorlds/README.md) |
| The player, the game mode and the automated test harness | `Source/NonEuclideanTest/NonEuclidean/` |
| The asset pipeline, map builders and every check with its command | `Tools/NonEuclidean/` and its [README](Tools/NonEuclidean/README.md) |
| The Blueprint-only project test | `Tools/NonEuclidean/bp_test/` |
| CodeParade's original assets and licence | `SourceArt/NonEuclidean/Original/` |

**Building this project** needs UE 5.8 and Visual Studio with the "Game development with C++" workload, because the project has a C++ module. Open the `.uproject` and let Unreal build it, or build `NonEuclideanTestEditor` (Win64, Development).

## The PortalWorlds plugin

PortalWorlds gives you seamless portals for non-Euclidean spaces:
- **Recursive rendering:** portals show the scene through other portals, several levels deep, with no visible edge and no frame lag.
- **Walking through:** the player passes through without a visible jump. A portal of a different size scales the player, and a mirroring portal flips the world left-right.
- **Spaces:** keep the alternative versions of a room out of sight of each other.

It works from C++ and from Blueprint, and supports UE 5.8 on Windows only. The full guide is in the [plugin README](Plugins/PortalWorlds/README.md).

### Install it in your project

1. **Get a copy of the plugin.**
   - **For a Blueprint-only project, or to skip compiling,** use a prebuilt copy, which includes `Binaries/`. Make one from this repository (Visual Studio required, once):
     ```powershell
     powershell -ExecutionPolicy Bypass -File Tools\NonEuclidean\package_plugin.ps1
     ```
     The copy is written to `Saved/PluginPackage/PortalWorlds`. Use this script rather than plain `RunUAT BuildPlugin`: it keeps a setting Blueprint-only projects need in order to package the plugin.
   - **For a C++ project,** you can copy `Plugins/PortalWorlds` straight from this repository; Unreal compiles it with your project.
2. **Add it to your project:** copy the `PortalWorlds` folder into your project's `Plugins/` folder (create it if needed). Then enable **Portal Worlds** in **Edit > Plugins** and restart the editor. It's off by default, so each project has to enable it.
3. **Turn on the global clip plane:** in **Project Settings > Rendering**, enable **Support global clip plane for Planar Reflections** and restart the editor. In `Config/DefaultEngine.ini` that's:
   ```ini
   [/Script/Engine.RendererSettings]
   r.AllowGlobalClipPlane=True
   r.AllowOcclusionQueries=False
   ```
   Turning off occlusion queries is recommended: portal views jump between viewpoints, and stale occlusion results can make geometry pop for a frame.
4. **Set up the player:**
   - In your player controller, set **Player Camera Manager Class** to **PortalPlayerCameraManager**.
   - Add a **Portal Traversal** component to your player pawn. It finds the pawn's camera by itself.
5. **Place portals:** drag **Portal** actors into the level, scale them to fit your doorways, and set each one's **Front Link** and **Back Link** to the portal it leads to.

**Packaging a Blueprint-only project** that uses PortalWorlds (or any plugin with code) needs Visual Studio with the "Game development with C++" workload. Unreal builds the plugin into your game automatically, and your project stays Blueprint-only. Using the plugin in the editor doesn't need Visual Studio.

## Licence

This project is MIT-licensed (see `LICENSE`). It includes assets and code ported from CodeParade's NonEuclidean engine, also MIT (© 2018 CodeParade); see `SourceArt/NonEuclidean/Original/LICENSE` and `Plugins/PortalWorlds/THIRD_PARTY_NOTICES.md`.
