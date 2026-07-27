"""One-shot black sky fix: BP soft refs, both MPCs, hide SkyAtmosphere, refresh BP_Sky."""
import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)
import md_sky_paths as sky_paths
import md_sky_component as sky_comp

BP_SKY = sky_paths.BP_SKY
LEVEL = sky_paths.LEVEL_LOBBY

MPC_PATHS = sky_paths.MPC_PATHS
PATHS = sky_paths.PATHS


def load(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    unreal.log_warning(f"MISSING {path}")
    return None


def set_prop(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        unreal.log_warning(f"skip {name}: {e}")
        return False


def sky_config_target(obj):
    import md_sky_component as sky_comp

    return sky_comp.get_sky_controller(obj) or obj


def set_soft_refs(obj):
    target = sky_config_target(obj)
    for prop, path in PATHS.items():
        asset = load(path)
        if asset:
            set_prop(target, prop, asset)

    extra = [load(p) for p in MPC_PATHS if p != PATHS["EnvironmentMPC"]]
    extra = [m for m in extra if m]
    if extra:
        set_prop(target, "SupplementaryMPCs", extra)


def actor_props(obj, midday=12.0):
    import md_sky_component as sky_comp

    target = sky_config_target(obj)
    sky_comp.configure_lobby_frozen_sky(obj, midday)
    for prop, val in (
        ("bSuppressLevelSkyActors", True),
        ("SkyDomeScale", unreal.Vector(1, 1, 1)),
        ("CloudDomeScale", unreal.Vector(40, 40, 40)),
        ("MpcTimeOfDayName", "Current Time"),
        ("MpcDayCycleProgressName", "Day Cycle Progress"),
        ("OrbitDegreesPerHour", 15.0),
        ("OrbitOrientationOffsetDegrees", -90.0),
        ("MpcDayLengthSeconds", 600.0),
        ("MpcNightLengthSeconds", 480.0),
        ("MpcDayStartTimeHours", 0.0),
        ("MpcShaderTimeOffsetHours", 12.0),
    ):
        set_prop(target, prop, val)


def set_mpc_defaults(mpc, midday=12.0):
    md_sky_mpc.set_mpc_asset_defaults(mpc, midday)


def push_world_mpc(mpc, midday=12.0):
    md_sky_mpc.push_midday_scalars(mpc, midday)


def apply_components(actor):
    sky_mesh = load(PATHS["SkyDomeMesh"])
    cloud_mesh = load(PATHS["CloudDomeMesh"])
    sky_mat = load(PATHS["SkyMaterial"])
    cloud_mat = load(PATHS["CloudMaterial"])

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        try:
            c.set_editor_property("reverse_culling", True)
            c.set_editor_property("bounds_scale", 1000.0)
        except Exception:
            pass
        try:
            c.set_force_disable_nanite(True)
        except Exception:
            try:
                c.set_editor_property("bDisallowNanite", True)
            except Exception:
                pass
        if "SkyDomeClouds" in n:
            if cloud_mesh:
                c.set_static_mesh(cloud_mesh)
            if cloud_mat:
                c.set_material(0, cloud_mat)
            c.set_relative_scale3d(unreal.Vector(40, 40, 40))
            c.set_visibility(True, True)
        elif "SkyDome" in n and "Cloud" not in n:
            if sky_mesh:
                c.set_static_mesh(sky_mesh)
            if sky_mat:
                c.set_material(0, sky_mat)
            c.set_relative_scale3d(unreal.Vector(1, 1, 1))
            c.set_visibility(True, True)
            try:
                c.set_hidden_in_game(False, True)
            except Exception:
                pass
            if sky_mat:
                try:
                    mid = c.create_dynamic_material_instance(0, sky_mat)
                    if mid:
                        mid.set_scalar_parameter_value("Sky Brightness", 1.5)
                except Exception:
                    pass


def hide_level_sky():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    hidden = 0
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        if cls in ("SkyAtmosphere", "DirectionalLight", "SkyLight"):
            try:
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass
            a.set_actor_hidden_in_game(True)
            hidden += 1
    unreal.log(f"hidden level sky actors={hidden}")


def refresh_actor(actor):
    apply_components(actor)
    try:
        actor.call_method("RefreshSkyNow")
    except Exception:
        try:
            actor.rerun_construction_scripts()
        except Exception as e:
            unreal.log_warning(f"refresh: {e}")


def run():
    midday = 12.0

    for path in MPC_PATHS:
        mpc = load(path)
        if mpc:
            set_mpc_defaults(mpc, midday)
            push_world_mpc(mpc, midday)

    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
        cdo = unreal.get_default_object(bp.generated_class())
        set_soft_refs(cdo)
        actor_props(cdo, midday)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log_warning(f"compile: {e}")
        unreal.EditorAssetLibrary.save_asset(BP_SKY)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        blob = f"{actor.get_actor_label()}|{actor.get_class().get_name()}"
        if "BP_Sky" not in blob and "MD_StylizedSky" not in blob:
            continue
        set_soft_refs(actor)
        actor_props(actor, midday)
        refresh_actor(actor)
        unreal.log(f"fixed {actor.get_actor_label()}")

    hide_level_sky()

    mi = load(PATHS["SkyMaterial"])
    if mi:
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mi, "Sky Brightness", 1.5
            )
            unreal.EditorAssetLibrary.save_asset(PATHS["SkyMaterial"])
        except Exception as e:
            unreal.log_warning(f"mi brightness: {e}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("FIX_BLACK_SKY_OK")


run()
