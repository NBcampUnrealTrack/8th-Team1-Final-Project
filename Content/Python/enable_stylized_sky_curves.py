"""Enable SoStylized BP_StylizedSky full curve day-cycle system in L_Lobby."""
from __future__ import annotations

import unreal

SRC_BP = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/BP_StylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

CURVE_ASSETS = [
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Curves/Atlas_Sky",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Curves/Atlas_Clouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Curves/Atlas_SunMoon",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Curves/Curve_Sky_Classic_Day",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Curves/Curve_Clouds_Classic_Day",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Curves/Curve_SunColor_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Curves/Curve_SunGlow_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/MPC_GlobalEnvironment",
]


def find_or_spawn():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sky = None
    for a in list(eas.get_all_level_actors()):
        cls = a.get_class().get_name()
        lab = a.get_actor_label() or ""
        if "BP_LobbyStylizedSky" in cls:
            eas.destroy_actor(a)
            unreal.log(f"removed crude {lab}")
            continue
        if "BP_StylizedSky" in cls:
            sky = a
    if not sky:
        bp = unreal.EditorAssetLibrary.load_asset(SRC_BP)
        if not bp:
            raise RuntimeError("BP_StylizedSky missing")
        sky = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        unreal.log("spawned BP_StylizedSky")
    sky.set_actor_label("LobbyStylizedSky")
    try:
        sky.set_folder_path("Environment/Sky")
    except Exception:
        pass
    return sky


def hide_conflicts():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if any(
            k in lab
            for k in (
                "PastelSun",
                "PastelFill",
                "PastelSkyLight",
                "PastelFog",
                "PastelSkyAtmosphere",
                "PastelPPV",
            )
        ) or cls == "SkyAtmosphere":
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


def enable_curves(sky):
    # Full curve-driven day cycle ON
    sets = [
        ("dayCycleEnabled?", True),
        ("freezeAllTime?", False),
        ("start Time", 12.0),
        ("day Length", 600.0),  # 10 minutes per full day
        ("dynamicWeather?", False),
        ("clouds?", True),
        ("cloud Shadows?", True),
    ]
    for name, val in sets:
        try:
            sky.set_editor_property(name, val)
            unreal.log(f"set {name}={val}")
        except Exception as e:
            unreal.log(f"fail {name}: {e}")

    for name in (
        "start Time",
        "current Time of Day",
        "dayCycleEnabled?",
        "freezeAllTime?",
        "day Length",
        "day Cycle Progress",
        "dynamicWeather?",
        "clouds?",
        "starting Weather",
    ):
        try:
            unreal.log(f"read {name}={sky.get_editor_property(name)}")
        except Exception as e:
            unreal.log(f"read fail {name}: {e}")


def verify_curve_assets():
    for p in CURVE_ASSETS:
        ok = unreal.EditorAssetLibrary.does_asset_exist(p)
        unreal.log(f"{'OK' if ok else 'MISS'} {p}")


def run():
    verify_curve_assets()
    hide_conflicts()
    sky = find_or_spawn()
    enable_curves(sky)
    try:
        sl = sky.get_component_by_class(unreal.SkyLightComponent)
        if sl:
            sl.recapture_sky()
    except Exception:
        pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("CURVE_DAYCYCLE_OK")


run()
