"""
Force soft-path remaps using full SoftObjectPath (pkg.asset) and fix BP CDO lighting.
Does NOT delete SoStylized assets.
"""
from __future__ import annotations

import unreal

SRC_PREFIX = "/Game/Developers/TaitoP/SoStylized"
DST_PREFIX = "/Game/Developers/TaitoP/Environment/Sky/Owned"
OWNED_BP = f"{DST_PREFIX}/Environment/Sky/BP_StylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def soft(path: str) -> unreal.SoftObjectPath:
    # Ensure Package.Asset form
    if "." not in path.rsplit("/", 1)[-1]:
        name = path.rsplit("/", 1)[-1]
        path = f"{path}.{name}"
    return unreal.SoftObjectPath(path)


def collect(root, soft_only=False):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    if soft_only:
        props = (
            ("include_soft_package_references", True),
            ("include_hard_package_references", False),
            ("include_soft_path_references", True),
        )
    else:
        props = (
            ("include_soft_package_references", True),
            ("include_hard_package_references", True),
            ("include_soft_path_references", True),
        )
    for prop, val in props:
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
    packages = []
    for p in owned_assets:
        pkg = unreal.find_package(p) or unreal.load_package(p)
        if pkg:
            packages.append(pkg)

    redirect = {}
    for owned in owned_assets:
        src = SRC_PREFIX + owned[len(DST_PREFIX) :]
        if unreal.EditorAssetLibrary.does_asset_exist(src):
            redirect[soft(src)] = soft(owned)

    unreal.log(f"redirect_entries={len(redirect)} packages={len(packages)}")
    at = unreal.AssetToolsHelpers.get_asset_tools()
    try:
        at.rename_referencing_soft_object_paths(packages, redirect)
        unreal.log("soft rewrite ok")
    except Exception as e:
        unreal.log_warning(str(e))

    # Also replace hard refs via consolidate ONLY inside a copy? Skip delete.
    # Instead: for materials/MIC/MF, walk and set.

    # Fix BP CDO lighting defaults so spawned actors get correct values
    bp = unreal.EditorAssetLibrary.load_asset(OWNED_BP)
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        data = bfl.get_data(h)
        try:
            obj = bfl.get_associated_object(data)
        except Exception:
            obj = bfl.get_object(data)
        if isinstance(obj, unreal.DirectionalLightComponent):
            obj.set_editor_property("intensity", 10.0)
            obj.set_editor_property("light_color", unreal.Color(255, 250, 243, 255))
            obj.set_editor_property("atmosphere_sun_light", True)
            obj.set_editor_property("specular_scale", 0.2)
            obj.set_editor_property("indirect_lighting_intensity", 1.0)
            obj.set_editor_property("light_source_angle", 1.5)
            unreal.log("CDO DirLight=10")
        if isinstance(obj, unreal.SkyLightComponent):
            cube = unreal.EditorAssetLibrary.load_asset(f"{DST_PREFIX}/Environment/Sky/Textures/T_Skylight_HDR")
            if cube:
                obj.set_editor_property("cubemap", cube)
            obj.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            obj.set_editor_property("intensity", 1.0)
            unreal.log("CDO SkyLight cubemap+1")
        if isinstance(obj, unreal.ExponentialHeightFogComponent):
            obj.set_editor_property("fog_density", 0.12)
            obj.set_editor_property("fog_height_falloff", 0.3)
            obj.set_editor_property("fog_max_opacity", 0.001)
            obj.set_editor_property("enable_volumetric_fog", True)
            unreal.log("CDO Fog")
        if isinstance(obj, unreal.PostProcessComponent):
            s = obj.get_editor_property("settings")
            s.set_editor_property("override_bloom_intensity", True)
            s.set_editor_property("bloom_intensity", 6.0)
            s.set_editor_property("override_auto_exposure_bias", True)
            s.set_editor_property("auto_exposure_bias", 1.0)
            s.set_editor_property("override_auto_exposure_min_brightness", True)
            s.set_editor_property("auto_exposure_min_brightness", 1.0)
            s.set_editor_property("override_auto_exposure_max_brightness", True)
            s.set_editor_property("auto_exposure_max_brightness", 1.0)
            obj.set_editor_property("settings", s)
            unreal.log("CDO PP")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(OWNED_BP)

    # Respawn actor fresh from owned BP so CDO applies
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
            unreal.log(f"set {name}={val}")
        except Exception as e:
            unreal.log(f"{name}: {e}")

    # Force lights on instance after construction
    for c in actor.get_components_by_class(unreal.DirectionalLightComponent):
        c.set_editor_property("intensity", 10.0)
        c.set_visibility(True)
    for c in actor.get_components_by_class(unreal.SkyLightComponent):
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
    unreal.log(
        f"AFTER Dir={d.get_editor_property('intensity') if d else None} Sky={s.get_editor_property('intensity') if s else None}"
    )

    hard = sorted(
        x
        for x in collect(OWNED_BP, soft_only=False)
        if x.startswith(SRC_PREFIX)
        and ("/Environment/Sky/" in x or "MPC_Global" in x or "/Textures/Noise" in x or "/Materials/MF_" in x)
    )
    soft_leaks = sorted(x for x in collect(OWNED_BP, soft_only=True) if x.startswith(SRC_PREFIX))
    unreal.log(f"core_hard_or_any_leaks={len(hard)}")
    for h in hard[:25]:
        unreal.log(f"CORE {h}")
    unreal.log(f"soft_leaks={len(soft_leaks)}")

    # Direct hard deps of BP only (1-hop)
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    opts.set_editor_property("include_hard_package_references", True)
    opts.set_editor_property("include_soft_package_references", False)
    opts.set_editor_property("include_soft_path_references", False)
    one = ar.get_dependencies(unreal.Name(OWNED_BP), opts) or []
    unreal.log("ONE_HOP_HARD")
    for d in one:
        unreal.log(f"  {d}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("FINALIZE_OK")


run()
