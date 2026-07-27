"""
Replace custom Lobby sky with SoStylized BP_StylizedSky (Classic Day look).
Also hide conflicting level lights/atmosphere that fight the stylized sky.
"""
from __future__ import annotations

import unreal

SRC_BP = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/BP_StylizedSky"
DST_DIR = "/Game/Developers/TaitoP/Environment/Sky"
DST_BP = f"{DST_DIR}/BP_LobbyStylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def duplicate_bp():
    if not unreal.EditorAssetLibrary.does_directory_exist(DST_DIR):
        unreal.EditorAssetLibrary.make_directory(DST_DIR)
    if unreal.EditorAssetLibrary.does_asset_exist(DST_BP):
        # Remove our custom shell so we can replace with real duplicate
        unreal.EditorAssetLibrary.delete_asset(DST_BP)
    ok = unreal.EditorAssetLibrary.duplicate_asset(SRC_BP, DST_BP)
    if not ok:
        # duplicate_asset returns object in some versions
        if not unreal.EditorAssetLibrary.does_asset_exist(DST_BP):
            raise RuntimeError("failed to duplicate BP_StylizedSky")
    unreal.EditorAssetLibrary.save_asset(DST_BP)
    unreal.log(f"duplicated -> {DST_BP}")
    return unreal.EditorAssetLibrary.load_asset(DST_BP)


def configure_instance(actor):
    """Push Classic Day / midday settings where possible."""
    # Common SoStylized property names (try many)
    candidates = {
        "Time of Day": 12.0,
        "TimeOfDay": 12.0,
        "time_of_day": 12.0,
        "TOD": 12.0,
        "Animate Time Of Day": False,
        "AnimateTimeOfDay": False,
        "bAnimateTimeOfDay": False,
        "Use Lite Materials": False,
        "bUseLiteMaterials": False,
    }
    for prop, val in candidates.items():
        for name in (prop, prop.replace(" ", ""), prop.replace(" ", "_")):
            try:
                actor.set_editor_property(name, val)
                unreal.log(f"set {name}={val}")
            except Exception:
                pass

    # Reflect all properties containing time/day/weather
    try:
        for p in actor.__dict__:
            pass
    except Exception:
        pass

    # Editor property dump via get_editor_property trial on likely BP vars
    likely = [
        "time_of_day",
        "TimeOfDay",
        "day_length_in_minutes",
        "DayLengthInMinutes",
        "sky_material",
        "cloud_material",
        "weather",
        "Weather",
        "weather_state",
        "WeatherState",
        "current_weather",
        "orbit_inclination",
        "sun_vertical_offset",
        "cloud_speed",
        "CloudSpeed",
        "enable_volumetric_clouds",
        "use_sky_atmosphere",
    ]
    for name in likely:
        try:
            v = actor.get_editor_property(name)
            unreal.log(f"has {name}={v}")
        except Exception:
            pass


def clear_conflicting():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        path = a.get_path_name()
        # Remove our old custom sky
        if lab in ("LobbyStylizedSky",) or "BP_LobbyStylizedSky" in cls or "BP_LobbyStylizedSky" in path:
            eas.destroy_actor(a)
            continue
        if "BP_StylizedSky" in cls and "SoStylized" not in path:
            # leftover
            pass
        # Hide level lights that fight integrated sky lighting
        if lab in (
            "PastelSkyAtmosphere",
            "PastelSkyLight",
            "PastelDirectionalLight",
            "SkyAtmosphere",
        ) or cls in ("SkyAtmosphere",):
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
                # also disable
                try:
                    a.set_actor_enable_collision(False)
                except Exception:
                    pass
                unreal.log(f"hid {lab or cls}")
            except Exception as e:
                unreal.log_warning(str(e))
        # Disable duplicate directional / skylights labeled pastel
        if "Pastel" in lab and ("Light" in lab or "Directional" in lab or "SkyLight" in lab):
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
                unreal.log(f"hid light {lab}")
            except Exception:
                pass


def spawn(bp_asset):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # Also destroy any existing SoStylized sky already in level
    for a in list(eas.get_all_level_actors()):
        cls = a.get_class().get_name()
        lab = a.get_actor_label() or ""
        if "BP_StylizedSky" in cls or lab in ("BP_StylizedSky", "StylizedSky", "LobbyStylizedSky"):
            eas.destroy_actor(a)

    actor = eas.spawn_actor_from_object(bp_asset, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if not actor:
        raise RuntimeError("spawn failed")
    actor.set_actor_label("LobbyStylizedSky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    configure_instance(actor)
    unreal.log(f"spawned {actor}")
    return actor


def run():
    bp = duplicate_bp()
    clear_conflicting()
    spawn(bp)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("PLACE_STYLIZED_SKY_OK")


run()
