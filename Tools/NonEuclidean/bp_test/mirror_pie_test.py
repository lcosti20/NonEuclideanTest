"""Play-in-editor test of the mirror tunnel in the Blueprint-only test project.

  UnrealEditor.exe PortalWorldsBPTest.uproject -unattended -nosplash -ExecutePythonScript="<this file>"

Opens L_MirrorTunnel, plays in editor and drives BP_MirrorFlyer from Python (the same calls a
Blueprint graph would make):
  1. Screenshots for mirror_check.py at two poses: the shot, the same pose with Portal.Hide 1, the
     reflected pose (across the tunnel's centre plane y = 0) with Portal.Hide 1, and Portal.Debug 1.
  2. Flies through the portal with Add Movement Input and checks Is Mirrored, Is View Mirrored and
     On Portal Traversed; screenshot of the mirrored world.
  3. While mirrored: yaw input (Add Yaw Input) and strafe input (Add Movement Input along the
     control rotation's right) must come out reversed in world terms. Then the same unmirrored.
  4. Flies back through and checks the player is no longer mirrored.
  Control shots with Portal2's link made plain (positive X scale) measure what any portal shows in
  this renderer, for comparison.
Screenshots go to the project's Saved/Screenshots/WindowsEditor/BPMirror_*.png.
"""

import traceback

import unreal

SETTLE = 2.0  # seconds for TSR and Lumen to converge after a pose or setting change
POSES = {  # name: (location, yaw)
    "centre": ((300.0, 0.0, 150.0), 180.0),
    "offset": ((250.0, 40.0, 170.0), 190.0),
    "offset_reflected": ((250.0, -40.0, 170.0), 170.0),
}
SHOTS = [  # (file, pose, console settings)
    ("centre", "centre", []), ("centre_hidden", "centre", ["Portal.Hide 1"]), ("centre_pink", "centre", ["Portal.Debug 1"]),
    ("offset", "offset", []), ("offset_hidden", "offset", ["Portal.Hide 1"]),
    ("offset_hiddenref", "offset_reflected", ["Portal.Hide 1"]), ("offset_pink", "offset", ["Portal.Debug 1"]),
    # Control: the same with Portal2 made a plain (non-mirroring) link, to tell a mirroring error
    # from what any portal shows in this renderer. Predicted by the hidden shot, unflipped.
    ("plain_centre", "centre", ["plain"]), ("plain_offset", "offset", ["plain"]),
]

state = {"step": "start", "t": 0.0, "events": [], "handle": None, "done": False, "shot": 0, "results": []}


def log(msg):
    unreal.log_warning("[BPMIRROR] " + msg)


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def console(cmd):
    unreal.SystemLibrary.execute_console_command(world(), cmd)


def pose(name):
    loc, yaw = POSES[name]
    state["pawn"].set_actor_location(unreal.Vector(*loc), False, True)
    state["pc"].set_control_rotation(unreal.Rotator(0, 0, yaw))
    state["comp"].reset_tracking()


def set_portal2_mirrored(mirrored):
    """Portal2's X scale: negative (as built) mirrors, positive makes a plain link."""
    portal2 = next(p for p in state["subsystem"].get_all_portals() if p.get_actor_label() == "Portal2")
    scale = portal2.get_actor_scale3d()
    portal2.set_actor_scale3d(unreal.Vector(-abs(scale.x) if mirrored else abs(scale.x), scale.y, scale.z))


def go(step):
    state["step"] = step
    state["t"] = 0.0


def on_traversed(from_portal, to_portal, scale_factor):
    state["events"].append((from_portal.get_actor_label(), to_portal.get_actor_label()))


def yaw():
    return state["pc"].get_control_rotation().yaw


def right_vector():
    return unreal.MathLibrary.get_right_vector(unreal.Rotator(0, 0, yaw()))


def tick(dt):
    try:
        state["t"] += dt
        step, t = state["step"], state["t"]
        if step == "start":
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
            go("wait_pawn")
        elif step == "wait_pawn":
            w = world()
            pawn = unreal.GameplayStatics.get_player_pawn(w, 0) if w else None
            if pawn:
                state["pawn"], state["pc"] = pawn, unreal.GameplayStatics.get_player_controller(w, 0)
                state["comp"] = pawn.get_component_by_class(unreal.PortalTraversalComponent)
                state["comp"].on_portal_traversed.add_callable(on_traversed)
                state["subsystem"] = next(o for o in unreal.ObjectIterator(unreal.PortalSubsystem) if o.get_outer() == w)
                log(f"pawn {pawn.get_class().get_name()}, controller {state['pc'].get_class().get_name()}")
                go("shot_setup")
            elif t > 30:
                raise RuntimeError("PIE never started")
        elif step == "shot_setup":
            name, pose_name, settings = SHOTS[state["shot"]]
            console("Portal.Hide 0")
            console("Portal.Debug 0")
            set_portal2_mirrored("plain" not in settings)
            for cmd in settings:
                if cmd != "plain":
                    console(cmd)
            pose(pose_name)
            go("shot_take")
        elif step == "shot_take" and t >= SETTLE:
            name = SHOTS[state["shot"]][0]
            unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, f"BPMirror_{name}.png")
            go("shot_next")
        elif step == "shot_next" and t >= 1.0:
            state["shot"] += 1
            if state["shot"] < len(SHOTS):
                go("shot_setup")
            else:
                console("Portal.Hide 0")
                console("Portal.Debug 0")
                set_portal2_mirrored(True)
                pose("centre")
                go("fly_in")
        elif step == "fly_in":
            state["pawn"].add_movement_input(unreal.Vector(-1, 0, 0), 1.0, True)
            if state["pawn"].get_actor_location().x < -700:
                log(f"flew through: events {state['events']}, Is Mirrored {state['comp'].is_mirrored()}, "
                    f"Is View Mirrored {state['subsystem'].is_view_mirrored()}, current space {state['subsystem'].get_current_space()}, "
                    f"location {state['pawn'].get_actor_location()}")
                go("mirrored_shot")
            elif t > 10:
                raise RuntimeError("never got through the tunnel")
        elif step == "mirrored_shot" and t >= SETTLE:
            unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, "BPMirror_mirrored.png")
            go("controls_start")
        elif step == "controls_start" and t >= 1.0:
            state["yaw0"] = yaw()
            go("yaw")
        elif step == "yaw":
            state["pc"].add_yaw_input(1.0)
            if t >= 0.25:
                state["yaw_change"] = unreal.MathLibrary.normalize_axis(yaw() - state["yaw0"])
                state["loc0"] = state["pawn"].get_actor_location()
                state["right0"] = right_vector()
                go("strafe")
        elif step == "strafe":
            state["pawn"].add_movement_input(state["right0"], 1.0, True)
            if t >= 0.25:
                moved = state["pawn"].get_actor_location() - state["loc0"]
                along_right = moved.dot(state["right0"])
                mirrored = state["comp"].is_mirrored()
                state["results"].append((mirrored, state["yaw_change"], along_right))
                log(f"controls, mirrored={mirrored}: yaw input +1 for 0.25 s changed the control yaw by "
                    f"{state['yaw_change']:+.1f} deg; strafe right moved {along_right:+.0f} cm along the control rotation's right")
                if mirrored:
                    pose_back()
                    go("fly_back")
                else:
                    finish()
        elif step == "fly_back":
            state["pawn"].add_movement_input(unreal.Vector(1, 0, 0), 1.0, True)
            loc = state["pawn"].get_actor_location()
            if not state["comp"].is_mirrored() and loc.x > 300:
                log(f"flew back: events {state['events']}, Is Mirrored {state['comp'].is_mirrored()}, "
                    f"Is View Mirrored {state['subsystem'].is_view_mirrored()}, location {loc}")
                go("settle_back")
            elif t > 10:
                raise RuntimeError("never got back through the tunnel")
        elif step == "settle_back" and t >= 1.0:
            go("controls_start")
    except Exception:
        log("failed: " + traceback.format_exc().replace("\n", " | "))
        finish()


def pose_back():
    """Turns round (in the mirrored copy) to face back into the tunnel."""
    y = state["pawn"].get_actor_location().y
    state["pawn"].set_actor_location(unreal.Vector(-700.0, y, 150.0), False, True)
    state["pc"].set_control_rotation(unreal.Rotator(0, 0, 0))
    state["comp"].reset_tracking()


def finish():
    if state["done"]:
        return
    state["done"] = True
    ok = (len(state["results"]) == 2 and state["results"][0][0] and not state["results"][1][0]
          and state["results"][0][1] * state["results"][1][1] < 0 and state["results"][0][2] * state["results"][1][2] < 0)
    log("RESULT " + ("PASS" if ok else "FAIL") + f": traversals {state['events']}, controls {state['results']}")
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


try:
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Test/L_MirrorTunnel"):
        raise RuntimeError("could not open L_MirrorTunnel")
    state["handle"] = unreal.register_slate_post_tick_callback(tick)
except Exception:
    unreal.log_error("[BPMIRROR] failed:\n" + traceback.format_exc())
    unreal.SystemLibrary.quit_editor()
