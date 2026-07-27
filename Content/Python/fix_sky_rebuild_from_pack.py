"""
Rebuild broken sky redirectors: refresh from SoStylized pack copy, purge
StylizedStylized stubs, re-copy owned PATHS, then fix black sky.

Prerequisite: run once (or re-run) pack copy — see restore_sostylized_from_pack.ps1

Editor (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/fix_sky_rebuild_from_pack.py"
"""
from __future__ import annotations

import importlib
import sys
from pathlib import Path

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_owned_assets",
    "fix_lobby_sky_black_now",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_owned_assets as owned_assets

paths = importlib.reload(paths)
owned_assets = importlib.reload(owned_assets)

SKY_ROOT = paths.SKY_ROOT
STUB_MAX = 12000  # redirectors ~1-2KB; real MI ~20KB+, mesh ~100KB+

SRC_PREFIXES = (
    "/Game/Developers/TaitoP/SoStylized/Environment",
    "/Game/SoStylized/Environment",
)


def disk_size(pkg: str) -> int:
    rel = pkg.replace("/Game/", "Content/").split(".", 1)[0] + ".uasset"
    p = Path(unreal.Paths.project_dir()) / rel.replace("/", "\\")
    return p.stat().st_size if p.is_file() else 0


def is_redirector_or_stub(pkg: str) -> bool:
    name = pkg.rsplit("/", 1)[-1]
    if "StylizedStylized" in name or "StylizedStylized" in pkg:
        return True
    if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
        return False
    size = disk_size(pkg)
    if size and size < STUB_MAX:
        return True
    try:
        obj = unreal.EditorAssetLibrary.load_asset(pkg)
        if obj and obj.get_class().get_name() == "ObjectRedirector":
            return True
    except Exception:
        pass
    return False


def delete_package(pkg: str) -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
        return False
    ok = unreal.EditorAssetLibrary.delete_asset(pkg)
    unreal.log(f"DELETE {pkg} ok={ok} size={disk_size(pkg)}")
    return ok


def purge_broken_under(root: str) -> int:
    n = 0
    if not unreal.EditorAssetLibrary.does_directory_exist(root):
        return 0
    for item in unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False) or []:
        pkg = item.split(".", 1)[0]
        if is_redirector_or_stub(pkg):
            if delete_package(pkg):
                n += 1
    return n


def find_src_for_owned(owned: str) -> str | None:
    tail = owned
    if tail.startswith(SKY_ROOT + "/"):
        tail = "Sky/" + tail[len(SKY_ROOT) + 1 :]
    elif tail.startswith(paths.SKY_ROOT):
        tail = owned[len(paths.SKY_ROOT) + 1 :]
        tail = f"Sky/{tail}" if not tail.startswith("Sky/") else tail

    candidates = []
    for prefix in SRC_PREFIXES:
        if owned.endswith("MPC_GlobalEnvironment") or "/MPC_" in owned:
            candidates.append(f"{prefix.rsplit('/Environment', 1)[0]}/Environment/MPC_GlobalEnvironment")
        else:
            candidates.append(f"{prefix}/{tail.replace('Sky/Sky/', 'Sky/')}")

    for key, owned_path in paths.PATHS.items():
        if owned_path == owned:
            candidates = list(paths.FALLBACKS.get(key, ())) + candidates

    seen = set()
    for src in candidates:
        if src in seen:
            continue
        seen.add(src)
        if unreal.EditorAssetLibrary.does_asset_exist(src) and not is_redirector_or_stub(src):
            if disk_size(src) >= STUB_MAX or "MPC_" in src:
                return src
    return None


def force_rebuild_owned() -> int:
    n = 0
    for _key, owned in paths.PATHS.items():
        if is_redirector_or_stub(owned):
            delete_package(owned)
        src = find_src_for_owned(owned)
        if not src:
            unreal.log_warning(f"no clean source for {owned}")
            continue
        if unreal.EditorAssetLibrary.does_asset_exist(owned):
            delete_package(owned)
        if owned_assets.duplicate_to_owned(src, owned):
            n += 1
            unreal.log(f"REBUILD {src} -> {owned}")
    return n


def scan_registry() -> None:
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for p in (
        SKY_ROOT,
        "/Game/Developers/TaitoP/SoStylized",
        "/Game/SoStylized",
        "/Game/Developers/TaitoP",
    ):
        try:
            ar.scan_paths_synchronous([p], True)
        except Exception:
            pass


def main() -> None:
    unreal.log("FIX_SKY_REBUILD_FROM_PACK start")
    scan_registry()

    purged = purge_broken_under(SKY_ROOT)
    purged += purge_broken_under("/Game/Developers/TaitoP/SoStylized/Environment/Sky")
    purged += purge_broken_under("/Game/SoStylized/Environment/Sky")
    purged += purge_broken_under("/Game/SoStylized/Effects")
    rebuilt = force_rebuild_owned()
    scan_registry()

    import fix_lobby_sky_black_now as fix_black

    fix_black = importlib.reload(fix_black)
    fix_black.main()

    unreal.log(f"FIX_SKY_REBUILD_FROM_PACK_OK purged={purged} rebuilt={rebuilt}")


if __name__ == "__main__":
    main()
