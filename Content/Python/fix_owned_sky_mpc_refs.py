"""Point owned sky materials at owned MPC_GlobalEnvironment (fixes shader GUID crash)."""
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

OWNED_MPC = paths.PATHS["EnvironmentMPC"]
SKY_ROOT = paths.SKY_ROOT


def owned_mpc():
    if not unreal.EditorAssetLibrary.does_asset_exist(OWNED_MPC):
        return None
    return unreal.EditorAssetLibrary.load_asset(OWNED_MPC)


def fix_collection_expr(expr, mpc) -> bool:
    changed = False
    try:
        col = expr.get_editor_property("collection")
    except Exception:
        return False
    if not col:
        return False
    col_path = col.get_path_name().split(".", 1)[0]
    if col_path == OWNED_MPC:
        return False
    if "MPC_GlobalEnvironment" in col_path or paths.is_sostylized_path(col_path):
        expr.set_editor_property("collection", mpc)
        return True
    return False


def fix_material(material, mpc) -> bool:
    changed = False
    for expr in unreal.MaterialEditingLibrary.get_material_expressions(material):
        if fix_collection_expr(expr, mpc):
            changed = True
    if changed:
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.EditorAssetLibrary.save_loaded_asset(material)
        unreal.log(f"FIX_MPC_REF {material.get_path_name()}")
    return changed


def fix_material_function(mf, mpc) -> bool:
    changed = False
    try:
        exprs = unreal.MaterialEditingLibrary.get_material_expressions(mf)
    except Exception as e:
        unreal.log_warning(f"skip MF {mf.get_path_name()}: {e}")
        return False
    for expr in exprs:
        if fix_collection_expr(expr, mpc):
            changed = True
    if changed:
        unreal.EditorAssetLibrary.save_loaded_asset(mf)
        unreal.log(f"FIX_MPC_REF MF {mf.get_path_name()}")
    return changed


def main() -> None:
    mpc = owned_mpc()
    if not mpc:
        raise RuntimeError(f"owned MPC missing: {OWNED_MPC}")

    sys.modules.pop("md_sky_import", None)
    import md_sky_import as md_sky_mpc

    md_sky_mpc = importlib.reload(md_sky_mpc)
    md_sky_mpc.set_mpc_asset_defaults(mpc, 8.0)
    md_sky_mpc.push_midday_scalars(mpc, midday=8.0, day_start=0.0, shader_offset=12.0)

    redirect = migrate.build_redirect_map()
    n = 0
    for pkg in migrate.list_owned_packages():
        migrate.fix_asset_refs(pkg, redirect)
        asset = unreal.EditorAssetLibrary.load_asset(pkg)
        if not asset:
            continue
        if isinstance(asset, unreal.Material):
            n += int(fix_material(asset, mpc))
        elif isinstance(asset, unreal.MaterialFunction):
            n += int(fix_material_function(asset, mpc))

    unreal.log(f"FIX_OWNED_SKY_MPC_REFS_OK touched={n}")


if __name__ == "__main__":
    main()
