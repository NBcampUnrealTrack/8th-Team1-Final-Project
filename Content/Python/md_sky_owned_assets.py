"""Duplicate missing owned sky assets from SoStylized fallbacks."""
from __future__ import annotations

import unreal

import md_sky_paths as paths


def _ensure_dir(package_path: str) -> None:
    folder = package_path.rsplit("/", 1)[0]
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)


def duplicate_to_owned(src: str, owned: str) -> bool:
    if unreal.EditorAssetLibrary.does_asset_exist(owned):
        return True
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        return False
    _ensure_dir(owned)
    try:
        if unreal.EditorAssetLibrary.duplicate_asset(src, owned):
            unreal.EditorAssetLibrary.save_asset(owned)
            unreal.log(f"owned dup {src} -> {owned}")
            return True
    except Exception as e:
        unreal.log_warning(f"dup fail {src}: {e}")
    asset = unreal.EditorAssetLibrary.load_asset(src)
    if not asset:
        return False
    name = owned.rsplit("/", 1)[-1]
    folder = owned.rsplit("/", 1)[0]
    dup = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, folder, asset)
    if dup:
        unreal.EditorAssetLibrary.save_loaded_asset(dup)
        unreal.log(f"owned dup {src} -> {owned}")
        return True
    return False


def ensure_minimum_owned() -> int:
    """Copy PATHS entries from FALLBACKS when owned package is missing."""
    if not unreal.EditorAssetLibrary.does_directory_exist(paths.SKY_ROOT):
        unreal.EditorAssetLibrary.make_directory(paths.SKY_ROOT)
    n = 0
    for key, owned in paths.PATHS.items():
        if unreal.EditorAssetLibrary.does_asset_exist(owned):
            continue
        for src in paths.FALLBACKS.get(key, ()):
            if duplicate_to_owned(src, owned):
                n += 1
                break
        else:
            unreal.log_warning(f"ensure_owned: no source for {key} -> {owned}")
    if n:
        unreal.log(f"ensure_minimum_owned copied={n}")
    return n
