"""
Simple undo: reverse Stylized renames, restore weather sounds, recreate BP_Sky.

Run in editor (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/restore_deleted_sky_simple.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_rename",
    "md_sky_owned_assets",
    "restore_bp_sky_full",
    "md_sky_import",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_rename as rename_rules
import md_sky_owned_assets as owned_assets

paths = importlib.reload(paths)
rename_rules = importlib.reload(rename_rules)
owned_assets = importlib.reload(owned_assets)

SKY_ROOT = paths.SKY_ROOT
SOUND_SOURCES = (
    "/Game/SoStylized/Sounds/Weather",
    "/Game/SoStylized/Sounds",
    "/Game/Developers/TaitoP/SoStylized/Sounds/Weather",
    "/Game/Developers/TaitoP/SoStylized/Sounds",
)
SOUND_DESTS = (
    f"{SKY_ROOT}/Sounds/Weather",
    f"{SKY_ROOT}/Sounds",
    f"{paths.DELETABLE_SRC_PREFIX}/Sounds/Weather",
    f"{paths.DELETABLE_SRC_PREFIX}/Sounds",
)


def ensure_dir(package_path: str) -> None:
    folder = package_path.rsplit("/", 1)[0]
    if folder and not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)


def rewrite_soft_refs(redirect: dict[str, str]) -> None:
    if not redirect:
        return
    packages = []
    for pkg in rename_rules.packages_to_rewrite_soft_refs():
        try:
            loaded = unreal.load_package(pkg)
            if loaded:
                packages.append(loaded)
        except Exception:
            pass
    if not packages:
        return
    soft_map = {
        unreal.SoftObjectPath(src): unreal.SoftObjectPath(dst)
        for src, dst in redirect.items()
    }
    try:
        at = unreal.AssetToolsHelpers.get_asset_tools()
        at.rename_referencing_soft_object_paths(packages, soft_map)
        unreal.log(f"SOFT_REWRITE ok pairs={len(soft_map)}")
    except Exception as e:
        unreal.log_warning(f"SOFT_REWRITE fail: {e}")


def reverse_renames() -> int:
    mapping = rename_rules.build_reverse_rename_map()
    if not mapping:
        unreal.log("REVERSE_RENAME nothing to undo")
        return 0

    renamed = 0
    for src, dst in sorted(mapping.items(), key=lambda item: item[0].count("/"), reverse=True):
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
    rewrite_soft_refs(mapping)
    return renamed


def duplicate_tree(src_root: str, dst_root: str) -> int:
    if not unreal.EditorAssetLibrary.does_directory_exist(src_root):
        return 0
    if not unreal.EditorAssetLibrary.does_directory_exist(dst_root):
        unreal.EditorAssetLibrary.make_directory(dst_root)

    copied = 0
    for item in unreal.EditorAssetLibrary.list_assets(src_root, recursive=True, include_folder=False) or []:
        pkg = item.split(".", 1)[0]
        rel = pkg[len(src_root) :].lstrip("/")
        dst = f"{dst_root}/{rel}" if rel else dst_root
        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            continue
        if owned_assets.duplicate_to_owned(pkg, dst):
            copied += 1
    return copied


def restore_sounds() -> int:
    copied = 0
    seen: set[str] = set()
    for src, dst in zip(SOUND_SOURCES, SOUND_DESTS):
        key = (src, dst)
        if key in seen:
            continue
        seen.add(key)
        copied += duplicate_tree(src, dst)
    return copied


def main() -> None:
    unreal.log("RESTORE_DELETED_SKY_SIMPLE start")

    renamed = reverse_renames()
    owned = owned_assets.ensure_minimum_owned()
    sounds = restore_sounds()

    import restore_bp_sky_full as restore

    restore = importlib.reload(restore)
    restore.main()

    unreal.log(
        "RESTORE_DELETED_SKY_SIMPLE_OK "
        f"reversed={renamed} owned_copied={owned} sounds_copied={sounds}"
    )


if __name__ == "__main__":
    main()
