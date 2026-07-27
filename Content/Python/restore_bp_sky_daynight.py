"""
Restore BP_Sky: owned sky assets, cloud strip textures, day/night local cycle.

Run (PIE stopped, L_Lobby):
  py "E:/Unreal/MarineDemolition/Content/Python/restore_bp_sky_daynight.py"
"""
from __future__ import annotations

import importlib
import json
import sys
from pathlib import Path

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_component",
    "md_sky_bp_profile",
    "md_sky_owned_assets",
    "restore_bp_sky_full",
    "fix_sky_dome_interior",
    "migrate_bp_sky_off_sostylized",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp
import md_sky_bp_profile as profile_mod
import md_sky_owned_assets as owned_assets

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
profile_mod = importlib.reload(profile_mod)
owned_assets = importlib.reload(owned_assets)

SKY_ROOT = paths.SKY_ROOT
SRC = "/Game/Developers/TaitoP/SoStylized"
ART_VARIED = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_sostylized_varied")
ART_REF = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_ref_sostylized")
CLOUD_STRIPS = (
    "T_CloudLayer01",
    "T_CloudLayer13",
    "T_CloudLayer_Overhead04",
)
DAY_CYCLE_SECONDS = 600.0  # 10 min full day


def pick_png(stem: str) -> str:
    for folder in (ART_VARIED, ART_REF):
        p = folder / f"{stem}.png"
        if p.is_file():
            return str(p).replace("\\", "/")
    raise FileNotFoundError(f"cloud PNG missing: {stem}")


def ensure_owned_folders() -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(SKY_ROOT):
        unreal.EditorAssetLibrary.make_directory(SKY_ROOT)
    for sub in ("Materials", "Textures", "Meshes", "Curves", "Textures/CloudLayers", "Effects"):
        p = f"{SKY_ROOT}/{sub}"
        if not unreal.EditorAssetLibrary.does_directory_exist(p):
            unreal.EditorAssetLibrary.make_directory(p)


def duplicate_owned_closure() -> int:
    import migrate_bp_sky_off_sostylized as migrate

    migrate = importlib.reload(migrate)
    seeds = list(migrate.SEED_ROOTS)
    for stem in CLOUD_STRIPS:
        seeds.append(f"{SRC}/Environment/Sky/Textures/CloudLayers/{stem}")
    packages = migrate.collect_deps(seeds)
    unreal.log(f"OWNED closure packages={len(packages)}")
    n = 0
    for src in sorted(packages, key=migrate.rank_path):
        if migrate.duplicate_one(src):
            n += 1
    redirect = migrate.build_redirect_map()
    for dst in migrate.list_owned_packages():
        migrate.fix_asset_refs(dst, redirect)
    migrate.refresh_owned_registry()
    return n


def import_cloud_strips() -> int:
    dest = f"{SKY_ROOT}/Textures/CloudLayers"
    if not unreal.EditorAssetLibrary.does_directory_exist(dest):
        unreal.EditorAssetLibrary.make_directory(dest)
    n = 0
    for stem in CLOUD_STRIPS:
        src = pick_png(stem)
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = dest
        task.destination_name = stem
        task.replace_existing = True
        task.automated = True
        task.save = True
        task.factory = unreal.TextureFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.EditorAssetLibrary.load_asset(f"{dest}/{stem}")
        if tex:
            tex.set_editor_property("srgb", True)
            tex.set_editor_property(
                "compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT
            )
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
            unreal.log(f"CLOUD_STRIP {dest}/{stem} <- {src}")
            n += 1
    return n


def wire_cloud_mi_textures() -> None:
    mi_path = f"{SKY_ROOT}/Materials/Presets/MI_StylizedClouds_Classic"
    if not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    for stem in CLOUD_STRIPS:
        tex_path = f"{SKY_ROOT}/Textures/CloudLayers/{stem}"
        if not unreal.EditorAssetLibrary.does_asset_exist(tex_path):
            continue
        tex = unreal.EditorAssetLibrary.load_asset(tex_path)
        for tp in list(mi.texture_parameter_values):
            if str(tp.get_editor_property("parameter_info").get_editor_property("name")) == stem:
                tp.set_editor_property("parameter_value", tex)
        unreal.log(f"wire cloud MI {stem}")
    unreal.EditorAssetLibrary.save_asset(mi_path)


def recompile_cloud_materials() -> None:
    owned_mi = f"{SKY_ROOT}/Materials/Presets/MI_StylizedClouds_Classic"
    owned_mat = f"{SKY_ROOT}/Materials/M_StylizedClouds"
    for mat in (owned_mat, owned_mi):
        if not unreal.EditorAssetLibrary.does_asset_exist(mat):
            unreal.log_warning(f"skip recompile missing {mat}")
            continue
        m = unreal.EditorAssetLibrary.load_asset(mat)
        if isinstance(m, unreal.Material):
            unreal.MaterialEditingLibrary.recompile_material(m)
        unreal.EditorAssetLibrary.save_asset(mat)
        unreal.log(f"recompiled {mat}")


def fix_owned_mpc_refs() -> None:
    import fix_owned_sky_mpc_refs as mpc_fix

    mpc_fix = importlib.reload(mpc_fix)
    mpc_fix.main()


def daynight_profile() -> dict:
    import copy

    prof = copy.deepcopy(profile_mod.LOBBY_DEFAULTS)
    prof["soft_objects"] = dict(paths.PATHS)
    sc = prof["sky_controller"]
    sc["bFrozenTime"] = False
    sc["bLocalDayCycle"] = True
    sc["DayLengthSeconds"] = DAY_CYCLE_SECONDS
    sc["LocalTimeOfDayHours"] = 8.0  # morning start — visible sun rise soon
    sc["MpcDayLengthSeconds"] = DAY_CYCLE_SECONDS
    sc["MpcNightLengthSeconds"] = 480.0
    sc["MpcDayStartTimeHours"] = 0.0
    sc["MpcShaderTimeOffsetHours"] = 12.0
    sc["SunriseHour"] = 6.0
    sc["SunsetHour"] = 18.0
    prof["adapter"] = {"bDriveFromGameState": False}
    prof["mpc_midday_hour"] = 8.0
    return prof


def save_profile(prof: dict) -> None:
    out = Path(unreal.Paths.project_dir()) / profile_mod.PROFILE_PATH.replace("/", "\\")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(prof, indent=2, ensure_ascii=False), encoding="utf-8")
    unreal.log(f"PROFILE saved {out}")


def apply_daynight_to_actor(actor, seconds: float = DAY_CYCLE_SECONDS) -> None:
    """Apply day/night cycle settings to a sky actor (not a sub-component)."""
    if not actor or not hasattr(actor, "get_class"):
        return
    if actor.get_class().get_name() == "MD_StylizedSkyControllerComponent":
        unreal.log_warning("apply_daynight_to_actor: pass sky actor, not SkyController component")
        return
    sky_comp.configure_editor_day_cycle(actor, seconds)
    sky_comp.set_sky_prop(actor, "LocalTimeOfDayHours", 8.0)
    sky_comp.set_sky_prop(actor, "MpcShaderTimeOffsetHours", 12.0)
    sky_comp.set_sky_prop(actor, "MpcDayStartTimeHours", 0.0)
    sky_comp.set_sky_prop(actor, "bSuppressLevelSkyActors", True)


def main() -> None:
    unreal.log("RESTORE_BP_SKY_DAYNIGHT start")
    ensure_owned_folders()
    dup_n = duplicate_owned_closure()
    fix_owned_mpc_refs()
    cloud_n = import_cloud_strips()
    wire_cloud_mi_textures()
    fix_owned_mpc_refs()
    recompile_cloud_materials()
    owned_assets.ensure_minimum_owned()

    prof = daynight_profile()
    save_profile(prof)

    import restore_bp_sky_full as restore

    restore = importlib.reload(restore)
    restore.main()

    # restore_bp_sky_full merge may re-harvest frozen settings — force day cycle profile last
    prof = daynight_profile()
    save_profile(prof)

    actor = restore.find_level_sky_actor()
    if actor:
        restore.apply_profile_to_target(
            sky_comp.get_sky_controller(actor) or actor, prof
        )
        restore.apply_profile_components(actor, prof)
        apply_daynight_to_actor(actor)
        try:
            actor.call_method("RefreshSkyNow")
        except Exception:
            pass
        try:
            actor.rerun_construction_scripts()
        except Exception:
            pass

    import fix_sky_dome_interior as dome

    dome = importlib.reload(dome)
    dome.run()

    bp = sky_comp.load_bp_sky_blueprint()
    if bp:
        cdo = unreal.get_default_object(bp.generated_class())
        apply_daynight_to_actor(cdo)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log_warning(f"compile: {e}")
        unreal.EditorAssetLibrary.save_asset(paths.BP_SKY)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(
        f"RESTORE_BP_SKY_DAYNIGHT_OK dup={dup_n} cloud_strips={cloud_n} "
        f"cycle={DAY_CYCLE_SECONDS}s frozen=False"
    )


if __name__ == "__main__":
    main()
