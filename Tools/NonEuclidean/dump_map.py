"""Debug helper: log every actor's class, label and transform in a map (default L_NE_Tunnels)."""
import sys
import unreal

path = sys.argv[1] if len(sys.argv) > 1 else "/Game/NonEuclidean/Maps/L_NE_Tunnels"
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    t = a.get_actor_transform()
    unreal.log_warning(f"[NE] {a.get_class().get_name():28s} {a.get_actor_label():18s} loc={t.translation} rot={t.rotation.rotator()} scale={t.scale3d}")
