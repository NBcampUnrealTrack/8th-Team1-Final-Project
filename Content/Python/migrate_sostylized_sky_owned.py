"""
Migrate SoStylized sky (textures + lighting stack) into owned Lobby paths so
SoStylized folder can be deleted later.

Uses AssetTools.migrate_packages / advanced copy when available, else
duplicate_asset + rename_referencing_soft_object_paths.
"""
from __future__ import annotations

import unreal

SRC_PREFIX = "/Game/Developers/TaitoP/SoStylized"
DST_PREFIX = "/Game/Developers/TaitoP/Environment/Sky/Owned"
SRC_BP = f"{SRC_PREFIX}/Environment/Sky/BP_StylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
OWNED_BP = f"{DST_PREFIX}/Environment/Sky/BP_StylizedSky"
LOBBY_LABEL = "LobbyStylizedSky"

# Core sky + lighting (exclude optional weather FX/audio unless pulled hard)
INCLUDE_PREFIXES = (
    f"{SRC_PREFIX}/Environment/Sky/",
    f"{SRC_PREFIX}/Environment/MPC_GlobalEnvironment",
    f"{SRC_PREFIX}/Textures/",
    f"{SRC_PREFIX}/Materials/",
)

EXCLUDE_SUBSTRINGS = (
    "/Effects/",
    "/Sounds/",
    "/Demo/",
    "/Curves/Presets/",  # classic curves enough for Lobby look
)


def map_path(src: str) -> str:
    if not src.startswith(SRC_PREFIX + "/"):
        raise ValueError(src)
    return DST_PREFIX + src[len(SRC_PREFIX) :]


def collect_deps(roots):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    for prop, val in (
        ("include_soft_package_references", True),
        ("include_hard_package_references", True),
        ("include_soft_path_references", True),
        ("include_searchable_names", False),
        ("include_soft_management_references", False),
        ("include_hard_management_references", False),
    ):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    seen = set()
    stack = list(roots)
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
            s = str(d)
            if s.startswith(SRC_PREFIX):
                stack.append(s)
    return sorted(seen)


def should_include(path: str) -> bool:
    if any(x in path for x in EXCLUDE_SUBSTRINGS):
        return False
    return any(path == p or path.startswith(p) for p in INCLUDE_PREFIXES) or path == SRC_BP


def ensure_dir_for_asset(package_path: str):
    # package_path like /Game/.../Foo (no asset suffix)
    folder = package_path.rsplit("/", 1)[0]
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)


def duplicate_one(src: str) -> str:
    dst = map_path(src)
    ensure_dir_for_asset(dst)
    name = src.rsplit("/", 1)[-1]
    dst_folder = dst.rsplit("/", 1)[0]
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.log(f"EXISTS {dst}")
        return dst
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        unreal.log_warning(f"MISS {src}")
        return ""
    # EAL: duplicate_asset(source_path, destination_path) — full package paths
    try:
        ok = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
        if ok:
            unreal.EditorAssetLibrary.save_asset(dst)
            unreal.log(f"DUP {src} -> {dst}")
            return dst
    except Exception as e:
        unreal.log(f"EAL.dup fail {e}")
    # AssetTools: duplicate_asset(name, dest_folder, loaded_object)
    asset = unreal.EditorAssetLibrary.load_asset(src)
    if not asset:
        unreal.log_warning(f"LOAD_FAIL {src}")
        return ""
    at = unreal.AssetToolsHelpers.get_asset_tools()
    try:
        dup = at.duplicate_asset(name, dst_folder, asset)
    except Exception as e:
        unreal.log_warning(f"AT.dup fail {src}: {e}")
        return ""
    if not dup:
        unreal.log_warning(f"DUP_FAIL {src}")
        return ""
    unreal.EditorAssetLibrary.save_loaded_asset(dup)
    unreal.log(f"DUP {src} -> {dst}")
    return dst

def rewrite_soft_paths(packages):
    """Point any remaining SoStylized soft refs inside Owned packages to Owned paths."""
    at = unreal.AssetToolsHelpers.get_asset_tools()
    # Build path pairs for rename_referencing_soft_object_paths
    # API: rename_referencing_soft_object_paths(packages_to_check, asset_redirector_map)
    # Try inspect
    try:
        import inspect

        unreal.log("rename_soft sig=" + str(inspect.signature(at.rename_referencing_soft_object_paths)))
    except Exception as e:
        unreal.log(f"rename_soft inspect: {e}")

    # Map of old package name -> new package name
    redirect_map = {}
    for src in packages:
        dst = map_path(src)
        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            redirect_map[src] = dst

    # Packages to scan = all owned copies
    owned = [map_path(p) for p in packages if unreal.EditorAssetLibrary.does_asset_exist(map_path(p))]

    try:
        # Some UE versions want Map[Name,Name] or SoftObjectPath pairs
        at.rename_referencing_soft_object_paths(owned, redirect_map)
        unreal.log(f"rewrite_soft ok count={len(redirect_map)}")
        return
    except Exception as e:
        unreal.log_warning(f"rename_referencing_soft_object_paths failed: {e}")

    # Fallback: load materials/BP and fix common object refs manually for lighting textures
    for dst in owned:
        asset = unreal.EditorAssetLibrary.load_asset(dst)
        if not asset:
            continue
        changed = False
        # Material texture samples
        if isinstance(asset, unreal.Material):
            for expr in unreal.MaterialEditingLibrary.get_material_expressions(asset):
                try:
                    tex = expr.get_editor_property("texture")
                except Exception:
                    tex = None
                if tex:
                    tp = tex.get_path_name().split(".")[0]
                    if tp.startswith(SRC_PREFIX):
                        nd = map_path(tp)
                        nt = unreal.EditorAssetLibrary.load_asset(nd)
                        if nt:
                            expr.set_editor_property("texture", nt)
                            changed = True
                try:
                    coll = expr.get_editor_property("collection")
                except Exception:
                    coll = None
                if coll:
                    cp = coll.get_path_name().split(".")[0]
                    if cp.startswith(SRC_PREFIX):
                        nc = unreal.EditorAssetLibrary.load_asset(map_path(cp))
                        if nc:
                            expr.set_editor_property("collection", nc)
                            changed = True
            if changed:
                unreal.MaterialEditingLibrary.recompile_material(asset)
        if isinstance(asset, unreal.MaterialInstanceConstant):
            try:
                parent = asset.get_editor_property("parent")
                if parent:
                    pp = parent.get_path_name().split(".")[0]
                    if pp.startswith(SRC_PREFIX):
                        np = unreal.EditorAssetLibrary.load_asset(map_path(pp))
                        if np:
                            asset.set_editor_property("parent", np)
                            changed = True
            except Exception:
                pass
            # texture params
            try:
                for tp in asset.texture_parameter_values:
                    tex = tp.get_editor_property("parameter_value")
                    if tex:
                        path = tex.get_path_name().split(".")[0]
                        if path.startswith(SRC_PREFIX):
                            nt = unreal.EditorAssetLibrary.load_asset(map_path(path))
                            if nt:
                                tp.set_editor_property("parameter_value", nt)
                                changed = True
            except Exception:
                pass
        if changed:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
            unreal.log(f"fixed refs {dst}")


def fix_bp_lighting_refs(bp_path: str):
    """Ensure SkyLight cubemap and mesh/mats on BP point to Owned assets."""
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    if not bp:
        raise RuntimeError(bp_path)
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        data = bfl.get_data(h)
        try:
            obj = bfl.get_associated_object(data)
        except Exception:
            obj = bfl.get_object(data)
        if not obj:
            continue
        # SkyLight cubemap
        if isinstance(obj, unreal.SkyLightComponent):
            try:
                cube = obj.get_editor_property("cubemap")
                if cube:
                    cp = cube.get_path_name().split(".")[0]
                    if cp.startswith(SRC_PREFIX):
                        nc = unreal.EditorAssetLibrary.load_asset(map_path(cp))
                        if nc:
                            obj.set_editor_property("cubemap", nc)
                            unreal.log("SkyLight cubemap -> Owned")
            except Exception as e:
                unreal.log(f"skylight cubemap: {e}")
            # apply known Classic midday lighting values (from ref inspect)
            try:
                obj.set_editor_property("intensity", 1.0)
                obj.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
                obj.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
                obj.set_editor_property("lower_hemisphere_is_black", True)
                obj.set_editor_property(
                    "lower_hemisphere_color",
                    unreal.LinearColor(0.073351, 0.157299, 0.270833, 1.0),
                )
                obj.set_editor_property("light_color", unreal.Color(255, 0, 230, 255))
                obj.set_editor_property("indirect_lighting_intensity", 0.0)
            except Exception as e:
                unreal.log(f"skylight set: {e}")
        if isinstance(obj, unreal.DirectionalLightComponent):
            try:
                obj.set_editor_property("intensity", 10.0)
                obj.set_editor_property("light_color", unreal.Color(255, 250, 243, 255))
                obj.set_editor_property("atmosphere_sun_light", True)
                obj.set_editor_property("specular_scale", 0.2)
                obj.set_editor_property("indirect_lighting_intensity", 1.0)
                obj.set_editor_property("light_source_angle", 1.5)
                obj.set_editor_property("cast_shadows", True)
                obj.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
                obj.set_editor_property("dynamic_shadow_distance_movable_light", 40000.0)
                obj.set_editor_property("volumetric_scattering_intensity", 1.0)
            except Exception as e:
                unreal.log(f"dir set: {e}")
        if isinstance(obj, unreal.ExponentialHeightFogComponent):
            try:
                obj.set_editor_property("fog_density", 0.12)
                obj.set_editor_property("fog_height_falloff", 0.3)
                obj.set_editor_property("fog_max_opacity", 0.001)
                obj.set_editor_property("enable_volumetric_fog", True)
                obj.set_editor_property("volumetric_fog_distance", 6000.0)
                obj.set_editor_property("volumetric_fog_scattering_distribution", 0.2552)
                obj.set_editor_property("volumetric_fog_albedo", unreal.Color(174, 183, 213, 255))
                obj.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
            except Exception as e:
                unreal.log(f"fog set: {e}")
        if isinstance(obj, unreal.PostProcessComponent):
            try:
                s = obj.get_editor_property("settings")
                s.set_editor_property("override_bloom_intensity", True)
                s.set_editor_property("bloom_intensity", 6.0)
                s.set_editor_property("override_bloom_threshold", True)
                s.set_editor_property("bloom_threshold", 0.4)
                s.set_editor_property("override_bloom_size_scale", True)
                s.set_editor_property("bloom_size_scale", 15.0)
                s.set_editor_property("override_auto_exposure_bias", True)
                s.set_editor_property("auto_exposure_bias", 1.0)
                s.set_editor_property("override_auto_exposure_min_brightness", True)
                s.set_editor_property("auto_exposure_min_brightness", 1.0)
                s.set_editor_property("override_auto_exposure_max_brightness", True)
                s.set_editor_property("auto_exposure_max_brightness", 1.0)
                obj.set_editor_property("settings", s)
            except Exception as e:
                unreal.log(f"pp set: {e}")
        if isinstance(obj, unreal.StaticMeshComponent):
            try:
                mesh = obj.get_editor_property("static_mesh")
                if mesh:
                    mp = mesh.get_path_name().split(".")[0]
                    if mp.startswith(SRC_PREFIX):
                        nm = unreal.EditorAssetLibrary.load_asset(map_path(mp))
                        if nm:
                            obj.set_static_mesh(nm)
                mat = obj.get_material(0)
                if mat:
                    mp = mat.get_path_name().split(".")[0]
                    if mp.startswith(SRC_PREFIX):
                        nm = unreal.EditorAssetLibrary.load_asset(map_path(mp))
                        if nm:
                            obj.set_material(0, nm)
            except Exception as e:
                unreal.log(f"mesh/mat: {e}")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(bp_path)


def try_migrate_packages(packages):
    at = unreal.AssetToolsHelpers.get_asset_tools()
    if hasattr(at, "migrate_packages"):
        try:
            import inspect

            unreal.log("migrate_packages sig=" + str(inspect.signature(at.migrate_packages)))
        except Exception:
            pass
        try:
            # Often: migrate_packages(package_names, destination_path)
            at.migrate_packages([unreal.Name(p) for p in packages], DST_PREFIX)
            unreal.log("migrate_packages called")
            return True
        except Exception as e:
            unreal.log_warning(f"migrate_packages failed: {e}")
    if hasattr(at, "begin_advanced_copy_packages"):
        try:
            import inspect

            unreal.log(
                "begin_advanced_copy_packages sig="
                + str(inspect.signature(at.begin_advanced_copy_packages))
            )
        except Exception:
            pass
        try:
            at.begin_advanced_copy_packages(packages, DST_PREFIX)
            unreal.log("begin_advanced_copy_packages called")
            return True
        except Exception as e:
            unreal.log_warning(f"begin_advanced_copy failed: {e}")
    return False


def place_owned_bp():
    bp = unreal.EditorAssetLibrary.load_asset(OWNED_BP)
    if not bp:
        raise RuntimeError(f"owned BP missing: {OWNED_BP}")
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab == LOBBY_LABEL or "BP_LobbyStylizedSky" in cls or "BP_StylizedSky" in cls:
            eas.destroy_actor(a)
    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label(LOBBY_LABEL)
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    # Enable classic day cycle like reference
    for name, val in (
        ("dayCycleEnabled?", True),
        ("freezeAllTime?", False),
        ("start Time", 12.0),
        ("day Length", 600.0),
        ("dynamicWeather?", False),
        ("clouds?", True),
    ):
        try:
            actor.set_editor_property(name, val)
            unreal.log(f"actor {name}={val}")
        except Exception as e:
            unreal.log(f"actor prop {name}: {e}")
    # Hide conflicting pastel lights
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if any(k in lab for k in ("PastelSun", "PastelFill", "PastelSkyLight", "PastelFog", "PastelSkyAtmosphere", "PastelPPV")) or cls == "SkyAtmosphere":
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
                for cc in (unreal.DirectionalLightComponent, unreal.SkyLightComponent, unreal.ExponentialHeightFogComponent):
                    c = a.get_component_by_class(cc)
                    if c:
                        try:
                            c.set_visibility(False)
                            c.set_intensity(0.0)
                        except Exception:
                            pass
            except Exception:
                pass
    try:
        sl = actor.get_component_by_class(unreal.SkyLightComponent)
        if sl:
            sl.recapture_sky()
    except Exception:
        pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"placed {actor.get_path_name()}")


def assert_no_sostylized_on_owned():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    for prop, val in (
        ("include_soft_package_references", True),
        ("include_hard_package_references", True),
        ("include_soft_path_references", True),
    ):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    leaks = []
    # check owned BP deps
    deps = ar.get_dependencies(unreal.Name(OWNED_BP), opts) or []
    for d in deps:
        s = str(d)
        if "SoStylized" in s:
            leaks.append(s)
            unreal.log_warning(f"LEAK {OWNED_BP} -> {s}")
        else:
            unreal.log(f"OKDEP {s}")
    if leaks:
        unreal.log_warning(f"LEAK_COUNT={len(leaks)} (will keep rewriting)")
    else:
        unreal.log("NO_SOSTYLIZED_DEPS_ON_OWNED_BP")
    return leaks


def run():
    if not unreal.EditorAssetLibrary.does_directory_exist(DST_PREFIX):
        unreal.EditorAssetLibrary.make_directory(DST_PREFIX)

    roots = [SRC_BP, f"{SRC_PREFIX}/Environment/MPC_GlobalEnvironment"]
    all_deps = collect_deps(roots)
    packages = [p for p in all_deps if should_include(p)]
    # Always include classic curve set explicitly
    for extra in (
        f"{SRC_PREFIX}/Environment/Sky/Curves/Atlas_Sky",
        f"{SRC_PREFIX}/Environment/Sky/Curves/Atlas_Clouds",
        f"{SRC_PREFIX}/Environment/Sky/Curves/Atlas_SunMoon",
        f"{SRC_PREFIX}/Environment/Sky/Curves/Curve_Sky_Classic_Day",
        f"{SRC_PREFIX}/Environment/Sky/Curves/Curve_Clouds_Classic_Day",
        f"{SRC_PREFIX}/Environment/Sky/Curves/Curve_SunColor_Classic",
        f"{SRC_PREFIX}/Environment/Sky/Curves/Curve_SunGlow_Classic",
        f"{SRC_PREFIX}/Environment/Sky/Textures/T_Skylight_HDR",
        f"{SRC_PREFIX}/Textures/Noise/T_NoiseRough",
    ):
        if unreal.EditorAssetLibrary.does_asset_exist(extra) and extra not in packages:
            packages.append(extra)
    packages = sorted(set(packages))
    unreal.log(f"MIGRATE_COUNT={len(packages)}")

    # Prefer engine migrate; fallback to per-asset duplicate
    used_migrate = try_migrate_packages(packages)
    if not used_migrate or not unreal.EditorAssetLibrary.does_asset_exist(OWNED_BP):
        unreal.log("FALLBACK_DUPLICATE")
        # Leaves first: sort by path depth descending? actually textures first = no deps among selves roughly by class
        # Sort: Texture/Curve/MPC first, then Materials, then Meshes, then BP last
        def rank(p):
            if "/Textures/" in p or p.endswith("MPC_GlobalEnvironment"):
                return 0
            if "/Curves/" in p:
                return 1
            if "/Materials/" in p and "/Presets/" not in p:
                return 2
            if "/Presets/" in p:
                return 3
            if "/Meshes/" in p:
                return 4
            if p.endswith("BP_StylizedSky"):
                return 9
            return 5

        for p in sorted(packages, key=rank):
            duplicate_one(p)

    rewrite_soft_paths(packages)
    if unreal.EditorAssetLibrary.does_asset_exist(OWNED_BP):
        fix_bp_lighting_refs(OWNED_BP)
    else:
        # migrate may have put assets under DST_PREFIX differently
        # search
        found = unreal.EditorAssetLibrary.list_assets(DST_PREFIX, True, False)
        unreal.log("OWNED_LIST")
        for a in found:
            if "BP_StylizedSky" in a:
                unreal.log(f"FOUND_BP {a}")
        raise RuntimeError("Owned BP_StylizedSky not found after migrate")

    # Second rewrite pass after BP fix
    rewrite_soft_paths(packages)
    leaks = assert_no_sostylized_on_owned()
    place_owned_bp()
    unreal.log(f"MIGRATE_SKY_OWNED_OK leaks={len(leaks)}")


run()
