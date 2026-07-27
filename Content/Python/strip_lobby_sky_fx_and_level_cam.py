"""
Straighten viewport + strip Lobby stylized sky FX:
  - no sound / ambience
  - no meteor shower / shooting stars
  - no rainbow / planet ring
"""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

# Component name substrings to hide/disable
FX_NAME_KEYS = (
    "meteor",
    "shooting",
    "star",
    "rainbow",
    "ring",
    "planet",
    "audio",
    "sound",
    "ambience",
    "weather",
    "rain",
    "lightning",
    "wind",
    "thunder",
    "niagara",
)

# Actor-level bool props to force off (best-effort name variants)
ACTOR_OFF_PROPS = (
    "dynamicWeather?",
    "clouds?",  # keep clouds True actually - don't include
    "shootingStars?",
    "shooting Stars?",
    "meteors?",
    "meteorShower?",
    "rainbow?",
    "planetRing?",
    "planet Ring?",
    "audio?",
    "sounds?",
    "ambientAudio?",
    "weatherAudio?",
    "enableAudio?",
    "enableSounds?",
    "enableShootingStars?",
    "enableMeteorShower?",
    "enableRainbow?",
    "enablePlanetRing?",
    "rings?",
)


def straighten_viewport():
    # Level editor camera: keep position, zero roll
    try:
        loc, rot = unreal.EditorLevelLibrary.get_level_viewport_camera_info()
    except Exception:
        loc, rot = None, None
        try:
            # UE5.8 UnrealEditorSubsystem
            ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
            loc, rot = ues.get_level_viewport_camera_info()
        except Exception as e:
            unreal.log_warning(f"get cam: {e}")
            return
    if loc is None:
        return
    # Keep pitch/yaw, force roll=0
    new_rot = unreal.Rotator(rot.pitch, rot.yaw, 0.0)
    try:
        unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, new_rot)
    except Exception:
        try:
            ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
            ues.set_level_viewport_camera_info(loc, new_rot)
        except Exception as e:
            unreal.log_warning(f"set cam: {e}")
            return
    unreal.log(f"viewport roll=0 pitch={new_rot.pitch} yaw={new_rot.yaw} loc={loc}")


def _name_hit(name: str) -> bool:
    low = name.lower()
    return any(k in low for k in FX_NAME_KEYS)


def strip_sky_fx(actor):
    # Prefer explicit known toggles
    for name, val in (
        ("dynamicWeather?", False),
        ("shootingStars?", False),
        ("meteorShower?", False),
        ("rainbow?", False),
        ("planetRing?", False),
        ("planet Rings?", False),
        ("rings?", False),
        ("audio?", False),
        ("sounds?", False),
        ("ambientSounds?", False),
        ("weatherSounds?", False),
        ("music?", False),
    ):
        try:
            actor.set_editor_property(name, val)
            unreal.log(f"actor set {name}={val}")
        except Exception:
            pass

    # Brute-force: any bool prop whose name suggests FX we want off
    for attr in dir(actor):
        if attr.startswith("_"):
            continue
        low = attr.lower().replace(" ", "")
        if not any(
            k in low
            for k in (
                "rainbow",
                "meteor",
                "shootingstar",
                "planetring",
                "ring",
                "audio",
                "sound",
                "ambience",
                "weather",
                "lightning",
                "rain",
            )
        ):
            continue
        if any(k in low for k in ("cloud", "daycycle", "time", "sun", "moon", "fog", "skylight")):
            # don't disable core sky
            if "weather" not in low and "sound" not in low and "audio" not in low:
                continue
        try:
            v = actor.get_editor_property(attr)
        except Exception:
            continue
        unreal.log(f"prop {attr}={v}")
        if isinstance(v, bool) and v is True:
            try:
                actor.set_editor_property(attr, False)
                unreal.log(f"forced off {attr}")
            except Exception:
                pass

    # Disable components by class / name
    for c in list(actor.get_components_by_class(unreal.ActorComponent)):
        try:
            name = c.get_name()
        except Exception:
            continue
        cls = c.get_class().get_name()
        low = (name + " " + cls).lower()
        is_audio = isinstance(c, unreal.AudioComponent) or ("audio" in low) or ("sound" in low)
        is_niagara = "niagara" in low
        is_mesh_fx = isinstance(c, unreal.StaticMeshComponent) and any(
            k in low for k in ("ring", "rainbow", "planet", "meteor", "star", "shoot")
        )
        if not (is_audio or is_niagara or is_mesh_fx):
            continue
        try:
            if hasattr(c, "set_visibility"):
                c.set_visibility(False, True)
            if hasattr(c, "set_hidden_in_game"):
                c.set_hidden_in_game(True, True)
            if hasattr(c, "deactivate"):
                c.deactivate()
            if hasattr(c, "stop"):
                c.stop()
            if hasattr(c, "set_active"):
                c.set_active(False, True)
            for p in ("volume_multiplier", "volume"):
                try:
                    c.set_editor_property(p, 0.0)
                except Exception:
                    pass
            unreal.log(f"disabled {name} ({cls})")
        except Exception as e:
            unreal.log(f"disable fail {name}: {e}")


def run():
    straighten_viewport()
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sky = None
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab == "LobbyStylizedSky" or "BP_StylizedSky" in cls or "BP_LobbyStylizedSky" in cls:
            sky = a
            unreal.log(f"sky {lab} {cls}")
            strip_sky_fx(a)
    if not sky:
        unreal.log_warning("no sky actor")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("STRIP_SKY_FX_OK")


run()
