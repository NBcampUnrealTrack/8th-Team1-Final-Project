"""
Consolidate SoStylized twins into Owned copies so hard refs move to Owned.
Deletes the SoStylized twin assets that were already duplicated (folder-delete ready).
Also force light intensities via set_intensity API.
"""
from __future__ import annotations

import unreal

SRC_PREFIX = "/Game/Developers/TaitoP/SoStylized"
DST_PREFIX = "/Game/Developers/TaitoP/Environment/Sky/Owned"
OWNED_BP = f"{DST_PREFIX}/Environment/Sky/BP_StylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def collect(root):
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
    stack = [root]
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
    return seen


def run():
    owned_assets = [a.split(".")[0] for a in unreal.EditorAssetLibrary.list_assets(DST_PREFIX, True, False)]
    pairs = []
    for owned in owned_assets:
        src = SRC_PREFIX + owned[len(DST_PREFIX) :]
        # Never consolidate the source BP into owned while we still want a clean owned BP;
        # consolidating SoStylized BP -> Owned BP would be wrong direction if names collide.
        if src.endswith("/BP_StylizedSky"):
            continue
        if unreal.EditorAssetLibrary.does_asset_exist(src) and unreal.EditorAssetLibrary.does_asset_exist(owned):
            pairs.append((src, owned))

    # Rank: textures/curves/mpc first, materials, meshes, effects, sounds last
    def rank(pair):
        s = pair[0]
        if "/Textures/" in s:
            return 0
        if "/Curves/" in s or "MPC_" in s:
            return 1
        if "/Materials/" in s and "/Presets/" not in s:
            return 2
        if "/Presets/" in s:
            return 3
        if "/Meshes/" in s:
            return 4
        if "/Effects/" in s:
            return 5
        if "/Sounds/" in s:
            return 6
        return 7

    pairs = sorted(pairs, key=rank)
    unreal.log(f"CONSOLIDATE_PAIRS={len(pairs)}")

    ok_n = 0
    fail_n = 0
    for src, dst in pairs:
        src_obj = unreal.EditorAssetLibrary.load_asset(src)
        dst_obj = unreal.EditorAssetLibrary.load_asset(dst)
        if not src_obj or not dst_obj:
            fail_n += 1
            continue
        try:
            # consolidate_assets(asset_to_consolidate_to, assets_to_consolidate)
            # Replaces refs to src with dst, deletes src
            result = unreal.EditorAssetLibrary.consolidate_assets(dst_obj, [src_obj])
            unreal.log(f"CONS {src} -> {dst} result={result}")
            ok_n += 1
        except Exception as e:
            unreal.log_warning(f"CONS_FAIL {src}: {e}")
            fail_n += 1

    unreal.log(f"cons_ok={ok_n} fail={fail_n}")

    # Save owned + compile BP
    bp = unreal.EditorAssetLibrary.load_asset(OWNED_BP)
    if bp:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        unreal.EditorAssetLibrary.save_asset(OWNED_BP)

    leaks = sorted(d for d in collect(OWNED_BP) if d.startswith(SRC_PREFIX))
    unreal.log(f"LEAKS_AFTER_CONS={len(leaks)}")
    for L in leaks[:40]:
        unreal.log(f"STILL {L}")

    # Respawn + force intensities
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
            c.set_editor_property("intensity", 10.0)
        c.set_visibility(True)
        try:
            c.set_light_color(unreal.LinearColor(1.0, 0.98, 0.95, 1.0))
        except Exception:
            pass
    for c in actor.get_components_by_class(unreal.SkyLightComponent):
        try:
            c.set_intensity(1.0)
        except Exception:
            c.set_editor_property("intensity", 1.0)
        c.set_visibility(True)
        try:
            c.recapture_sky()
        except Exception:
            pass
    for c in actor.get_components_by_class(unreal.ExponentialHeightFogComponent):
        c.set_editor_property("fog_density", 0.12)
        c.set_visibility(True)

    d = actor.get_component_by_class(unreal.DirectionalLightComponent)
    s = actor.get_component_by_class(unreal.SkyLightComponent)
    f = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
    unreal.log(
        f"LIGHT Dir={d.get_editor_property('intensity') if d else None} "
        f"Sky={s.get_editor_property('intensity') if s else None} "
        f"Fog={f.get_editor_property('fog_density') if f else None}"
    )
    if s:
        cube = s.get_editor_property("cubemap")
        unreal.log(f"CUBE {cube.get_path_name() if cube else None}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("CONS_PASS_OK")


run()
