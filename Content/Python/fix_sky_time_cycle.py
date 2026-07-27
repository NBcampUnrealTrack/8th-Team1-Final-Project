"""
Fix sky not updating with time:
- Soft refs / MPC on SkyController (not stale actor-level props)
- GameStateTimeAdapter.bDriveFromGameState for match clock
- Optional local day cycle for editor preview
"""
import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)
import md_sky_paths as sky_paths
import md_sky_component as sky_comp

BP_SKY = sky_paths.BP_SKY
PATHS = sky_paths.PATHS


def load(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    return None


def apply_soft_refs(target):
    for prop, path in PATHS.items():
        asset = load(path)
        if asset:
            sky_comp.set_sky_prop(target, prop, asset)
    extra = load(sky_paths.PATHS["EnvironmentMPC"])
    if extra and False:  # single owned MPC only
        sky_comp.set_sky_prop(target, "SupplementaryMPCs", [extra])


def configure_match_time_drive(actor):
    """Match: follow AMD_GameStateBase clock when started."""
    sky_comp.set_adapter_prop(actor, "bDriveFromGameState", True)
    sky_comp.set_sky_prop(actor, "bLocalDayCycle", True)
    sky_comp.set_sky_prop(actor, "MpcShaderTimeOffsetHours", 12.0)
    sky_comp.set_sky_prop(actor, "MpcDayStartTimeHours", 0.0)
    unreal.log("sky: GameState time drive ON (overrides frozen when match clock runs)")


def configure_editor_day_cycle(actor, seconds_per_day=120.0):
    """Editor/lobby preview without PIE game clock."""
    sky_comp.set_adapter_prop(actor, "bDriveFromGameState", False)
    sky_comp.set_sky_prop(actor, "bFrozenTime", False)
    sky_comp.set_sky_prop(actor, "bLocalDayCycle", True)
    sky_comp.set_sky_prop(actor, "DayLengthSeconds", float(seconds_per_day))
    sky_comp.set_sky_prop(actor, "LocalTimeOfDayHours", 8.0)
    unreal.log(f"sky: local day cycle ON ({seconds_per_day}s per day)")


def push_and_refresh(actor):
    mpc = load(PATHS["EnvironmentMPC"])
    if mpc:
        md_sky_mpc.push_midday_scalars(mpc, day_start=0.0, shader_offset=12.0)
    actor.call_method("RefreshSkyNow")


def run(use_match_clock=True, editor_local_cycle=False):
    bp = load(BP_SKY)
    if not bp:
        raise RuntimeError(f"missing {BP_SKY}")

    cdo = unreal.get_default_object(bp.generated_class())
    apply_soft_refs(cdo)

    if editor_local_cycle:
        configure_editor_day_cycle(cdo)
    elif use_match_clock:
        configure_match_time_drive(cdo)
    else:
        sky_comp.configure_lobby_frozen_sky(cdo, 12.0)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_SKY)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if "BP_Sky" in (a.get_actor_label() or "") or "MD_StylizedSky" in a.get_class().get_name():
            apply_soft_refs(a)
            if editor_local_cycle:
                configure_editor_day_cycle(a)
            elif use_match_clock:
                configure_match_time_drive(a)
            else:
                sky_comp.configure_lobby_frozen_sky(a, 12.0)
            push_and_refresh(a)
            ctrl = sky_comp.get_sky_controller(a)
            mpc = ctrl.get_editor_property("EnvironmentMPC") if ctrl else None
            worlds = md_sky_mpc.all_world_contexts()
            if mpc and worlds:
                t = md_sky_mpc.read_mpc_scalar(mpc, "Current Time", worlds[0])
                unreal.log(f"level BP_Sky MPC Current Time={t}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("FIX_SKY_TIME_CYCLE_OK")


# Default: match GameState drive (8:00-18:00 window)
run(use_match_clock=True, editor_local_cycle=False)
