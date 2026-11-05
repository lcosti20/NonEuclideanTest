"""Play-in-editor test of PortalWorlds in the Blueprint-only test project.

  UnrealEditor.exe PortalWorldsBPTest.uproject -unattended -nosplash -ExecutePythonScript="<this file>"

1. Checks that the plugin's Blueprint API is visible (Python sees exactly the Blueprint-exposed
   functions, properties and events).
2. Starts PIE in L_PortalFall, binds a listener to the player's PortalTraversal.OnPortalTraversed
   (the same binding a Blueprint event node makes), lets the player fall through the portal loop
   for a few seconds, and reports what the event, the components and the subsystem saw.
"""

import traceback

import unreal

PIE_SECONDS = 6.0
state = {"t": 0.0, "pie": False, "bound": False, "events": [], "done": False, "handle": None}


def log(msg):
    unreal.log_warning("[BPTEST] " + msg)


def check_api():
    expected = {
        unreal.PortalTraversalComponent: ["on_portal_traversed", "eye", "get_traversal_count", "reset_tracking", "apply_scale_factor",
                                           "is_mirrored", "set_mirrored", "mirror_movement_input"],
        unreal.PortalSubsystem: ["get_all_portals", "get_current_space", "get_actor_space", "set_actor_space",
                                 "refresh_spaces", "get_nearest_portal_distance", "get_last_capture_count", "is_view_mirrored"],
        unreal.Portal: ["front_link", "back_link", "surface_half_extent", "connect", "get_normal", "get_link_for", "is_mirrored_link_to"],
    }
    missing = []
    for cls, names in expected.items():
        for name in names:
            if not hasattr(cls, name) and name not in [p for p in dir(cls)]:
                missing.append(f"{cls.__name__}.{name}")
    # Properties are only listed on instances' editor properties; check via the class default object.
    log("Blueprint API: " + ("all present" if not missing else "MISSING " + ", ".join(missing)))


def on_traversed(from_portal, to_portal, scale_factor):
    state["events"].append((from_portal.get_actor_label() if from_portal else None,
                            to_portal.get_actor_label() if to_portal else None, round(scale_factor, 3)))


def tick(dt):
    try:
        state["t"] += dt
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if not state["pie"]:
            les.editor_request_begin_play()
            state["pie"] = True
            state["t"] = 0.0
            return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if world is None:
            if state["t"] > 30.0:
                log("PIE never started")
                finish()
            return
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        if pawn and not state["bound"]:
            comp = pawn.get_component_by_class(unreal.PortalTraversalComponent)
            comp.on_portal_traversed.add_callable(on_traversed)
            state["bound"] = True
            state["t"] = 0.0
            state["pawn"] = pawn
            state["comp"] = comp
            log(f"pawn {pawn.get_class().get_name()}, traversal Eye = {comp.get_editor_property('eye').get_name() if comp.get_editor_property('eye') else None}")
            return
        if state["bound"] and state["t"] >= PIE_SECONDS:
            comp = state["comp"]
            # The PIE world's PortalSubsystem instance (Blueprint gets it with the "Get PortalSubsystem" node).
            subsystem = next(o for o in unreal.ObjectIterator(unreal.PortalSubsystem) if o.get_outer() == world)
            portals = subsystem.get_all_portals()
            log(f"OnPortalTraversed fired {len(state['events'])} times; GetTraversalCount = {comp.get_traversal_count()}")
            log(f"first events: {state['events'][:3]}")
            log(f"subsystem: {len(portals)} portals {[p.get_actor_label() for p in portals]}, "
                f"current space {subsystem.get_current_space()}, captures last frame {subsystem.get_last_capture_count()}, "
                f"nearest portal {subsystem.get_nearest_portal_distance(state['pawn'].get_actor_location()):.0f} cm")
            log(f"GetActorSpace(pawn) = {unreal.PortalSubsystem.get_actor_space(state['pawn'])}")
            finish()
    except Exception:
        log("tick failed: " + traceback.format_exc().replace("\n", " | "))
        finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    log("finishing (t=%.2f) from: %s" % (state["t"], " | ".join(l.strip() for l in traceback.format_stack(limit=4))))
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    log("done")
    unreal.SystemLibrary.quit_editor()


try:
    # -ExecutePythonScript closes the editor once the script returns unless it is kept alive;
    # finish() quits when the test is done.
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    check_api()
    log("python subsystem helpers: " + str([n for n in dir(unreal) if "ubsystem" in n and ("Library" in n or n.startswith("get_"))]))
    state["handle"] = unreal.register_slate_post_tick_callback(tick)
except Exception:
    unreal.log_error("[BPTEST] failed:\n" + traceback.format_exc())
    unreal.SystemLibrary.quit_editor()
