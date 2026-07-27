"""
Restore SoStylized sky from D:\\UnrealPack\\Stylize\\SoStylized (disk copy + editor fix).

1) Close editor, run:
   powershell -ExecutionPolicy Bypass -File "E:/Unreal/MarineDemolition/Content/Python/restore_sostylized_from_pack.ps1"

2) Open editor, L_Lobby, PIE stopped:
   py "E:/Unreal/MarineDemolition/Content/Python/restore_sostylized_from_pack.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_owned_assets",
    "fix_sky_rebuild_from_pack",
    "restore_bp_sky_full",
):
    sys.modules.pop(_name, None)

PACK_GAME_PREFIX = "/Game/Developers/TaitoP/SoStylized/Environment"


def verify_pack_on_disk() -> bool:
    from pathlib import Path

    checks = (
        "Content/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome.uasset",
        "Content/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic.uasset",
    )
    root = Path(unreal.Paths.project_dir())
    ok = True
    for rel in checks:
        p = root / rel.replace("/", "\\")
        size = p.stat().st_size if p.is_file() else 0
        good = size > 12000
        unreal.log(f"PACK_CHECK {rel} size={size} ok={good}")
        ok = ok and good
    if not ok:
        unreal.log_warning(
            "SoStylized pack copy missing or stub — close editor and run "
            "Content/Python/restore_sostylized_from_pack.ps1 first"
        )
    return ok


def main() -> None:
    unreal.log("RESTORE_SOSTYLIZED_FROM_PACK start")
    verify_pack_on_disk()

    import fix_sky_rebuild_from_pack as rebuild

    rebuild = importlib.reload(rebuild)
    rebuild.main()

    # Ensure BP_Sky asset + level actor with saved profile
    import restore_bp_sky_full as restore

    restore = importlib.reload(restore)
    try:
        restore.main()
    except Exception as e:
        unreal.log_warning(f"restore_bp_sky_full: {e}")

    unreal.log("RESTORE_SOSTYLIZED_FROM_PACK_OK")


if __name__ == "__main__":
    main()
