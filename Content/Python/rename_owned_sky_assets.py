"""
Rename owned Environment/Sky assets to project naming (drop Stylized, fix typos).

Rules:
  - SM_StylizedSkyDome -> SM_SkyDome, M_StylizedSky -> M_Sky, etc.
  - T_BackroundClouds1A -> T_BackgroundClouds1A
  - Delete duplicate /Environment/Sky/BP_Sky (canonical BP at /Developers/TaitoP/BP_Sky)

Run (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/rename_owned_sky_assets.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_rename",
    "md_sky_component",
    "md_sky_import",
    "fix_bp_sky_authority",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_rename as rename_rules

paths = importlib.reload(paths)
rename_rules = importlib.reload(rename_rules)


def ensure_dir(package_path: str) -> None:
    folder = package_path.rsplit("/", 1)[0]
    if folder and not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)


def rewrite_soft_refs(redirect: dict[str, str]) -> None:
    if not redirect:
        return
    packages = []
    for p in rename_rules.packages_to_rewrite_soft_refs():
        try:
            pkg = unreal.load_package(p)
            if pkg:
                packages.append(pkg)
        except Exception:
            pass
    if not packages:
        return
    at = unreal.AssetToolsHelpers.get_asset_tools()
    soft_map = {
        unreal.SoftObjectPath(src): unreal.SoftObjectPath(dst)
        for src, dst in redirect.items()
    }
    try:
        at.rename_referencing_soft_object_paths(packages, soft_map)
        unreal.log(f"SOFT_REWRITE ok packages={len(packages)} pairs={len(soft_map)}")
    except Exception as e:
        unreal.log_warning(f"SOFT_REWRITE fail: {e}")


def rename_assets(mapping: dict[str, str]) -> tuple[int, int]:
    """Returns (renamed_count, skipped_count)."""
    pairs = sorted(mapping.items(), key=lambda item: item[0].count("/"), reverse=True)
    renamed = 0
    skipped = 0
    for src, dst in pairs:
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            skipped += 1
            continue
        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            unreal.log_warning(f"RENAME skip exists {dst}")
            skipped += 1
            continue
        ensure_dir(dst)
        ok = unreal.EditorAssetLibrary.rename_asset(src, dst)
        unreal.log(f"RENAME {src} -> {dst} ok={ok}")
        if ok:
            renamed += 1
        else:
            skipped += 1
    return renamed, skipped


def delete_duplicate_bp_sky() -> bool:
    dup = rename_rules.DUPLICATE_BP
    canon = paths.BP_SKY

    dup_exists = unreal.EditorAssetLibrary.does_asset_exist(dup)
    canon_exists = unreal.EditorAssetLibrary.does_asset_exist(canon)

    if not dup_exists:
        return False

    # Only duplicate lived under Environment/Sky — move to canonical path instead of delete.
    if not canon_exists:
        ensure_dir(canon)
        ok = unreal.EditorAssetLibrary.rename_asset(dup, canon)
        unreal.log(f"MOVE duplicate BP {dup} -> {canon} ok={ok}")
        return ok

    dup_bp = unreal.EditorAssetLibrary.load_asset(dup)
    canon_bp = unreal.EditorAssetLibrary.load_asset(canon)
    dup_ok = bool(dup_bp and hasattr(dup_bp, "generated_class") and dup_bp.generated_class())
    canon_ok = bool(canon_bp and hasattr(canon_bp, "generated_class") and canon_bp.generated_class())

    if not canon_ok and dup_ok:
        unreal.EditorAssetLibrary.delete_asset(canon)
        ok = unreal.EditorAssetLibrary.rename_asset(dup, canon)
        unreal.log(f"REPLACE broken canonical with duplicate {dup} -> {canon} ok={ok}")
        return ok

    if canon_ok and dup_exists:
        ok = unreal.EditorAssetLibrary.delete_asset(dup)
        unreal.log(f"DELETE duplicate {dup} ok={ok} (canonical valid)")
        return ok

    unreal.log_warning(f"KEEP duplicate {dup} — canonical state unclear")
    return False


def refresh_registry() -> None:
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.scan_paths_synchronous([paths.SKY_ROOT], True)
    except Exception:
        pass


def run_authority_pass() -> None:
    sys.modules.pop("fix_bp_sky_authority", None)
    import fix_bp_sky_authority  # noqa: F401 — module calls run() on import


def main() -> None:
    # Backup BP_Sky tuning before any asset moves/deletes.
    try:
        sys.modules.pop("export_bp_sky_profile", None)
        import export_bp_sky_profile as export_profile

        export_profile = importlib.reload(export_profile)
        export_profile.main()
    except Exception as e:
        unreal.log_warning(f"profile backup skipped: {e}")

    mapping = rename_rules.build_rename_map()
    unreal.log(f"RENAME_MAP count={len(mapping)}")
    for src, dst in sorted(mapping.items()):
        unreal.log(f"  plan {src} -> {dst}")

    renamed, skipped = rename_assets(mapping)
    rewrite_soft_refs(mapping)
    deleted_dup = delete_duplicate_bp_sky()
    refresh_registry()

    try:
        run_authority_pass()
    except Exception as e:
        unreal.log_warning(f"authority pass: {e}")

    unreal.log(
        "RENAME_OWNED_SKY_OK "
        f"renamed={renamed} skipped={skipped} "
        f"deleted_dup_bp={deleted_dup}"
    )


if __name__ == "__main__":
    main()
