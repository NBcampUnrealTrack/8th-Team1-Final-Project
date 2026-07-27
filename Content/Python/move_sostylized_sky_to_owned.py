"""
COMPLETE ownership transfer (run AFTER Unreal editor restart).

Strategy (safe, no consolidate):
  1) Delete temporary Owned duplicates
  2) MOVE (rename) SoStylized sky/lighting trees into Owned paths
     so hard refs update and SoStylized folder can be deleted later
  3) Place LobbyStylizedSky from Owned BP, freeze midday, force lights

Does not open dialogs. Avoids consolidate_assets (crashed editor previously).
"""
from __future__ import annotations

import unreal

SRC = "/Game/Developers/TaitoP/SoStylized"
DST = "/Game/Developers/TaitoP/Environment/Sky/Owned"
OWNED_BP = f"{DST}/Environment/Sky/BP_StylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

# Directories / assets to MOVE into Owned (relative to SRC)
MOVE_DIRS = [
    "Environment/Sky",
    "Effects",
    "Sounds",
    "Textures",
    "Materials",
    "Demo",
]
MOVE_ASSETS = [
    "Environment/MPC_GlobalEnvironment",
]


def ensure_dir(p: str):
    if not unreal.EditorAssetLibrary.does_directory_exist(p):
        unreal.EditorAssetLibrary.make_directory(p)


def delete_owned_tree():
    if unreal.EditorAssetLibrary.does_directory_exist(DST):
        # delete_directory may not exist on all versions — delete assets then dir
        assets = unreal.EditorAssetLibrary.list_assets(DST, True, False)
        unreal.log(f"deleting owned dups count={len(assets)}")
        for a in assets:
            pkg = a.split(".")[0]
            try:
                unreal.EditorAssetLibrary.delete_asset(pkg)
            except Exception as e:
                unreal.log_warning(f"del {pkg}: {e}")
        try:
            unreal.EditorAssetLibrary.delete_directory(DST)
        except Exception as e:
            unreal.log(f"deldir: {e}")
    ensure_dir(DST)


def rename_dir(src_rel: str):
    src = f"{SRC}/{src_rel}"
    dst = f"{DST}/{src_rel}"
    if not unreal.EditorAssetLibrary.does_directory_exist(src):
        unreal.log(f"skip missing dir {src}")
        return
    ensure_dir(dst.rsplit("/", 1)[0])
    # If dst exists partially, rename asset-by-asset
    if unreal.EditorAssetLibrary.does_directory_exist(dst):
        unreal.log(f"dst exists, asset-wise move {src_rel}")
        for a in unreal.EditorAssetLibrary.list_assets(src, True, False):
            sp = a.split(".")[0]
            rel = sp[len(SRC) + 1 :]
            dp = f"{DST}/{rel}"
            ensure_dir(dp.rsplit("/", 1)[0])
            if unreal.EditorAssetLibrary.does_asset_exist(dp):
                unreal.EditorAssetLibrary.delete_asset(dp)
            ok = unreal.EditorAssetLibrary.rename_asset(sp, dp)
            unreal.log(f"rename {sp} -> {dp} ok={ok}")
        return
    ok = unreal.EditorAssetLibrary.rename_directory(src, dst)
    unreal.log(f"rename_dir {src} -> {dst} ok={ok}")


def rename_asset(src_rel: str):
    src = f"{SRC}/{src_rel}"
    dst = f"{DST}/{src_rel}"
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        unreal.log(f"skip missing asset {src}")
        return
    ensure_dir(dst.rsplit("/", 1)[0])
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.EditorAssetLibrary.delete_asset(dst)
    ok = unreal.EditorAssetLibrary.rename_asset(src, dst)
    unreal.log(f"rename_asset {src} -> {dst} ok={ok}")


def place_and_light():
    bp = unreal.EditorAssetLibrary.load_asset(OWNED_BP)
    if not bp:
        raise RuntimeError(f"missing {OWNED_BP}")
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab == "LobbyStylizedSky" or "BP_StylizedSky" in cls or "BP_LobbyStylizedSky" in cls:
            eas.destroy_actor(a)
    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("LobbyStylizedSky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    for name, val in (
        ("dayCycleEnabled?", True),
        ("freezeAllTime?", True),
        ("start Time", 12.0),
        ("day Length", 600.0),
        ("dynamicWeather?", False),
        ("clouds?", True),
    ):
        try:
            actor.set_editor_property(name, val)
        except Exception:
            pass
    for c in actor.get_components_by_class(unreal.DirectionalLightComponent):
        try:
            c.set_intensity(10.0)
        except Exception:
            pass
        c.set_visibility(True)
    for c in actor.get_components_by_class(unreal.SkyLightComponent):
        try:
            c.set_intensity(1.0)
            c.recapture_sky()
        except Exception:
            pass
        c.set_visibility(True)
    for c in actor.get_components_by_class(unreal.ExponentialHeightFogComponent):
        try:
            c.set_editor_property("fog_density", 0.12)
        except Exception:
            pass
        c.set_visibility(True)
    # Hide pastel conflicts
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label() or ""
        if any(k in lab for k in ("PastelSun", "PastelFill", "PastelSkyLight", "PastelFog", "PastelSkyAtmosphere", "PastelPPV")):
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"placed {actor.get_path_name()}")


def verify():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    for prop, val in (
        ("include_soft_package_references", True),
        ("include_hard_package_references", True),
    ):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    seen = set()
    stack = [OWNED_BP]
    while stack:
        p = stack.pop()
        if p in seen:
            continue
        seen.add(p)
        try:
            deps = ar.get_dependencies(unreal.Name(p), opts) or []
        except Exception:
            continue
        for d in deps:
            stack.append(str(d))
    leaks = sorted(d for d in seen if "/SoStylized/" in d)
    unreal.log(f"LEAKS={len(leaks)}")
    for L in leaks[:30]:
        unreal.log(f"STILL {L}")
    if leaks:
        raise RuntimeError(f"still {len(leaks)} SoStylized deps")
    unreal.log("MOVE_OWNED_SKY_OK")


def run():
    unreal.log("=== move SoStylized sky -> Owned ===")
    delete_owned_tree()
    for d in MOVE_DIRS:
        rename_dir(d)
    for a in MOVE_ASSETS:
        rename_asset(a)
    place_and_light()
    verify()


run()
