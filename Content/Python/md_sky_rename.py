"""Owned sky asset rename rules (SoStylized legacy names → project names)."""
from __future__ import annotations

import unreal

import md_sky_paths as paths

SKY_ROOT = paths.SKY_ROOT
CANONICAL_BP = paths.BP_SKY
DUPLICATE_BP = f"{paths.SKY_ROOT}/BP_Sky"

# Longest match first — order matters.
_NAME_REPLACEMENTS: tuple[tuple[str, str], ...] = (
    ("StylizedSkyDome_Clouds", "SkyDome_Clouds"),
    ("StylizedSkyDome", "SkyDome"),
    ("StylizedClouds", "Clouds"),
    ("StylizedSky", "Sky"),
    ("StylizedElectricity", "Electricity"),
    ("StylizedBasic", "SkyBasic"),
    ("StylizedFogPP", "SkyFogPP"),
    ("Backround", "Background"),
)


def proposed_asset_name(old_name: str) -> str | None:
    new = old_name
    for src, dst in _NAME_REPLACEMENTS:
        new = new.replace(src, dst)
    return new if new != old_name else None


def list_sky_package_paths(recursive: bool = True) -> list[str]:
    if not unreal.EditorAssetLibrary.does_directory_exist(SKY_ROOT):
        return []
    raw = unreal.EditorAssetLibrary.list_assets(SKY_ROOT, recursive, False) or []
    out: list[str] = []
    for item in raw:
        pkg = item.split(".", 1)[0]
        if pkg.startswith(SKY_ROOT):
            out.append(pkg)
    return sorted(set(out))


def build_rename_map() -> dict[str, str]:
    """old package path -> new package path under SKY_ROOT."""
    mapping: dict[str, str] = {}
    for pkg in list_sky_package_paths():
        folder, name = pkg.rsplit("/", 1)
        new_name = proposed_asset_name(name)
        if not new_name:
            continue
        dst = f"{folder}/{new_name}"
        if dst != pkg:
            mapping[pkg] = dst
    return mapping


def build_reverse_rename_map() -> dict[str, str]:
    """new package path -> old Stylized package path (undo rename_owned_sky_assets)."""
    mapping: dict[str, str] = {}
    for pkg in list_sky_package_paths():
        folder, name = pkg.rsplit("/", 1)
        old_name = name
        for src, dst in reversed(_NAME_REPLACEMENTS):
            old_name = old_name.replace(dst, src)
        if old_name != name:
            mapping[pkg] = f"{folder}/{old_name}"
    return mapping


def packages_to_rewrite_soft_refs() -> list[str]:
    extra = [CANONICAL_BP, DUPLICATE_BP, paths.LEVEL_LOBBY]
    pkgs = list_sky_package_paths() + [p for p in extra if unreal.EditorAssetLibrary.does_asset_exist(p)]
    return list(dict.fromkeys(pkgs))
