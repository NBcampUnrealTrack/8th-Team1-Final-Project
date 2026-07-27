"""
Duplicate + rewrite the last SoStylized material-function / noise deps.
For FX bloat, run prune_sky_unused_owned.py after this (BP_Sky closure only).
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths", "migrate_bp_sky_off_sostylized"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths

paths = importlib.reload(paths)
import migrate_bp_sky_off_sostylized as migrate

migrate = importlib.reload(migrate)

SKY_ROOT = paths.SKY_ROOT
SRC = "/Game/Developers/TaitoP/SoStylized"

# Remaining closure deps (materials + rain FX pulled by fog/electricity chain).
EXPLICIT_SOURCES = (
    f"{SRC}/Materials/MF_Lerp_Five_Float1",
    f"{SRC}/Materials/MF_Lerp_Five_Float3",
    f"{SRC}/Materials/M_StylizedBasic",
    f"{SRC}/Environment/Sky/Materials/MF_DayCycleEmission",
    f"{SRC}/Environment/Sky/Materials/M_StylizedFogPP",
    f"{SRC}/Environment/Sky/Materials/M_SunCloudShadows_LF",
    f"{SRC}/Environment/Sky/Materials/MI_SunCloudShadows_LF",
    f"{SRC}/Textures/Noise/T_NoiseRough",
    f"{SRC}/Textures/Noise/T_NoiseRough02",
    f"{SRC}/Textures/Noise/T_NoiseSmooth",
    f"{SRC}/Effects/Materials/M_ElectricShockwave",
    f"{SRC}/Effects/Materials/MI_ElectricShockwave",
    f"{SRC}/Effects/Materials/MI_FoggySprite",
    f"{SRC}/Effects/Materials/M_StylizedElectricity",
    f"{SRC}/Effects/Materials/MI_StylizedElectricity",
    f"{SRC}/Effects/Materials/MI_RockDebris",
    f"{SRC}/Effects/Emitters/NE_RainFog",
    f"{SRC}/Effects/Systems/NS_RainWithoutSplash",
    f"{SRC}/Effects/Systems/NS_RainWithSplash",
    "/Game/SoStylized/Materials/MF_Lerp_Five_Float1",
    "/Game/SoStylized/Materials/MF_Lerp_Five_Float3",
    "/Game/SoStylized/Materials/M_StylizedBasic",
    "/Game/SoStylized/Textures/Noise/T_NoiseRough",
    "/Game/SoStylized/Textures/Noise/T_NoiseRough02",
    "/Game/SoStylized/Textures/Noise/T_NoiseSmooth",
    "/Game/SoStylized/Effects/Materials/M_ElectricShockwave",
    "/Game/SoStylized/Effects/Materials/MI_FoggySprite",
    "/Game/SoStylized/Effects/Materials/M_StylizedElectricity",
)

FX_SEED_ROOTS = (
    f"{SRC}/Environment/Sky/Materials/M_StylizedFogPP",
)

OWNED_FIX_ROOTS = (
    f"{SKY_ROOT}/Materials/M_StylizedClouds",
    f"{SKY_ROOT}/Materials/MF_CloudLayer",
    f"{SKY_ROOT}/Materials/M_StylizedSky",
    f"{SKY_ROOT}/Materials/M_StylizedBasic",
    f"{SKY_ROOT}/Materials/M_StylizedFogPP",
    f"{SKY_ROOT}/Materials/M_SunCloudShadows_LF",
    f"{SKY_ROOT}/Materials/MI_SunCloudShadows_LF",
    f"{SKY_ROOT}/Materials/Presets/MI_StylizedSky_Classic",
    f"{SKY_ROOT}/Materials/Presets/MI_StylizedClouds_Classic",
)


def collect_fx_deps(roots: tuple[str, ...]) -> list[str]:
    """Collect SoStylized deps including /Effects/ (main migrate excludes these)."""
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    for prop, val in (
        ("include_soft_package_references", True),
        ("include_hard_package_references", True),
        ("include_soft_path_references", True),
    ):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    seen: set[str] = set()
    stack = [r for r in roots if unreal.EditorAssetLibrary.does_asset_exist(r)]
    while stack:
        p = stack.pop()
        if p in seen:
            continue
        seen.add(p)
        try:
            deps = ar.get_dependencies(unreal.Name(p), opts) or []
        except Exception:
            continue
        for d in deps:
            s = str(d)
            if paths.is_sostylized_path(s):
                stack.append(s)
    return sorted(seen)


def ensure_sky_dirs():
    for sub in (
        "Materials",
        "Textures",
        "Textures/Noise",
        "Effects",
        "Effects/Materials",
        "Effects/Systems",
        "Effects/Emitters",
        "Effects/Meshes",
        "Effects/Textures",
    ):
        p = f"{SKY_ROOT}/{sub}"
        if not unreal.EditorAssetLibrary.does_directory_exist(p):
            unreal.EditorAssetLibrary.make_directory(p)


def run():
    ensure_sky_dirs()
    bp = migrate.find_bp_sky_path()
    roots = [bp] + migrate.all_bp_sky_paths()
    before = migrate.collect_sostylized_leaks(roots)
    unreal.log(f"FIX_SKY_LEAKS before={len(before)}")

    duped = 0
    all_sources = list(dict.fromkeys([*EXPLICIT_SOURCES, *collect_fx_deps(FX_SEED_ROOTS)]))
    for src in all_sources:
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            continue
        try:
            dst = paths.map_src_to_owned(src)
        except ValueError as e:
            unreal.log_warning(f"skip {src}: {e}")
            continue
        if migrate.duplicate_one(src):
            duped += 1
            unreal.log(f"EXPLICIT DUP {src} -> {dst}")
    unreal.log(f"explicit_duped={duped}")

    migrate.refresh_owned_registry()
    redirect = migrate.build_redirect_map()

    migrate.rewrite_soft_refs(redirect, roots)
    for pkg in migrate.list_owned_packages():
        migrate.fix_asset_refs(pkg, redirect)
    for root in OWNED_FIX_ROOTS:
        migrate.fix_asset_refs(root, redirect)
    for bp_path in roots:
        migrate.fix_asset_refs(bp_path, redirect)

    leaks = migrate.collect_sostylized_leaks(roots)
    consolidated = migrate.consolidate_leak_list(leaks, redirect)
    consolidated += migrate.consolidate_all_owned_twins(redirect)
    unreal.log(f"consolidated={consolidated}")

    migrate.rewrite_soft_refs(redirect, roots)
    leaks = migrate.collect_sostylized_leaks(roots)
    if leaks:
        consolidated += migrate.consolidate_leak_list(leaks, redirect)
        unreal.log(f"consolidated_pass2={consolidated}")

    leaks = migrate.finish_remaining_leaks(roots, max_rounds=3)

    for root in OWNED_FIX_ROOTS:
        asset = unreal.EditorAssetLibrary.load_asset(root)
        if not asset:
            continue
        if isinstance(asset, unreal.Material):
            unreal.MaterialEditingLibrary.recompile_material(asset)
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
        elif isinstance(asset, unreal.MaterialInstanceConstant):
            unreal.EditorAssetLibrary.save_loaded_asset(asset)

    if leaks:
        unreal.log_warning(f"FIX_SKY_LEAKS_FINAL remaining={len(leaks)} (Developers/TaitoP/SoStylized)")
        for L in leaks:
            unreal.log_warning(f"  STILL {L}")
        unreal.log("Run prune_sky_unused_owned.py then re-check leaks")
    else:
        unreal.log("FIX_SKY_LEAKS_FINAL_OK leaks=0 — run prune_sky_unused_owned.py to drop unreferenced FX")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


run()
