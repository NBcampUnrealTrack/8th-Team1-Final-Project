"""Helpers for BP_Sky after AMD_StylizedSkyActor component split."""
from __future__ import annotations

import unreal

import md_sky_paths as sky_paths


def _component_by_class_name(actor, class_name: str):
    if not actor:
        return None
    for comp in actor.get_components_by_class(unreal.ActorComponent):
        try:
            if comp.get_class().get_name() == class_name:
                return comp
        except Exception:
            continue
    return None


def get_sky_controller(actor):
    return _component_by_class_name(actor, "MD_StylizedSkyControllerComponent")


def get_game_state_time_adapter(actor):
    return _component_by_class_name(actor, "MD_GameStateDayCycleAdapterComponent")


def set_sky_prop(actor, prop_name: str, value) -> bool:
    """Set on SkyController if present, else on actor (legacy BP)."""
    ctrl = get_sky_controller(actor)
    if ctrl:
        try:
            ctrl.set_editor_property(prop_name, value)
            return True
        except Exception:
            pass
    try:
        actor.set_editor_property(prop_name, value)
        return True
    except Exception:
        return False


def set_adapter_prop(actor, prop_name: str, value) -> bool:
    adapter = get_game_state_time_adapter(actor)
    if not adapter:
        return False
    try:
        adapter.set_editor_property(prop_name, value)
        return True
    except Exception:
        return False


def configure_match_time_drive(actor) -> None:
    set_adapter_prop(actor, "bDriveFromGameState", True)
    set_sky_prop(actor, "bLocalDayCycle", True)
    set_sky_prop(actor, "MpcShaderTimeOffsetHours", 12.0)
    set_sky_prop(actor, "MpcDayStartTimeHours", 0.0)


def configure_editor_day_cycle(actor, seconds_per_day: float = 120.0) -> None:
    set_adapter_prop(actor, "bDriveFromGameState", False)
    set_sky_prop(actor, "bFrozenTime", False)
    set_sky_prop(actor, "bLocalDayCycle", True)
    set_sky_prop(actor, "DayLengthSeconds", float(seconds_per_day))


def configure_lobby_frozen_sky(actor, hour: float = 12.0) -> None:
    """Lobby/editor: frozen midday, no GameState drive."""
    set_sky_prop(actor, "bFrozenTime", True)
    set_sky_prop(actor, "bLocalDayCycle", True)
    set_sky_prop(actor, "FrozenTimeOfDayHours", float(hour))
    set_sky_prop(actor, "LocalTimeOfDayHours", float(hour))
    set_adapter_prop(actor, "bDriveFromGameState", False)
    try:
        actor.set_editor_property("bDriveFromGameState", False)
    except Exception:
        pass


def read_frozen_hour(actor, default: float = 12.0) -> float:
    ctrl = get_sky_controller(actor)
    if ctrl:
        try:
            return float(ctrl.get_editor_property("FrozenTimeOfDayHours"))
        except Exception:
            pass
    try:
        return float(actor.get_editor_property("FrozenTimeOfDayHours"))
    except Exception:
        return default


def find_bp_sky_path() -> str:
    for p in (
        sky_paths.BP_SKY,
        f"{sky_paths.SKY_ROOT}/BP_MD_StylizedSky",
        f"{sky_paths.SKY_ROOT}/BP_Sky",
        sky_paths.LEGACY_BP_SKY,
        "/Game/Developers/TaitoP/BP_Sky",
    ):
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            return p
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in ar.get_all_assets() or []:
        if str(a.asset_name) in ("BP_MD_StylizedSky", "BP_Sky"):
            return str(a.package_name)
    return ""


def load_bp_sky_blueprint():
    path = find_bp_sky_path()
    if not path:
        return None
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset and isinstance(asset, unreal.Blueprint):
        return asset
    name = path.rsplit("/", 1)[-1]
    bp_obj = unreal.load_object(None, f"{path}.{name}")
    if bp_obj and isinstance(bp_obj, unreal.Blueprint):
        return bp_obj
    return asset if asset and hasattr(asset, "generated_class") else bp_obj


def load_bp_sky_generated_class():
    path = find_bp_sky_path()
    if not path:
        return None
    bp = load_bp_sky_blueprint()
    if bp and hasattr(bp, "generated_class"):
        gen = bp.generated_class()
        if gen:
            return gen
    name = path.rsplit("/", 1)[-1]
    return unreal.load_class(None, f"{path}.{name}_C")


def load_bp_sky_and_cdo():
    path = find_bp_sky_path()
    if not path:
        return None, None, ""
    bp = load_bp_sky_blueprint()
    gen = load_bp_sky_generated_class()
    if not gen:
        return bp, None, path
    return bp, unreal.get_default_object(gen), path


def get_bp_sky_cdo():
    _, cdo, _ = load_bp_sky_and_cdo()
    return cdo
