"""
Delete /Game/Environment assets not reachable from L_Lobby (+ BP_MD_StylizedSky closure).

Scope: shared global folders only (not Developers/).

  py "E:/Unreal/MarineDemolition/Content/Python/prune_environment_unreferenced_by_lobby.py"
  py "E:/Unreal/MarineDemolition/Content/Python/prune_environment_unreferenced_by_lobby.py" --dry-run
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths",):
    sys.modules.pop(_name, None)

import md_sky_paths as paths

paths = importlib.reload(paths)

ENV_ROOT = "/Game/Environment"
LEVEL = paths.LEVEL_LOBBY
BP_SKY = paths.BP_SKY
SKY_ROOT = paths.SKY_ROOT
LANDSCAPE_ROOT = paths.LANDSCAPE_LOBBY

DRY_RUN = "--dry-run" in sys.argv


def _dep_options() -> unreal.AssetRegistryDependencyOptions:
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


def _pkg_from_object(obj) -> str | None:
    if not obj:
        return None
    try:
        return obj.get_path_name().split(".")[0]
    except Exception:
        return None


def collect_level_placed_refs(level_path: str) -> set[str]:
    """Hard refs from actors/components placed in the level (landscape, meshes, BPs)."""
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(level_path)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    refs: set[str] = set()
    for actor in eas.get_all_level_actors():
        cls = actor.get_class().get_name()
        bp_pkg = _pkg_from_object(actor.get_class())
        if bp_pkg and bp_pkg.startswith("/Game/"):
            refs.add(bp_pkg)

        if cls == "Landscape":
            for prop in ("landscape_material", "landscape_hole_material"):
                try:
                    mat = actor.get_editor_property(prop)
                    pkg = _pkg_from_object(mat)
                    if pkg:
                        refs.add(pkg)
                except Exception:
                    pass
            try:
                info = actor.get_landscape_info()
            except Exception:
                info = None
            if info:
                for prop in ("layers", "layer_info_map", "target_layers"):
                    try:
                        val = info.get_editor_property(prop)
                    except Exception:
                        continue
                    if isinstance(val, dict):
                        for v in val.values():
                            pkg = _pkg_from_object(v)
                            if pkg:
                                refs.add(pkg)
                    elif isinstance(val, (list, tuple)):
                        for v in val:
                            pkg = _pkg_from_object(v)
                            if pkg:
                                refs.add(pkg)

        for comp in actor.get_components_by_class(unreal.ActorComponent):
            if isinstance(comp, unreal.StaticMeshComponent):
                try:
                    mesh = comp.get_editor_property("static_mesh")
                except Exception:
                    mesh = None
                pkg = _pkg_from_object(mesh)
                if pkg:
                    refs.add(pkg)
                try:
                    n = comp.get_num_materials()
                except Exception:
                    n = 0
                for idx in range(n):
                    try:
                        mat = comp.get_material(idx)
                    except Exception:
                        mat = None
                    pkg = _pkg_from_object(mat)
                    if pkg:
                        refs.add(pkg)
            elif isinstance(comp, unreal.LandscapeComponent):
                try:
                    pkg = _pkg_from_object(comp.get_editor_property("override_material"))
                    if pkg:
                        refs.add(pkg)
                except Exception:
                    pass
    return refs


def collect_closure(roots: list[str]) -> set[str]:
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = _dep_options()
    seen: set[str] = set()
    stack = [r for r in roots if unreal.EditorAssetLibrary.does_asset_exist(r)]
    while stack:
        pkg = stack.pop()
        if pkg in seen:
            continue
        seen.add(pkg)
        try:
            deps = ar.get_dependencies(unreal.Name(pkg), opts) or []
        except Exception:
            continue
        for dep in deps:
            s = str(dep)
            if s not in seen:
                stack.append(s)
    return seen


def expand_required_from_level(level_path: str, required: set[str]) -> set[str]:
    placed = collect_level_placed_refs(level_path)
    unreal.log(f"PRUNE level placed refs={len(placed)}")
    for p in sorted(placed):
        unreal.log(f"  PLACED {p}")
    if placed:
        required |= collect_closure(list(placed))
    return {p for p in required if p.startswith(ENV_ROOT + "/") or p == ENV_ROOT}


def add_material_parent_chain(required: set[str], mi_path: str | None) -> None:
    if not mi_path or not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return
    stack = [mi_path.split(".")[0]]
    seen: set[str] = set()
    while stack:
        pkg = stack.pop()
        if pkg in seen:
            continue
        seen.add(pkg)
        if pkg.startswith(ENV_ROOT):
            required.add(pkg)
        asset = unreal.EditorAssetLibrary.load_asset(pkg)
        if not isinstance(asset, unreal.MaterialInstanceConstant):
            continue
        try:
            parent = asset.get_editor_property("parent")
        except Exception:
            continue
        if not parent:
            continue
        pp = parent.get_path_name().split(".")[0]
        if pp.startswith(ENV_ROOT) and pp not in seen:
            stack.append(pp)


def list_environment_packages() -> list[str]:
    if not unreal.EditorAssetLibrary.does_directory_exist(ENV_ROOT):
        return []
    assets = unreal.EditorAssetLibrary.list_assets(ENV_ROOT, True, False)
    return sorted({a.split(".")[0] for a in assets})


def delete_packages(packages: list[str]) -> int:
    n = 0
    for pkg in sorted(packages, key=lambda p: p.count("/"), reverse=True):
        if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
            continue
        if DRY_RUN:
            unreal.log(f"PRUNE DRY DEL {pkg}")
            n += 1
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


def run() -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        raise RuntimeError(f"missing level {LEVEL}")

    roots = [LEVEL]
    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        roots.append(BP_SKY)

    closure = collect_closure(roots)
    required = {p for p in closure if p.startswith(ENV_ROOT + "/") or p == ENV_ROOT}

    for p in paths.PATHS.values():
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            required.add(p.split(".")[0])
    for p in paths.CLOUD_LAYER_PATHS:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            required.add(p.split(".")[0])
    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        required.add(BP_SKY)

    add_material_parent_chain(required, paths.PATHS.get("SkyMaterial"))
    add_material_parent_chain(required, paths.PATHS.get("CloudMaterial"))
    required = expand_required_from_level(LEVEL, required)

    owned = list_environment_packages()
    to_delete = [p for p in owned if p not in required]

    unreal.log(
        f"PRUNE_ENV roots={roots} required={len(required)} owned={len(owned)} "
        f"delete={len(to_delete)} dry_run={DRY_RUN}"
    )
    for p in sorted(required):
        unreal.log(f"  KEEP {p}")
    for p in to_delete:
        unreal.log(f"  DEL  {p}")

    deleted = delete_packages(to_delete)

    if not DRY_RUN:
        for root in (SKY_ROOT, LANDSCAPE_ROOT, ENV_ROOT):
            if unreal.EditorAssetLibrary.does_directory_exist(root):
                try:
                    unreal.AssetToolsHelpers.get_asset_tools().fix_up_redirectors_in_folder(root)
                except Exception:
                    pass
        try:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        except Exception as e:
            unreal.log_warning(f"save level: {e}")

    unreal.log(f"PRUNE_ENV_UNREFERENCED_OK deleted={deleted}")


run()
