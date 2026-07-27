"""
Second pass: migrate ALL remaining SoStylized transitive deps into Owned,
rewrite material/BP refs, force Classic midday lighting on Lobby actor.
"""
from __future__ import annotations

import unreal

SRC_PREFIX = "/Game/Developers/TaitoP/SoStylized"
DST_PREFIX = "/Game/Developers/TaitoP/Environment/Sky/Owned"
OWNED_BP = f"{DST_PREFIX}/Environment/Sky/BP_StylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def map_path(src: str) -> str:
    return DST_PREFIX + src[len(SRC_PREFIX) :]


def collect(root):
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


def ensure_dir_for_asset(package_path: str):
    folder = package_path.rsplit("/", 1)[0]
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)


def duplicate_one(src: str) -> str:
    dst = map_path(src)
    ensure_dir_for_asset(dst)
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        return dst
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        unreal.log_warning(f"MISS {src}")
        return ""
    try:
        ok = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
        if ok:
            unreal.EditorAssetLibrary.save_asset(dst)
            unreal.log(f"DUP {src}")
            return dst
    except Exception as e:
        unreal.log(f"EAL fail {e}")
    asset = unreal.EditorAssetLibrary.load_asset(src)
    if not asset:
        return ""
    name = src.rsplit("/", 1)[-1]
    folder = dst.rsplit("/", 1)[0]
    dup = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, folder, asset)
    if dup:
        unreal.EditorAssetLibrary.save_loaded_asset(dup)
        unreal.log(f"DUP {src}")
        return dst
    unreal.log_warning(f"DUP_FAIL {src}")
    return ""


def fix_asset_refs(dst: str, redirect: dict):
    asset = unreal.EditorAssetLibrary.load_asset(dst)
    if not asset:
        return False
    changed = False

    def remap_obj(obj):
        nonlocal changed
        if not obj:
            return obj
        try:
            path = obj.get_path_name().split(".")[0]
        except Exception:
            return obj
        if path in redirect:
            neu = unreal.EditorAssetLibrary.load_asset(redirect[path])
            if neu:
                changed = True
                return neu
        return obj

    if isinstance(asset, unreal.Material):
        for expr in unreal.MaterialEditingLibrary.get_material_expressions(asset):
            for prop in ("texture", "collection", "material_function", "function"):
                try:
                    v = expr.get_editor_property(prop)
                except Exception:
                    continue
                nv = remap_obj(v)
                if nv is not v:
                    try:
                        expr.set_editor_property(prop, nv)
                    except Exception:
                        pass
        if changed:
            unreal.MaterialEditingLibrary.recompile_material(asset)
    elif isinstance(asset, unreal.MaterialInstanceConstant):
        try:
            parent = asset.get_editor_property("parent")
            np = remap_obj(parent)
            if np is not parent:
                asset.set_editor_property("parent", np)
        except Exception:
            pass
        try:
            for tp in list(asset.texture_parameter_values):
                tex = tp.get_editor_property("parameter_value")
                nt = remap_obj(tex)
                if nt is not tex:
                    tp.set_editor_property("parameter_value", nt)
        except Exception:
            pass
    elif isinstance(asset, unreal.MaterialFunction):
        try:
            for expr in unreal.MaterialEditingLibrary.get_material_expressions(asset):
                for prop in ("texture", "collection", "material_function", "function"):
                    try:
                        v = expr.get_editor_property(prop)
                    except Exception:
                        continue
                    nv = remap_obj(v)
                    if nv is not v:
                        try:
                            expr.set_editor_property(prop, nv)
                        except Exception:
                            pass
        except Exception as e:
            unreal.log(f"MF fix {dst}: {e}")
    elif isinstance(asset, unreal.Blueprint):
        sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        bfl = unreal.SubobjectDataBlueprintFunctionLibrary
        for h in sub.k2_gather_subobject_data_for_blueprint(asset):
            data = bfl.get_data(h)
            try:
                obj = bfl.get_associated_object(data)
            except Exception:
                obj = bfl.get_object(data)
            if not obj:
                continue
            # common soft object props on components
            for prop in (
                "static_mesh",
                "cubemap",
                "override_materials",
                "asset",
                "niagara_system_asset",
                "sound",
                "sound_class",
            ):
                try:
                    v = obj.get_editor_property(prop)
                except Exception:
                    continue
                if isinstance(v, (list, tuple)):
                    continue
                nv = remap_obj(v)
                if nv is not v:
                    try:
                        obj.set_editor_property(prop, nv)
                    except Exception:
                        pass
            try:
                mat = obj.get_material(0) if hasattr(obj, "get_material") else None
                nm = remap_obj(mat)
                if nm is not mat and nm and hasattr(obj, "set_material"):
                    obj.set_material(0, nm)
            except Exception:
                pass
        if changed:
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)

    if changed:
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        unreal.log(f"fixed {dst}")
    return changed


def force_midday_lighting(actor):
    # Match reference CDO Classic Day lighting (from inspect)
    for c in actor.get_components_by_class(unreal.DirectionalLightComponent):
        try:
            c.set_editor_property("intensity", 10.0)
            c.set_editor_property("light_color", unreal.Color(255, 250, 243, 255))
            c.set_editor_property("atmosphere_sun_light", True)
            c.set_editor_property("specular_scale", 0.2)
            c.set_editor_property("indirect_lighting_intensity", 1.0)
            c.set_editor_property("light_source_angle", 1.5)
            c.set_editor_property("cast_shadows", True)
            c.set_visibility(True)
            c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            c.set_editor_property("dynamic_shadow_distance_movable_light", 40000.0)
            c.set_editor_property("volumetric_scattering_intensity", 1.0)
            unreal.log("forced DirLight")
        except Exception as e:
            unreal.log(f"dir {e}")
    for c in actor.get_components_by_class(unreal.SkyLightComponent):
        try:
            cube = unreal.EditorAssetLibrary.load_asset(
                f"{DST_PREFIX}/Environment/Sky/Textures/T_Skylight_HDR"
            )
            if cube:
                c.set_editor_property("cubemap", cube)
            c.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            c.set_editor_property("intensity", 1.0)
            c.set_editor_property("light_color", unreal.Color(255, 0, 230, 255))
            c.set_editor_property("lower_hemisphere_is_black", True)
            c.set_editor_property(
                "lower_hemisphere_color", unreal.LinearColor(0.073351, 0.157299, 0.270833, 1.0)
            )
            c.set_editor_property("indirect_lighting_intensity", 0.0)
            c.set_visibility(True)
            try:
                c.recapture_sky()
            except Exception:
                pass
            unreal.log("forced SkyLight")
        except Exception as e:
            unreal.log(f"skyl {e}")
    for c in actor.get_components_by_class(unreal.ExponentialHeightFogComponent):
        try:
            c.set_editor_property("fog_density", 0.12)
            c.set_editor_property("fog_height_falloff", 0.3)
            c.set_editor_property("fog_max_opacity", 0.001)
            c.set_editor_property("enable_volumetric_fog", True)
            c.set_editor_property("volumetric_fog_distance", 6000.0)
            c.set_editor_property("volumetric_fog_scattering_distribution", 0.2552)
            c.set_editor_property("volumetric_fog_albedo", unreal.Color(174, 183, 213, 255))
            c.set_visibility(True)
            unreal.log("forced Fog")
        except Exception as e:
            unreal.log(f"fog {e}")
    for c in actor.get_components_by_class(unreal.PostProcessComponent):
        try:
            s = c.get_editor_property("settings")
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
            c.set_editor_property("settings", s)
            unreal.log("forced PP")
        except Exception as e:
            unreal.log(f"pp {e}")


def run():
    leaks = sorted(d for d in collect(OWNED_BP) if d.startswith(SRC_PREFIX))
    unreal.log(f"LEAKS_BEFORE={len(leaks)}")
    # Duplicate remaining SoStylized packages (effects/sounds/etc)
    # Rank: textures/materials first-ish by path depth
    for src in sorted(leaks, key=lambda p: (p.count("/"), p)):
        duplicate_one(src)

    # Build redirect for ALL SoStylized -> Owned that exist
    all_owned = unreal.EditorAssetLibrary.list_assets(DST_PREFIX, True, False)
    # Also need redirect from original paths
    redirect = {}
    # Re-collect every src under SoStylized that has owned twin via map_path convention
    # Scan owned assets and reverse-map
    for owned in all_owned:
        # owned like /Game/.../Owned/Environment/Sky/...
        if not owned.startswith(DST_PREFIX):
            continue
        src = SRC_PREFIX + owned[len(DST_PREFIX) :]
        # list_assets returns Package.Asset — strip
        owned_pkg = owned.split(".")[0]
        src_pkg = src.split(".")[0]
        if unreal.EditorAssetLibrary.does_asset_exist(src_pkg):
            redirect[src_pkg] = owned_pkg

    unreal.log(f"REDIRECTS={len(redirect)}")

    # Fix every owned asset
    for owned in all_owned:
        pkg = owned.split(".")[0]
        fix_asset_refs(pkg, redirect)

    # Compile owned BP after component remaps
    bp = unreal.EditorAssetLibrary.load_asset(OWNED_BP)
    if bp:
        # Remap BP subobjects thoroughly
        fix_asset_refs(OWNED_BP, redirect)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        unreal.EditorAssetLibrary.save_asset(OWNED_BP)

    leaks2 = sorted(d for d in collect(OWNED_BP) if d.startswith(SRC_PREFIX))
    unreal.log(f"LEAKS_AFTER={len(leaks2)}")
    for L in leaks2[:40]:
        unreal.log(f"STILL {L}")

    # Force lighting on level actor
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sky = None
    for a in eas.get_all_level_actors():
        if (a.get_actor_label() or "") == "LobbyStylizedSky":
            sky = a
            break
    if sky:
        # Prefer frozen midday so curve tick doesn't zero lights in editor
        for name, val in (
            ("dayCycleEnabled?", True),
            ("freezeAllTime?", True),
            ("start Time", 12.0),
            ("day Length", 600.0),
            ("dynamicWeather?", False),
            ("clouds?", True),
        ):
            try:
                sky.set_editor_property(name, val)
            except Exception:
                pass
        force_midday_lighting(sky)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("PASS2_OK")


run()
