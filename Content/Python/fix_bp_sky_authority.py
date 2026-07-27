"""
BP_Sky authority pass: BP soft refs + time drive MPC/materials; suppress level sky actors.
Run: py "E:/Unreal/MarineDemolition/Content/Python/fix_bp_sky_authority.py"
"""
import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import", "md_sky_component", "md_sky_paths"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)
import md_sky_paths as sky_paths
import md_sky_component as sky_comp

sky_paths = importlib.reload(sky_paths)
sky_comp = importlib.reload(sky_comp)
import md_sky_owned_assets as owned_assets

BP_SKY = sky_paths.BP_SKY


def load_first(*candidates):
    for path in candidates:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            return unreal.EditorAssetLibrary.load_asset(path), path
    if candidates:
        unreal.log_warning(f"MISSING {candidates[0]}")
    return None, None


def load_key(key: str):
    asset, path = load_first(*sky_paths.candidate_paths(key))
    return asset


def set_prop(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        unreal.log_warning(f"skip {name}: {e}")
        return False


def sanitize_hour(hour: float) -> float:
    hour = float(hour) % 24.0
    if hour < 0:
        hour += 24.0
    if hour <= 0.001:
        return 12.0
    return hour


def find_bp_sky():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if "BP_Sky" in a.get_class().get_name() or a.get_actor_label() == "BP_Sky":
            return a
    return None


def push_mpc(hour: float):
    hour = sanitize_hour(hour)
    mpc = load_key("EnvironmentMPC")
    if not mpc:
        unreal.log_warning("push_mpc: no MPC asset")
        return
    md_sky_mpc.push_midday_scalars(mpc, midday=hour, day_start=0.0, shader_offset=12.0)


def apply_bp_props(actor):
    for prop in sky_paths.PATHS:
        asset = load_key(prop)
        if asset:
            sky_comp.set_sky_prop(actor, prop, asset)

    hour = sky_comp.read_frozen_hour(actor)
    sky_comp.configure_lobby_frozen_sky(actor, hour)
    for prop, val in (
        ("bSuppressLevelSkyActors", True),
        ("SkyDomeScale", unreal.Vector(1, 1, 1)),
        ("CloudDomeScale", unreal.Vector(40, 40, 40)),
        ("OrbitInclinationDegrees", 28.0),
        ("OrbitDegreesPerHour", 15.0),
        ("OrbitOrientationOffsetDegrees", -90.0),
        ("MpcDayLengthSeconds", 600.0),
        ("MpcNightLengthSeconds", 480.0),
        ("MpcDayStartTimeHours", 0.0),
        ("MpcShaderTimeOffsetHours", 12.0),
        ("AutoExposureBias", 2.0),
        ("BloomIntensity", 5.5),
        ("MpcTimeOfDayName", "Current Time"),
        ("MpcDayCycleProgressName", "Day Cycle Progress"),
        ("MpcOrbitOrientationName", "Orbit Orientation"),
        ("MpcOrbitInclinationName", "Orbit Inclination"),
        ("SunriseHour", 6.0),
        ("SunsetHour", 18.0),
    ):
        sky_comp.set_sky_prop(actor, prop, val)


def apply_components(actor):
    sky_mesh = load_key("SkyDomeMesh")
    cloud_mesh = load_key("CloudDomeMesh")
    sky_mat = load_key("SkyMaterial")
    cloud_mat = load_key("CloudMaterial")

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        try:
            c.set_editor_property("reverse_culling", True)
        except Exception:
            pass
        if "SkyDomeClouds" in n:
            if cloud_mesh:
                c.set_static_mesh(cloud_mesh)
            if cloud_mat:
                c.set_material(0, cloud_mat)
            c.set_relative_scale3d(unreal.Vector(40, 40, 40))
            c.set_visibility(True, True)
        elif n == "SkyDome":
            if sky_mesh:
                c.set_static_mesh(sky_mesh)
            if sky_mat:
                c.set_material(0, sky_mat)
            c.set_relative_scale3d(unreal.Vector(1, 1, 1))
            c.set_visibility(True, True)

    light = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if light:
        try:
            light.set_editor_property("atmosphere_sun_light", False)
        except Exception:
            pass


def suppress_level_sky():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        if cls in ("SkyAtmosphere", "DirectionalLight", "SkyLight") and "BP_Sky" not in a.get_class().get_name():
            set_prop(a, "bHidden", True)
            try:
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass


def run():
    owned_assets.ensure_minimum_owned()

    actor = find_bp_sky()
    if not actor:
        raise RuntimeError("BP_Sky not found in level")

    apply_bp_props(actor)
    apply_components(actor)
    hour = sky_comp.read_frozen_hour(actor)
    push_mpc(hour)
    suppress_level_sky()

    try:
        actor.call_method("RefreshSkyNow")
    except Exception:
        pass
    try:
        actor.rerun_construction_scripts()
    except Exception:
        pass

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"FIX_BP_SKY_AUTHORITY_OK hour={hour}")


run()
