"""Fix last 3 SoStylized texture leaks via replace_material_texture + delete twins."""
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

LEAKS = (
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/BackgroundClouds/T_BackroundClouds1A",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers/T_CloudLayer03",
    "/Game/Developers/TaitoP/SoStylized/Textures/Noise/T_NoiseRough",
)

SKY_ROOT = paths.SKY_ROOT
DELETABLE = paths.DELETABLE_SRC_PREFIX


def _registry_opts():
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
    return opts


def referencers(package: str) -> list[str]:
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        refs = ar.get_referencers(unreal.Name(package), _registry_opts()) or []
    except Exception:
        return []
    return sorted({str(r) for r in refs})


def sky_materials() -> list[str]:
    out = []
    for pkg in migrate.list_owned_packages():
        if not pkg.startswith(f"{SKY_ROOT}/Materials"):
            continue
        asset = unreal.EditorAssetLibrary.load_asset(pkg)
        if isinstance(asset, (unreal.Material, unreal.MaterialFunction, unreal.MaterialInstanceConstant)):
            out.append(pkg)
    return sorted(out)


def replace_texture_everywhere(old_path: str, new_path: str) -> int:
    old_tex = unreal.EditorAssetLibrary.load_asset(old_path)
    new_tex = unreal.EditorAssetLibrary.load_asset(new_path)
    if not old_tex or not new_tex:
        unreal.log_warning(f"missing tex old={old_path} new={new_path}")
        return 0
    n = 0
    if not hasattr(unreal.MaterialEditingLibrary, "replace_material_texture"):
        unreal.log_warning("replace_material_texture unavailable")
        return 0
    for pkg in sky_materials():
        asset = unreal.EditorAssetLibrary.load_asset(pkg)
        if isinstance(asset, unreal.Material):
            try:
                if unreal.MaterialEditingLibrary.replace_material_texture(asset, old_tex, new_tex):
                    unreal.MaterialEditingLibrary.recompile_material(asset)
                    unreal.EditorAssetLibrary.save_loaded_asset(asset)
                    unreal.log(f"REPLACE_TEX {pkg}")
                    n += 1
            except Exception as e:
                unreal.log_warning(f"replace {pkg}: {e}")
        elif isinstance(asset, unreal.MaterialFunction):
            try:
                if unreal.MaterialEditingLibrary.replace_material_texture(asset, old_tex, new_tex):
                    unreal.EditorAssetLibrary.save_loaded_asset(asset)
                    unreal.log(f"REPLACE_TEX_MF {pkg}")
                    n += 1
            except Exception as e:
                unreal.log_warning(f"replace mf {pkg}: {e}")
        elif isinstance(asset, unreal.MaterialInstanceConstant):
            changed = False
            try:
                for tp in list(asset.texture_parameter_values):
                    tex = tp.get_editor_property("parameter_value")
                    if tex and tex.get_path_name().split(".")[0] == old_path:
                        tp.set_editor_property("parameter_value", new_tex)
                        changed = True
            except Exception:
                pass
            if changed:
                unreal.EditorAssetLibrary.save_loaded_asset(asset)
                unreal.log(f"REPLACE_TEX_MI {pkg}")
                n += 1
    return n


def delete_leak(leak: str) -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(leak):
        return True
    refs = referencers(leak)
    blockers = [r for r in refs if not r.startswith(DELETABLE + "/") and r != DELETABLE]
    if blockers:
        unreal.log_warning(f"DELETE blocked {leak} ({len(blockers)} refs)")
        for b in blockers[:12]:
            unreal.log_warning(f"  ref-by {b}")
        return False
    ok = unreal.EditorAssetLibrary.delete_asset(leak)
    if ok:
        unreal.log(f"DELETE {leak}")
    return bool(ok)


def run():
    bp = migrate.find_bp_sky_path()
    roots = [bp] + migrate.all_bp_sky_paths()
    redirect = migrate.build_redirect_map()

    pairs = []
    for leak in LEAKS:
        owned = paths.map_src_to_owned(leak)
        redirect[leak] = owned
        pairs.append((leak, owned))
        if not unreal.EditorAssetLibrary.does_asset_exist(owned):
            migrate.duplicate_one(leak)

    migrate.rewrite_soft_refs(redirect, roots)

    replaced = 0
    for old, new in pairs:
        unreal.log(f"PAIR {old} -> {new}")
        for r in referencers(old):
            unreal.log(f"  ref-by {r}")
        replaced += replace_texture_everywhere(old, new)

    unreal.log(f"replace_material_texture hits={replaced}")

    for old, new in pairs:
        owned_obj = unreal.EditorAssetLibrary.load_asset(new)
        old_obj = unreal.EditorAssetLibrary.load_asset(old)
        if owned_obj and old_obj:
            try:
                unreal.EditorAssetLibrary.consolidate_assets(owned_obj, [old_obj])
                unreal.log(f"CONSOLIDATE {old} -> {new}")
            except Exception as e:
                unreal.log_warning(f"consolidate {old}: {e}")

    for old, _ in pairs:
        delete_leak(old)

    migrate.refresh_owned_registry()
    leaks = migrate.collect_sostylized_leaks(roots)
    unreal.log(f"FIX_LAST3 remaining={len(leaks)}")
    for L in leaks:
        unreal.log_warning(f"  STILL {L}")

    if not leaks:
        unreal.log("FIX_LAST3_OK — delete Content/Developers/TaitoP/SoStylized")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


run()
