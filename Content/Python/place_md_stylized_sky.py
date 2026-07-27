"""Spawn AMD_StylizedSkyActor in L_Lobby (after C++ compiles)."""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
CLS = "/Script/MarineDemolition.MD_StylizedSkyActor"


def run():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab in ("LobbyStylizedSky", "MD_StylizedSky") or "MD_StylizedSky" in cls or "BP_StylizedSky" in cls or "BP_Lobby" in cls:
            eas.destroy_actor(a)
            unreal.log(f"removed {lab}/{cls}")

    sky_cls = unreal.load_class(None, CLS)
    if not sky_cls:
        raise RuntimeError(f"class missing {CLS} — compile C++ / Live Coding first")

    actor = eas.spawn_actor_from_class(sky_cls, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("MD_StylizedSky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    # Lobby: freeze midday
    try:
        actor.set_editor_property("b_frozen_time", True)
        actor.set_editor_property("frozen_time_of_day_hours", 12.0)
    except Exception:
        try:
            actor.set_editor_property("bFrozenTime", True)
            actor.set_editor_property("FrozenTimeOfDayHours", 12.0)
        except Exception as e:
            unreal.log(f"freeze props: {e}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"placed {actor}")


run()
