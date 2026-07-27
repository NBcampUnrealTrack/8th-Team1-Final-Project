"""Move BP_Sky into Environment/Sky/ where content browser expects it."""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths", "md_sky_component"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths

paths = importlib.reload(paths)

OLD = "/Game/Developers/TaitoP/BP_Sky"
NEW = f"{paths.SKY_ROOT}/BP_Sky"


def main() -> None:
    if unreal.EditorAssetLibrary.does_asset_exist(NEW):
        unreal.log(f"BP_Sky already at {NEW}")
        return

    if not unreal.EditorAssetLibrary.does_asset_exist(OLD):
        raise RuntimeError(f"BP_Sky missing at {OLD} — run restore_bp_sky_full.py first")

    folder = paths.SKY_ROOT
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)

    ok = unreal.EditorAssetLibrary.rename_asset(OLD, NEW)
    if not ok:
        raise RuntimeError(f"rename failed {OLD} -> {NEW}")

    unreal.EditorAssetLibrary.save_asset(NEW)
    unreal.log(f"MOVE_BP_SKY_OK {OLD} -> {NEW}")


if __name__ == "__main__":
    main()
