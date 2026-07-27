"""
Promote lobby sky work from Developers/TaitoP → shared Content paths (git-ready).

Run once (PIE stopped, editor closed elsewhere):
  py "E:/Unreal/MarineDemolition/Content/Python/promote_sky_lobby_to_shared.py"

Targets:
  /Game/Developers/TaitoP/Environment/Sky  -> /Game/Environment/Sky
  /Game/Developers/TaitoP/BP_Sky             -> /Game/Environment/Sky/BP_MD_StylizedSky
  /Game/Developers/TaitoP/Landscape          -> /Game/Environment/Lobby/Landscape
  /Game/Developers/TaitoP/Maps/L_Lobby       -> /Game/Maps/L_Lobby

After run: update DefaultEngine.ini (script logs lines), fix redirectors in Content Browser.
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths", "md_sky_component"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)

# Post-promote canonical paths (must match md_sky_paths.py after edit)
SKY_ROOT = "/Game/Environment/Sky"
BP_SKY = f"{SKY_ROOT}/BP_MD_StylizedSky"
LEVEL = "/Game/Maps/L_Lobby"
LANDSCAPE_ROOT = "/Game/Environment/Lobby/Landscape"

MOVES: tuple[tuple[str, str, str], ...] = (
    ("dir", "/Game/Developers/TaitoP/Environment/Sky", SKY_ROOT),
    ("dir", "/Game/Developers/TaitoP/Landscape", LANDSCAPE_ROOT),
    ("asset", "/Game/Developers/TaitoP/BP_Sky", BP_SKY),
    ("asset", "/Game/Developers/TaitoP/Maps/L_Lobby", LEVEL),
)

OLD_DEV_PREFIX = "/Game/Developers/TaitoP"


def ensure_dir(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def rename_dir(src: str, dst: str) -> bool:
    if unreal.EditorAssetLibrary.does_directory_exist(dst):
        unreal.log(f"dir exists, per-asset move {src} -> {dst}")
        if not unreal.EditorAssetLibrary.does_directory_exist(src):
            return True
        ok_all = True
        for a in unreal.EditorAssetLibrary.list_assets(src, True, False):
            sp = a.split(".")[0]
            rel = sp[len(src) + 1 :]
            dp = f"{dst}/{rel}"
            ensure_dir(dp.rsplit("/", 1)[0])
            if unreal.EditorAssetLibrary.does_asset_exist(dp):
                unreal.log(f"  skip exists {dp}")
                continue
            ok = unreal.EditorAssetLibrary.rename_asset(sp, dp)
            unreal.log(f"  rename {sp} -> {dp} ok={ok}")
            ok_all = ok_all and ok
        return ok_all
    if not unreal.EditorAssetLibrary.does_directory_exist(src):
        unreal.log_warning(f"skip missing dir {src}")
        return True
    ensure_dir(dst.rsplit("/", 1)[0])
    ok = unreal.EditorAssetLibrary.rename_directory(src, dst)
    unreal.log(f"rename_dir {src} -> {dst} ok={ok}")
    return bool(ok)


def rename_asset(src: str, dst: str) -> bool:
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.log(f"already at {dst}")
        return True
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        unreal.log_warning(f"skip missing {src}")
        return True
    ensure_dir(dst.rsplit("/", 1)[0])
    ok = unreal.EditorAssetLibrary.rename_asset(src, dst)
    unreal.log(f"rename_asset {src} -> {dst} ok={ok}")
    return bool(ok)


def fixup_bp_sky_soft_refs() -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        return
    bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
    cdo = unreal.get_default_object(bp.generated_class())
    for key in paths.PATHS:
        owned = paths.PATHS[key]
        for candidate in paths.candidate_paths(key):
            if candidate.startswith(OLD_DEV_PREFIX):
                continue
            if not unreal.EditorAssetLibrary.does_asset_exist(candidate):
                continue
            asset = unreal.EditorAssetLibrary.load_asset(candidate)
            if asset:
                sky_comp.set_sky_prop(cdo, key, asset)
                break
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_asset(BP_SKY)


def refresh_level() -> None:
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception as e:
        unreal.log_warning(f"load_level: {e}")
        return
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        cls = actor.get_class().get_name()
        if "BP_MD_StylizedSky" in cls or "BP_Sky" in cls or actor.get_actor_label() == "BP_Sky":
            try:
                actor.call_method("RefreshSkyNow")
            except Exception:
                pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


def fix_redirectors(root: str) -> None:
    try:
        unreal.AssetToolsHelpers.get_asset_tools().fix_redirectors_in_folder(root)
        unreal.log(f"fix_redirectors {root}")
    except Exception:
        try:
            unreal.EditorAssetLibrary.fix_redirectors_in_folder(root)
            unreal.log(f"fix_redirectors {root}")
        except Exception as e:
            unreal.log_warning(f"redirectors {root}: {e}")


def run() -> None:
    n_ok = 0
    for kind, src, dst in MOVES:
        if kind == "dir":
            if rename_dir(src, dst):
                n_ok += 1
        else:
            if rename_asset(src, dst):
                n_ok += 1

    for root in (SKY_ROOT, LANDSCAPE_ROOT, "/Game/Maps"):
        fix_redirectors(root)

    fixup_bp_sky_soft_refs()
    refresh_level()

    unreal.log("PROMOTE_SKY_LOBBY_OK")
    unreal.log(f"  SKY_ROOT={SKY_ROOT}")
    unreal.log(f"  BP_SKY={BP_SKY}")
    unreal.log(f"  LEVEL={LEVEL}")
    unreal.log(f"  LANDSCAPE={LANDSCAPE_ROOT}")
    unreal.log("Update Config/DefaultEngine.ini:")
    unreal.log(f"  GameDefaultMap={LEVEL}.{LEVEL.rsplit('/', 1)[-1]}")
    unreal.log(f"  EditorStartupMap={LEVEL}.{LEVEL.rsplit('/', 1)[-1]}")
    unreal.log(f"moves_ok={n_ok}/{len(MOVES)}")


run()
