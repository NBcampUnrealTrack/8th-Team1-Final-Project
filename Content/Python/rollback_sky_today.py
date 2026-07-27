"""
Rollback today's sky work (safe): only undo SkyDome/MI renames, delete corrupt
double-Stylized junk, move BP_Sky back to /Developers/TaitoP/BP_Sky.

Does NOT touch curve/atlas names already using Stylized.

Run in open editor (PIE stopped, Save All first):
  py "E:/Unreal/MarineDemolition/Content/Python/rollback_sky_today.py"
"""
from __future__ import annotations

import importlib
import json
import sys
from pathlib import Path

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_component",
    "md_sky_bp_profile",
    "fix_bp_sky_authority",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp
import md_sky_bp_profile as profile_mod

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
profile_mod = importlib.reload(profile_mod)

SKY_ROOT = paths.SKY_ROOT
BP_ROOT = paths.BP_SKY
BP_ENV = f"{SKY_ROOT}/BP_Sky"
LEVEL = paths.LEVEL_LOBBY
PROFILE_FILE = Path(unreal.Paths.project_dir()) / profile_mod.PROFILE_PATH.replace("/", "\\")

# Only assets renamed today (Stylized -> Sky). Do not generic-reverse curves.
TODAY_REVERSE: dict[str, str] = {
    f"{SKY_ROOT}/Meshes/SM_SkyDome": f"{SKY_ROOT}/Meshes/SM_StylizedSkyDome",
    f"{SKY_ROOT}/Meshes/SM_SkyDome_Clouds": f"{SKY_ROOT}/Meshes/SM_StylizedSkyDome_Clouds",
    f"{SKY_ROOT}/Materials/Presets/MI_Sky_Classic": f"{SKY_ROOT}/Materials/Presets/MI_StylizedSky_Classic",
    f"{SKY_ROOT}/Materials/Presets/MI_Clouds_Classic": f"{SKY_ROOT}/Materials/Presets/MI_StylizedClouds_Classic",
    f"{SKY_ROOT}/Materials/M_Sky": f"{SKY_ROOT}/Materials/M_StylizedSky",
    f"{SKY_ROOT}/Materials/M_Clouds": f"{SKY_ROOT}/Materials/M_StylizedClouds",
}


def ensure_dir(package_path: str) -> None:
    folder = package_path.rsplit("/", 1)[0]
    if folder and not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)


def delete_corrupt_assets() -> int:
    deleted = 0
    if not unreal.EditorAssetLibrary.does_directory_exist(SKY_ROOT):
        return 0
    for item in unreal.EditorAssetLibrary.list_assets(SKY_ROOT, recursive=True, include_folder=False) or []:
        pkg = item.split(".", 1)[0]
        name = pkg.rsplit("/", 1)[-1]
        if "StylizedStylized" in name:
            if unreal.EditorAssetLibrary.delete_asset(pkg):
                unreal.log(f"DELETE corrupt {pkg}")
                deleted += 1
    return deleted


def reverse_today_renames() -> int:
    renamed = 0
    for src, dst in TODAY_REVERSE.items():
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            continue
        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            unreal.log_warning(f"REVERSE skip exists {dst}")
            continue
        ensure_dir(dst)
        ok = unreal.EditorAssetLibrary.rename_asset(src, dst)
        unreal.log(f"REVERSE {src} -> {dst} ok={ok}")
        if ok:
            renamed += 1
    return renamed


def duplicate_missing_from_sostylized() -> int:
    import md_sky_owned_assets as owned

    owned = importlib.reload(owned)
    return owned.ensure_minimum_owned()


def move_bp_to_root() -> bool:
    root_exists = unreal.EditorAssetLibrary.does_asset_exist(BP_ROOT)
    env_exists = unreal.EditorAssetLibrary.does_asset_exist(BP_ENV)

    if root_exists and not env_exists:
        unreal.log(f"BP_Sky already at {BP_ROOT}")
        return True
    if not env_exists and not root_exists:
        unreal.log_warning("BP_Sky missing — will recreate via authority pass")
        return False
    if env_exists and not root_exists:
        ensure_dir(BP_ROOT)
        ok = unreal.EditorAssetLibrary.rename_asset(BP_ENV, BP_ROOT)
        unreal.log(f"MOVE BP {BP_ENV} -> {BP_ROOT} ok={ok}")
        return ok

    root_bp = unreal.EditorAssetLibrary.load_asset(BP_ROOT)
    env_bp = unreal.EditorAssetLibrary.load_asset(BP_ENV)
    root_ok = bool(root_bp and hasattr(root_bp, "generated_class") and root_bp.generated_class())
    env_ok = bool(env_bp and hasattr(env_bp, "generated_class") and env_bp.generated_class())

    if root_ok and env_exists:
        ok = unreal.EditorAssetLibrary.delete_asset(BP_ENV)
        unreal.log(f"DELETE env duplicate {BP_ENV} ok={ok}")
        return True
    if env_ok and not root_ok:
        if root_exists:
            unreal.EditorAssetLibrary.delete_asset(BP_ROOT)
        ok = unreal.EditorAssetLibrary.rename_asset(BP_ENV, BP_ROOT)
        unreal.log(f"REPLACE root BP from env ok={ok}")
        return ok
    return root_ok or env_ok


def reset_profile() -> None:
    saved = None
    if PROFILE_FILE.is_file():
        try:
            saved = json.loads(PROFILE_FILE.read_text(encoding="utf-8"))
        except Exception:
            saved = None

    profile = dict(profile_mod.LOBBY_DEFAULTS)
    if saved:
        for section in ("sky_controller", "adapter", "components", "actor_transform", "mpc_midday_hour"):
            if section in saved and saved[section]:
                if isinstance(saved[section], dict):
                    profile.setdefault(section, {})
                    profile[section].update(saved[section])
                else:
                    profile[section] = saved[section]

    profile["soft_objects"] = dict(paths.PATHS)
    PROFILE_FILE.parent.mkdir(parents=True, exist_ok=True)
    PROFILE_FILE.write_text(json.dumps(profile, indent=2, ensure_ascii=False), encoding="utf-8")
    unreal.log(f"PROFILE reset Stylized paths -> {PROFILE_FILE}")


def ensure_lobby() -> None:
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception as e:
        unreal.log_warning(f"load_level: {e}")


def run_authority() -> None:
    import fix_bp_sky_authority as authority

    importlib.reload(authority)


def main() -> None:
    unreal.log("ROLLBACK_SKY_TODAY start (safe)")
    ensure_lobby()

    deleted = delete_corrupt_assets()
    renamed = reverse_today_renames()
    copied = duplicate_missing_from_sostylized()
    bp_ok = move_bp_to_root()
    reset_profile()

    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        ar.scan_paths_synchronous([SKY_ROOT, "/Game/Developers/TaitoP"], True)
    except Exception:
        pass

    run_authority()

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(
        "ROLLBACK_SKY_TODAY_OK "
        f"deleted_corrupt={deleted} reversed={renamed} copied={copied} bp_ok={bp_ok} "
        f"BP={BP_ROOT}"
    )


if __name__ == "__main__":
    main()
