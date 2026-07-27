"""
Delete Environment/Sky assets NOT reachable from BP_Sky (Classic lobby closure).

Keeps paths.PATHS + transitive SKY_ROOT deps from BP_Sky.
Run after migrate / fix_sky_leaks_final when FX bloat was duplicated by mistake.

  py "E:/Unreal/MarineDemolition/Content/Python/prune_sky_unused_owned.py"
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

# Classic BP_Sky does not use owned weather FX — always eligible for deletion.
ALWAYS_PRUNE_PREFIXES = (
    f"{SKY_ROOT}/Effects/",
)


def _dep_options():
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


def collect_closure_roots(roots: list[str]) -> set[str]:
    """All package paths reachable from roots (full dependency walk)."""
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = _dep_options()
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
            if s not in seen:
                stack.append(s)
    return seen


def collect_required_sky_packages(bp_paths: list[str]) -> set[str]:
    closure = collect_closure_roots(bp_paths)
    required: set[str] = set()
    for p in closure:
        if p.startswith(SKY_ROOT + "/") or p == SKY_ROOT:
            required.add(p)
    for p in paths.PATHS.values():
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            required.add(p)
    for p in paths.CLOUD_LAYER_PATHS:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            required.add(p)
    # Never keep duplicated weather FX under Environment/Sky/Effects/
    required = {p for p in required if not any(p.startswith(x) for x in ALWAYS_PRUNE_PREFIXES)}
    # Materials parent chain for Classic MI
    for mi in (
        paths.PATHS.get("SkyMaterial"),
        paths.PATHS.get("CloudMaterial"),
    ):
        if not mi or not unreal.EditorAssetLibrary.does_asset_exist(mi):
            continue
        required.add(mi.split(".")[0])
        stack = [mi]
        seen = set()
        while stack:
            pkg = stack.pop()
            if pkg in seen:
                continue
            seen.add(pkg)
            if pkg.startswith(SKY_ROOT):
                required.add(pkg.split(".")[0])
            asset = unreal.EditorAssetLibrary.load_asset(pkg)
            if isinstance(asset, unreal.MaterialInstanceConstant):
                try:
                    parent = asset.get_editor_property("parent")
                    if parent:
                        pp = parent.get_path_name().split(".")[0]
                        if pp.startswith(SKY_ROOT):
                            required.add(pp)
                            stack.append(pp)
                except Exception:
                    pass
    return required


def list_owned_sky_packages() -> list[str]:
    if not unreal.EditorAssetLibrary.does_directory_exist(SKY_ROOT):
        return []
    assets = unreal.EditorAssetLibrary.list_assets(SKY_ROOT, True, False)
    return sorted({a.split(".")[0] for a in assets})


def delete_packages(packages: list[str]) -> int:
    n = 0
    for pkg in sorted(packages, key=lambda p: p.count("/"), reverse=True):
        if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
            continue
        try:
            if unreal.EditorAssetLibrary.delete_asset(pkg):
                unreal.log(f"PRUNE DELETE {pkg}")
                n += 1
            else:
                unreal.log_warning(f"PRUNE delete failed {pkg}")
        except Exception as e:
            unreal.log_warning(f"PRUNE delete error {pkg}: {e}")
    return n


def run():
    bp_paths = migrate.all_bp_sky_paths()
    if not bp_paths:
        raise RuntimeError("BP_Sky not found")

    required = collect_required_sky_packages(bp_paths)
    owned = list_owned_sky_packages()
    to_delete = [p for p in owned if p not in required]

    unreal.log(f"PRUNE required={len(required)} owned={len(owned)} delete={len(to_delete)}")
    for p in sorted(required):
        unreal.log(f"  KEEP {p}")
    for p in to_delete:
        unreal.log(f"  DEL  {p}")

    deleted = delete_packages(to_delete)
    migrate.refresh_owned_registry()

    leaks = migrate.collect_sostylized_leaks(bp_paths)
    unreal.log(f"PRUNE deleted={deleted} sostylized_leaks={len(leaks)}")
    for L in leaks:
        unreal.log_warning(f"  LEAK {L}")

    if not leaks:
        unreal.log("PRUNE_SKY_UNUSED_OK — safe to delete Content/Developers/TaitoP/SoStylized")
    else:
        unreal.log_warning("PRUNE done — fix remaining leaks or consolidate SoStylized twins")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


run()
