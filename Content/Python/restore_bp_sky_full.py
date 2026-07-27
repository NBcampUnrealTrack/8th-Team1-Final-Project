"""
Full BP_Sky restore: harvest level instance settings → recreate asset → re-apply profile.

Run BEFORE destructive rename/migrate scripts in future:
  py "E:/Unreal/MarineDemolition/Content/Python/export_bp_sky_profile.py"

Restore (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/restore_bp_sky_full.py"
"""
from __future__ import annotations

import importlib
import json
import sys
from pathlib import Path

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_component",
    "md_sky_bp_profile",
    "md_sky_import",
    "md_sky_owned_assets",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp
import md_sky_bp_profile as profile_mod
import md_sky_owned_assets as owned_assets

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
profile_mod = importlib.reload(profile_mod)
owned_assets = importlib.reload(owned_assets)

BP_SKY = paths.BP_SKY
LEVEL = paths.LEVEL_LOBBY
PARENT_CLASS = "/Script/MarineDemolition.MD_StylizedSkyActor"
PROJECT_ROOT = Path(unreal.Paths.project_dir())
PROFILE_FILE = PROJECT_ROOT / profile_mod.PROFILE_PATH.replace("/", "\\")


def _vec3(v) -> list[float]:
    return [float(v.x), float(v.y), float(v.z)]


def _vec3_from_list(v) -> unreal.Vector:
    return unreal.Vector(float(v[0]), float(v[1]), float(v[2]))


def _soft_path(obj) -> str | None:
    if obj is None:
        return None
    try:
        if isinstance(obj, str):
            return obj.split(".", 1)[0] if obj.startswith("/Game/") else obj
        if hasattr(obj, "to_string"):
            s = str(obj.to_string())
            return s.split(".", 1)[0] if s.startswith("/Game/") else s
        path = obj.get_path_name()
        return path.split(".", 1)[0]
    except Exception:
        return None


def _load_asset(path: str | None, key: str | None = None):
    if not path and not key:
        return None
    pkg = path.split(".", 1)[0] if path else None
    if pkg and unreal.EditorAssetLibrary.does_asset_exist(pkg):
        return unreal.EditorAssetLibrary.load_asset(pkg)
    lookup_key = key or _key_for_path(pkg or "")
    if lookup_key:
        for candidate in paths.candidate_paths(lookup_key):
            if unreal.EditorAssetLibrary.does_asset_exist(candidate):
                return unreal.EditorAssetLibrary.load_asset(candidate)
    return None


def _load_static_mesh(path: str | None, key: str) -> unreal.StaticMesh | None:
    asset = _load_asset(path, key)
    return asset if isinstance(asset, unreal.StaticMesh) else None


def _load_material(path: str | None, key: str):
    asset = _load_asset(path, key)
    if isinstance(asset, unreal.MaterialInterface):
        return asset
    return None


def _key_for_path(pkg: str) -> str | None:
    base = pkg.split(".", 1)[0]
    for key, owned in paths.PATHS.items():
        if base == owned or base.endswith("/" + owned.rsplit("/", 1)[-1]):
            return key
        for candidate in paths.candidate_paths(key):
            if base == candidate or base.endswith("/" + candidate.rsplit("/", 1)[-1]):
                return key
    return None


def sanitize_soft_objects(soft: dict | None) -> dict:
    """Resolve broken rename paths → existing SoStylized / owned assets."""
    out: dict[str, str] = {}
    for key in profile_mod.SOFT_OBJECT_KEYS:
        pkg = (soft or {}).get(key)
        resolved = None
        if pkg:
            base = pkg.split(".", 1)[0]
            if unreal.EditorAssetLibrary.does_asset_exist(base):
                resolved = base
        if not resolved:
            for candidate in paths.candidate_paths(key):
                if unreal.EditorAssetLibrary.does_asset_exist(candidate):
                    resolved = candidate
                    break
        if resolved:
            out[key] = resolved
            if pkg and pkg.split(".", 1)[0] != resolved:
                unreal.log(f"SOFT_RESOLVE {key}: {pkg} -> {resolved}")
    return out


def ensure_lobby_level() -> None:
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception as e:
        unreal.log_warning(f"load_level: {e}")


def _set_prop(obj, name, value) -> bool:
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception:
        return False


def _read_prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None


def _serialize_value(name: str, value):
    if value is None:
        return None
    if isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.Vector):
        return _vec3(value)
    if isinstance(value, unreal.Rotator):
        return [float(value.pitch), float(value.yaw), float(value.roll)]
    if isinstance(value, unreal.LinearColor):
        return [float(value.r), float(value.g), float(value.b), float(value.a)]
    if isinstance(value, unreal.Name):
        return str(value)
    path = _soft_path(value)
    if path and path.startswith("/Game/"):
        return path
    if hasattr(value, "get_path_name"):
        p = value.get_path_name().split(".", 1)[0]
        if p.startswith("/Game/"):
            return p
    return None


def find_level_sky_actor():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    best = None
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        blob = f"{lab}|{cls}"
        if lab == "BP_Sky" or "BP_Sky" in cls or "MD_StylizedSky" in cls:
            best = a
            if lab == "BP_Sky":
                return a
    return best


def harvest_from_actor(actor) -> dict:
    out = {
        "actor_transform": {
            "location": _vec3(actor.get_actor_location()),
            "rotation": [
                float(actor.get_actor_rotation().pitch),
                float(actor.get_actor_rotation().yaw),
                float(actor.get_actor_rotation().roll),
            ],
        },
        "soft_objects": {},
        "sky_controller": {},
        "adapter": {},
        "components": {},
    }

    ctrl = sky_comp.get_sky_controller(actor)
    adapter = sky_comp.get_game_state_time_adapter(actor)
    target = ctrl or actor

    for key in profile_mod.SOFT_OBJECT_KEYS:
        val = _read_prop(target, key)
        path = _soft_path(val)
        if path:
            out["soft_objects"][key] = path

    for prop in profile_mod.SKY_CONTROLLER_PROPS:
        val = _serialize_value(prop, _read_prop(target, prop))
        if val is not None:
            out["sky_controller"][prop] = val

    if adapter:
        for prop in profile_mod.ADAPTER_PROPS:
            val = _serialize_value(prop, _read_prop(adapter, prop))
            if val is not None:
                out["adapter"][prop] = val

    light = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if light:
        out["components"]["directional_light"] = {}
        for prop in ("intensity", "Intensity", "atmosphere_sun_light", "AtmosphereSunLight"):
            val = _serialize_value(prop, _read_prop(light, prop))
            if val is not None:
                out["components"]["directional_light"][prop] = val

    sl = actor.get_component_by_class(unreal.SkyLightComponent)
    if sl:
        out["components"]["skylight"] = {}
        for prop in ("intensity", "Intensity"):
            val = _serialize_value(prop, _read_prop(sl, prop))
            if val is not None:
                out["components"]["skylight"][prop] = val

    fog = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if fog:
        out["components"]["height_fog"] = {}
        for prop in (
            "fog_density",
            "FogDensity",
            "fog_height_falloff",
            "FogHeightFalloff",
            "fog_max_opacity",
            "FogMaxOpacity",
        ):
            val = _serialize_value(prop, _read_prop(fog, prop))
            if val is not None:
                out["components"]["height_fog"][prop] = val

    pp = actor.get_component_by_class(unreal.PostProcessComponent)
    if pp:
        try:
            settings = pp.get_editor_property("settings")
            out["components"]["post_process"] = {
                "bloom_intensity": _read_prop(settings, "bloom_intensity")
                or _read_prop(settings, "BloomIntensity"),
                "auto_exposure_bias": _read_prop(settings, "auto_exposure_bias")
                or _read_prop(settings, "AutoExposureBias"),
            }
        except Exception:
            pass

    return out


def load_profile_file() -> dict | None:
    if not PROFILE_FILE.is_file():
        return None
    try:
        return json.loads(PROFILE_FILE.read_text(encoding="utf-8"))
    except Exception as e:
        unreal.log_warning(f"profile read fail: {e}")
        return None


def save_profile_file(data: dict) -> None:
    PROFILE_FILE.parent.mkdir(parents=True, exist_ok=True)
    PROFILE_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    unreal.log(f"PROFILE saved {PROFILE_FILE}")


def merge_profiles(*layers: dict | None) -> dict:
    import copy

    base = copy.deepcopy(profile_mod.LOBBY_DEFAULTS)
    for layer in layers:
        if not layer:
            continue
        for section in ("soft_objects", "sky_controller", "adapter", "components"):
            if section in layer and layer[section]:
                base.setdefault(section, {})
                base[section].update(layer[section])
        if "actor_transform" in layer:
            base["actor_transform"] = layer["actor_transform"]
        if "mpc_midday_hour" in layer:
            base["mpc_midday_hour"] = layer["mpc_midday_hour"]
    return base


def is_valid_bp() -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        return False
    bp = sky_comp.load_bp_sky_blueprint()
    if not bp:
        return False
    try:
        return bool(bp.generated_class())
    except Exception:
        return False


def ensure_bp_asset() -> unreal.Blueprint:
    if is_valid_bp():
        bp = sky_comp.load_bp_sky_blueprint()
        unreal.log(f"BP_Sky valid at {BP_SKY}")
        return bp

    for stale in (
        BP_SKY,
        f"{paths.SKY_ROOT}/BP_Sky",
        "/Game/Developers/TaitoP/BP_Sky",
    ):
        if unreal.EditorAssetLibrary.does_asset_exist(stale):
            unreal.EditorAssetLibrary.delete_asset(stale)
            unreal.log(f"DELETE stale {stale}")

    parent = unreal.load_class(None, PARENT_CLASS)
    if not parent:
        raise RuntimeError(f"{PARENT_CLASS} missing — Live Coding (Ctrl+Alt+F11) first")

    folder = BP_SKY.rsplit("/", 1)[0]
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_Sky", folder, unreal.Blueprint, factory
    )
    if not bp:
        raise RuntimeError(f"create_asset failed: {BP_SKY}")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_SKY)
    unreal.log(f"CREATED {BP_SKY}")
    return bp


def apply_profile_to_target(target, profile: dict) -> None:
    owned_assets.ensure_minimum_owned()

    soft = profile.get("soft_objects") or {}
    for key in profile_mod.SOFT_OBJECT_KEYS:
        pkg = soft.get(key)
        asset = _load_asset(pkg, key)
        if asset:
            _set_prop(target, key, asset)

    for prop, val in (profile.get("sky_controller") or {}).items():
        if prop in ("SkyDomeScale", "CloudDomeScale") and isinstance(val, list):
            _set_prop(target, prop, _vec3_from_list(val))
        elif prop.endswith("Name") and isinstance(val, str):
            _set_prop(target, prop, unreal.Name(val))
        else:
            _set_prop(target, prop, val)


def apply_profile_components(actor, profile: dict) -> None:
    comps = profile.get("components") or {}

    dl_cfg = comps.get("directional_light") or {}
    light = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if light and dl_cfg:
        for prop, val in dl_cfg.items():
            _set_prop(light, prop, val)

    sl_cfg = comps.get("skylight") or {}
    sl = actor.get_component_by_class(unreal.SkyLightComponent)
    if sl and sl_cfg:
        for prop, val in sl_cfg.items():
            _set_prop(sl, prop, val)

    fog_cfg = comps.get("height_fog") or {}
    fog = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if fog and fog_cfg:
        for prop, val in fog_cfg.items():
            _set_prop(fog, prop, val)

    pp_cfg = comps.get("post_process") or {}
    pp = actor.get_component_by_class(unreal.PostProcessComponent)
    if pp and pp_cfg:
        try:
            settings = pp.get_editor_property("settings")
            if "bloom_intensity" in pp_cfg:
                for pname in ("bloom_intensity", "BloomIntensity"):
                    if _set_prop(settings, pname, pp_cfg["bloom_intensity"]):
                        break
            if "auto_exposure_bias" in pp_cfg:
                for pname in ("auto_exposure_bias", "AutoExposureBias"):
                    if _set_prop(settings, pname, pp_cfg["auto_exposure_bias"]):
                        break
            pp.set_editor_property("settings", settings)
        except Exception as e:
            unreal.log_warning(f"post_process apply: {e}")

    mesh_cfg = comps.get("mesh") or {}
    if mesh_cfg.get("reverse_culling"):
        for c in actor.get_components_by_class(unreal.StaticMeshComponent):
            _set_prop(c, "reverse_culling", True)

    ctrl = sky_comp.get_sky_controller(actor)
    target = ctrl or actor
    sky_scale = (profile.get("sky_controller") or {}).get("SkyDomeScale")
    cloud_scale = (profile.get("sky_controller") or {}).get("CloudDomeScale")
    soft = profile.get("soft_objects") or {}
    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        mesh = None
        mat = None
        scale = None
        if "SkyDomeClouds" in n:
            mesh = _load_static_mesh(soft.get("CloudDomeMesh"), "CloudDomeMesh")
            mat = _load_material(soft.get("CloudMaterial"), "CloudMaterial")
            scale = _vec3_from_list(cloud_scale) if cloud_scale else unreal.Vector(40, 40, 40)
        elif n == "SkyDome":
            mesh = _load_static_mesh(soft.get("SkyDomeMesh"), "SkyDomeMesh")
            mat = _load_material(soft.get("SkyMaterial"), "SkyMaterial")
            scale = _vec3_from_list(sky_scale) if sky_scale else unreal.Vector(1, 1, 1)
        if mesh:
            c.set_static_mesh(mesh)
        if mat:
            c.set_material(0, mat)
        if scale:
            c.set_relative_scale3d(scale)


def push_mpc(hour: float) -> None:
    sys.modules.pop("md_sky_import", None)
    import md_sky_import as md_sky_mpc

    md_sky_mpc = importlib.reload(md_sky_mpc)
    mpc = _load_asset(paths.PATHS.get("EnvironmentMPC"), "EnvironmentMPC")
    if mpc:
        md_sky_mpc.push_midday_scalars(mpc, midday=hour, day_start=0.0, shader_offset=12.0)


def suppress_level_sky() -> None:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        if cls in ("SkyAtmosphere", "DirectionalLight", "SkyLight") and "BP_Sky" not in cls:
            _set_prop(a, "bHidden", True)
            try:
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass


def respawn_in_level(bp, profile: dict) -> unreal.Actor:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    old = find_level_sky_actor()
    loc = unreal.Vector(0, 0, 0)
    rot = unreal.Rotator(0, 0, 0)
    if old:
        loc = old.get_actor_location()
        rot = old.get_actor_rotation()
        eas.destroy_actor(old)

    tf = profile.get("actor_transform")
    if tf:
        loc = unreal.Vector(*tf["location"])
        r = tf["rotation"]
        rot = unreal.Rotator(r[0], r[1], r[2])

    actor = eas.spawn_actor_from_object(bp, loc, rot)
    actor.set_actor_label("BP_Sky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass

    adapter = sky_comp.get_game_state_time_adapter(actor)
    for prop, val in (profile.get("adapter") or {}).items():
        if adapter:
            _set_prop(adapter, prop, val)

    apply_profile_to_target(sky_comp.get_sky_controller(actor) or actor, profile)
    apply_profile_components(actor, profile)

    try:
        actor.call_method("RefreshSkyNow")
    except Exception:
        pass
    try:
        actor.rerun_construction_scripts()
    except Exception:
        pass

    return actor


def main() -> None:
    ensure_lobby_level()

    harvested = None
    actor = find_level_sky_actor()
    if actor:
        harvested = harvest_from_actor(actor)
        unreal.log(f"HARVEST ok from {actor.get_actor_label()} ({actor.get_class().get_name()})")
    else:
        unreal.log_warning("no level BP_Sky actor — using saved/default profile only")

    saved = load_profile_file()
    profile = merge_profiles(profile_mod.LOBBY_DEFAULTS, saved, harvested)
    profile["soft_objects"] = sanitize_soft_objects(profile.get("soft_objects"))
    save_profile_file(profile)

    bp = ensure_bp_asset()
    cdo = unreal.get_default_object(bp.generated_class())
    apply_profile_to_target(sky_comp.get_sky_controller(cdo) or cdo, profile)

    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    except Exception as e:
        unreal.log_warning(f"compile: {e}")
    unreal.EditorAssetLibrary.save_asset(BP_SKY)

    actor = respawn_in_level(bp, profile)
    hour = float((profile.get("sky_controller") or {}).get("LocalTimeOfDayHours", 12.0))
    if (profile.get("sky_controller") or {}).get("bFrozenTime", True):
        hour = float((profile.get("sky_controller") or {}).get("FrozenTimeOfDayHours", hour))
    push_mpc(hour)
    suppress_level_sky()

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(
        "RESTORE_BP_SKY_FULL_OK "
        f"harvested={bool(harvested)} saved={bool(saved)} hour={hour}"
    )


if __name__ == "__main__":
    main()
