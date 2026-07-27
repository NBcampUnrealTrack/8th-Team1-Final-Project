"""
Replace crude custom Lobby sky with SoStylized BP_StylizedSky configured as
Classic midday Clear (exact reference look).
"""
from __future__ import annotations

import unreal

SRC_BP = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/BP_StylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def clear_crude_and_conflicts():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        path = a.get_path_name()
        # Remove our handmade / duplicated lobby sky instances
        if (
            lab == "LobbyStylizedSky"
            or "BP_LobbyStylizedSky" in cls
            or ("BP_StylizedSky" in cls and "SoStylized" not in path and "Lobby" in path)
        ):
            eas.destroy_actor(a)
            unreal.log(f"destroyed {lab} {cls}")
            continue
        # Also remove any previously placed SoStylized sky so we start clean
        if "BP_StylizedSky" in cls:
            eas.destroy_actor(a)
            unreal.log(f"destroyed existing stylized sky {lab}")
            continue
        if any(
            k in lab or k == cls
            for k in (
                "PastelSun",
                "PastelFill",
                "PastelSkyLight",
                "PastelFog",
                "PastelSkyAtmosphere",
                "PastelPPV",
                "SkyAtmosphere",
            )
        ) or cls in ("SkyAtmosphere",):
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
                for cc in (
                    unreal.DirectionalLightComponent,
                    unreal.SkyLightComponent,
                    unreal.ExponentialHeightFogComponent,
                ):
                    c = a.get_component_by_class(cc)
                    if c:
                        try:
                            c.set_visibility(False)
                            c.set_intensity(0.0)
                        except Exception:
                            pass
                unreal.log(f"hid {lab}/{cls}")
            except Exception as e:
                unreal.log_warning(str(e))


def configure(actor):
    # Property names use spaces (SoStylized BP vars)
    sets = [
        ("start Time", 12.0),
        ("dayCycleEnabled?", False),
        ("freezeAllTime?", True),
        ("dynamicWeather?", False),
        ("clouds?", True),
        ("cloud Shadows?", True),
        ("starting Weather", "Clear"),
    ]
    for name, val in sets:
        try:
            actor.set_editor_property(name, val)
            unreal.log(f"set {name}={val}")
        except Exception as e:
            unreal.log(f"skip {name}: {e}")

    # Readback
    for name in (
        "start Time",
        "current Time of Day",
        "dayCycleEnabled?",
        "freezeAllTime?",
        "dynamicWeather?",
        "starting Weather",
        "clouds?",
    ):
        try:
            unreal.log(f"read {name}={actor.get_editor_property(name)}")
        except Exception:
            pass


def run():
    bp = unreal.EditorAssetLibrary.load_asset(SRC_BP)
    if not bp:
        raise RuntimeError("BP_StylizedSky missing")
    clear_crude_and_conflicts()
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if not actor:
        raise RuntimeError("spawn failed")
    actor.set_actor_label("LobbyStylizedSky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    configure(actor)
    # Recapture skylight inside BP if possible
    try:
        sl = actor.get_component_by_class(unreal.SkyLightComponent)
        if sl:
            sl.recapture_sky()
    except Exception:
        pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("REF_SKY_PLACED_OK")


run()
