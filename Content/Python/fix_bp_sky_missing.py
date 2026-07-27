"""
BP_Sky missing/broken (2KB redirector): delete stubs, recreate at
/Developers/TaitoP/BP_Sky, spawn in L_Lobby with saved lobby profile.

Run in editor (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/fix_bp_sky_missing.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_component",
    "restore_bp_sky_full",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)

BP_ROOT = paths.BP_SKY
BP_ENV = f"{paths.SKY_ROOT}/BP_Sky"
STUB_MAX_BYTES = 8000  # real BP_Sky is ~30KB+; stubs ~2KB


def package_size(pkg: str) -> int:
    rel = pkg.replace("/Game/", "Content/") + ".uasset"
    full = unreal.Paths.project_content_dir() + rel[len("Content/") :]
    try:
        from pathlib import Path

        p = Path(unreal.Paths.project_dir()) / rel.replace("/", "\\")
        if p.is_file():
            return p.stat().st_size
    except Exception:
        pass
    return 0


def is_valid_bp_path(pkg: str) -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
        return False
    bp = unreal.EditorAssetLibrary.load_asset(pkg)
    try:
        return bool(bp and hasattr(bp, "generated_class") and bp.generated_class())
    except Exception:
        return False


def delete_if_stub(pkg: str) -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
        return False
    if is_valid_bp_path(pkg):
        unreal.log(f"KEEP valid BP {pkg}")
        return False
    size = package_size(pkg)
    unreal.log(f"DELETE broken BP {pkg} size={size}")
    return unreal.EditorAssetLibrary.delete_asset(pkg)


def main() -> None:
    unreal.log("FIX_BP_SKY_MISSING start")

    for pkg in (BP_ROOT, BP_ENV, f"{paths.SKY_ROOT}/BP_StylizedSky"):
        delete_if_stub(pkg)

    import restore_bp_sky_full as restore

    restore = importlib.reload(restore)
    restore.main()

    found = sky_comp.find_bp_sky_path()
    unreal.log(f"FIX_BP_SKY_MISSING_OK bp_path={found} expected={BP_ROOT}")


if __name__ == "__main__":
    main()
