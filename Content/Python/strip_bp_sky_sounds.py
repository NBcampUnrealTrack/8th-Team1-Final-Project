"""
Remove all sound/audio from BP_Sky (blueprint + level) and delete weather SFX assets.

  py "E:/Unreal/MarineDemolition/Content/Python/strip_bp_sky_sounds.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths", "md_sky_component", "migrate_bp_sky_off_sostylized"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
import migrate_bp_sky_off_sostylized as migrate

migrate = importlib.reload(migrate)

LEVEL = paths.LEVEL_LOBBY
SKY_ROOT = paths.SKY_ROOT
SRC = paths.DELETABLE_SRC_PREFIX

SOUND_KEYS = (
    "audio",
    "sound",
    "ambience",
    "ambiance",
    "weather",
    "lightning",
    "thunder",
    "rain",
    "wind",
)

ACTOR_OFF_BOOLS = (
    "audio?",
    "sounds?",
    "ambientSounds?",
    "weatherSounds?",
    "enableAudio?",
    "enableSounds?",
    "lightningSounds?",
    "weatherAudio?",
    "dynamicWeather?",
)

SOUND_DELETE_ROOTS = (
    f"{SRC}/Sounds/Weather",
    f"{SRC}/Sounds",
    f"{SKY_ROOT}/Sounds",
)


def _is_soundish(name: str, cls: str) -> bool:
    low = f"{name} {cls}".lower()
    return any(k in low for k in SOUND_KEYS)


def clear_component_sound(c, label: str = "") -> bool:
    changed = False
    name = c.get_name()
    cls = c.get_class().get_name()
    low = f"{name} {cls}".lower()
    is_audio = isinstance(c, unreal.AudioComponent) or "AudioComponent" in cls or any(
        k in low for k in SOUND_KEYS
    )
    if not is_audio:
        return False
    for prop in (
        "sound",
        "sound_class",
        "sound_mix",
        "sound_mix_modifier",
        "sound_submix",
        "sound_submix_sends",
    ):
        try:
            c.set_editor_property(prop, None)
            changed = True
        except Exception:
            pass
    for prop, val in (
        ("b_auto_activate", False),
        ("auto_activate", False),
        ("volume_multiplier", 0.0),
        ("volume", 0.0),
        ("b_is_ui_sound", False),
        ("b_allow_spatialization", False),
    ):
        try:
            c.set_editor_property(prop, val)
            changed = True
        except Exception:
            pass
    try:
        if hasattr(c, "set_active"):
            c.set_active(False, True)
        if hasattr(c, "stop"):
            c.stop()
        if hasattr(c, "deactivate"):
            c.deactivate()
        if hasattr(c, "set_visibility"):
            c.set_visibility(False, True)
    except Exception:
        pass
    unreal.log(f"CLEAR_SOUND_COMP {label or name} ({cls})")
    return changed


def strip_actor(actor) -> None:
    for prop in ACTOR_OFF_BOOLS:
        try:
            actor.set_editor_property(prop, False)
        except Exception:
            pass
    sky_comp.configure_lobby_frozen_sky(actor, sky_comp.read_frozen_hour(actor))
    for c in list(actor.get_components_by_class(unreal.ActorComponent)):
        clear_component_sound(c)


def strip_blueprint(bp_path: str) -> None:
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    if not bp:
        unreal.log_warning(f"missing {bp_path}")
        return
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        data = bfl.get_data(h)
        try:
            obj = bfl.get_associated_object(data)
        except Exception:
            obj = bfl.get_object(data)
        if not obj:
            continue
        name = obj.get_name()
        cls = obj.get_class().get_name()
        if _is_soundish(name, cls) or "AudioComponent" in cls:
            clear_component_sound(obj, f"{bp_path}:{name}")
            for prop in ("sound", "sound_class", "asset"):
                try:
                    obj.set_editor_property(prop, None)
                except Exception:
                    pass
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    except Exception as e:
        unreal.log_warning(f"compile {bp_path}: {e}")
    unreal.EditorAssetLibrary.save_asset(bp_path)
    unreal.log(f"STRIP_BP_SOUNDS {bp_path}")


def find_level_bp_sky():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab == "BP_Sky" or "BP_Sky" in cls or "MD_StylizedSky" in cls:
            return a
    return None


def delete_sound_assets() -> int:
    n = 0
    seen: set[str] = set()
    for root in SOUND_DELETE_ROOTS:
        if not unreal.EditorAssetLibrary.does_directory_exist(root):
            continue
        for asset in unreal.EditorAssetLibrary.list_assets(root, True, False):
            pkg = asset.split(".")[0]
            if pkg in seen:
                continue
            seen.add(pkg)
            if unreal.EditorAssetLibrary.delete_asset(pkg):
                unreal.log(f"DELETE_SOUND {pkg}")
                n += 1
            else:
                unreal.log_warning(f"DELETE_SOUND fail {pkg}")
    return n


def run():
    bp_paths = migrate.all_bp_sky_paths()
    if not bp_paths:
        bp_paths = [paths.BP_SKY]

    for bp_path in bp_paths:
        if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
            strip_blueprint(bp_path)

    actor = find_level_bp_sky()
    if actor:
        strip_actor(actor)
        try:
            actor.call_method("RefreshSkyNow")
        except Exception:
            pass
    else:
        unreal.log_warning("no BP_Sky in level")

    deleted = delete_sound_assets()
    migrate.refresh_owned_registry()

    bp = bp_paths[0]
    leaks = migrate.collect_sostylized_leaks([bp])
    unreal.log(f"STRIP_BP_SKY_SOUNDS_OK deleted_sounds={deleted} leaks={len(leaks)}")
    for L in leaks:
        unreal.log_warning(f"  LEAK {L}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)


run()
