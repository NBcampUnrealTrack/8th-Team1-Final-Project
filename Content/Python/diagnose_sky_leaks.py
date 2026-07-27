"""Print SoStylized dependency leaks from BP_Sky → Environment/Sky closure."""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths", "md_sky_component"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths

paths = importlib.reload(paths)

SKY_ROOT = paths.SKY_ROOT
SRC_PREFIXES = paths.SRC_PREFIXES


def find_bp_sky_path() -> str:
    for p in (paths.BP_SKY, f"{SKY_ROOT}/BP_Sky"):
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            return p
    return paths.BP_SKY


def collect_leaks(root: str) -> list[str]:
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
    leaks = []
    stack = [root]
    seen = set()
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
            if paths.is_deletable_sostylized_path(s):
                leaks.append(s)
            elif s.startswith(SKY_ROOT) and s not in seen:
                stack.append(s)
    return sorted(set(leaks))


def run():
    bp = find_bp_sky_path()
    leaks = collect_leaks(bp)
    unreal.log(f"DIAGNOSE root={bp} leaks={len(leaks)}")
    for L in leaks:
        owned = ""
        try:
            owned = paths.map_src_to_owned(L)
            exists = unreal.EditorAssetLibrary.does_asset_exist(owned)
            unreal.log(f"  LEAK {L}  owned={owned} exists={exists}")
        except ValueError as e:
            unreal.log_warning(f"  LEAK {L}  unmapped ({e})")
    unreal.log("DIAGNOSE_SKY_LEAKS_OK")


run()
