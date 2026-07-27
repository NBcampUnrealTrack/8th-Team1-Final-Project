"""
Hard-reset BP_Sky to one clean SoStylized Classic configuration.
- Removes all BP_Sky / broken stylized sky actors from the current level
- Resets BP_Sky CDO SoftObjects + day freeze
- Spawns a single fresh BP_Sky
- Pushes midday into MPC_GlobalEnvironment (Current Time=12)
"""
import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)
import md_sky_paths as sky_paths
import md_sky_component as sky_comp

LEVEL = sky_paths.LEVEL_LOBBY
BP_SKY = sky_paths.BP_SKY
PATHS = sky_paths.PATHS


def load(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    unreal.log_error(f"MISSING {path}")
    return None


def set_prop(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        unreal.log_warning(f"skip {name}: {e}")
        return False


def is_sky_actor(actor):
    lab = actor.get_actor_label() or ""
    cls = actor.get_class().get_name()
    path = ""
    try:
        path = actor.get_class().get_path_name()
    except Exception:
        pass
    blob = f"{lab}|{cls}|{path}|{actor.get_path_name()}"
    return any(
        k in blob
        for k in (
            "BP_Sky",
            "BP_StylizedSky",
            "MD_StylizedSky",
            "LobbyStylizedSky",
            "LobbyOriginalSky",
            "M_LobbySkyFallback",
        )
    )


def sky_config_target(obj):
    import md_sky_component as sky_comp

    return sky_comp.get_sky_controller(obj) or obj


def reset_cdo(bp):
    cdo = unreal.get_default_object(bp.generated_class())
    target = sky_config_target(cdo)
    for prop, path in PATHS.items():
        if prop in ("SunPlaneMesh", "SunMaterial", "MoonMaterial"):
            continue
        asset = load(path)
        if asset:
            set_prop(target, prop, asset)

    extra_mpcs = [load(p) for p in SUPPLEMENTARY_MPCS]
    extra_mpcs = [m for m in extra_mpcs if m]
    if extra_mpcs:
        set_prop(target, "SupplementaryMPCs", extra_mpcs)

    import md_sky_component as sky_comp

    sky_comp.configure_lobby_frozen_sky(cdo, 12.0)
    for prop, val in (
        ("bSuppressLevelSkyActors", True),
        ("SkyDomeScale", unreal.Vector(1, 1, 1)),
        ("CloudDomeScale", unreal.Vector(40, 40, 40)),
        ("OrbitInclinationDegrees", 28.0),
        ("FogDensity", 0.003),
        ("DirectionalLightIntensity", 10.0),
        ("SkyLightIntensity", 1.15),
        ("MpcTimeOfDayName", "Current Time"),
        ("MpcDayCycleProgressName", "Day Cycle Progress"),
        ("MpcOrbitOrientationName", "Orbit Orientation"),
        ("MpcOrbitInclinationName", "Orbit Inclination"),
        ("OrbitDegreesPerHour", 15.0),
        ("OrbitOrientationOffsetDegrees", -90.0),
        ("MpcDayLengthSeconds", 600.0),
        ("MpcNightLengthSeconds", 480.0),
        ("MpcDayStartTimeHours", 0.0),
        ("MpcShaderTimeOffsetHours", 12.0),
    ):
        set_prop(target, prop, val)

    set_prop(cdo, "bUseFullSphereSkyDome", False)

    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    except Exception as e:
        unreal.log_warning(f"compile: {e}")
    unreal.EditorAssetLibrary.save_asset(BP_SKY)
    unreal.log("BP_Sky CDO reset OK")


def push_mpc_midday():
    mpc = load(PATHS["EnvironmentMPC"])
    if not mpc:
        return
    if md_sky_mpc.push_midday_scalars(mpc, 12.0):
        unreal.log("MPC midday pushed")


def apply_components(actor):
    sky_mesh = load(PATHS["SkyDomeMesh"])
    cloud_mesh = load(PATHS["CloudDomeMesh"])
    sun_mesh = load(PATHS["SunPlaneMesh"])
    sky_mat = load(PATHS["SkyMaterial"])
    cloud_mat = load(PATHS["CloudMaterial"])
    sun_mat = load(PATHS["SunMaterial"])
    moon_mat = load(PATHS["MoonMaterial"])

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        if "SkyDomeClouds" in n:
            if cloud_mesh:
                c.set_static_mesh(cloud_mesh)
            if cloud_mat:
                c.set_material(0, cloud_mat)
            c.set_relative_scale3d(unreal.Vector(40, 40, 40))
            try:
                c.set_editor_property("reverse_culling", True)
            except Exception:
                pass
        elif n == "SkyDome":
            if sky_mesh:
                c.set_static_mesh(sky_mesh)
            if sky_mat:
                c.set_material(0, sky_mat)
            c.set_relative_scale3d(unreal.Vector(1, 1, 1))
            try:
                c.set_editor_property("reverse_culling", True)
            except Exception:
                pass
        elif "SM_Sun" in n:
            if sun_mesh:
                c.set_static_mesh(sun_mesh)
            if sun_mat:
                c.set_material(0, sun_mat)
            c.set_relative_scale3d(unreal.Vector(1, 1, 1))
        elif "SM_Moon" in n or "Moon" in n:
            if sun_mesh:
                c.set_static_mesh(sun_mesh)
            if moon_mat:
                c.set_material(0, moon_mat)
            c.set_relative_scale3d(unreal.Vector(1, 1, 1))


def run():
    if not unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        raise RuntimeError(f"BP_Sky missing at {BP_SKY}")

    bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
    reset_cdo(bp)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for a in list(eas.get_all_level_actors()):
        if is_sky_actor(a):
            eas.destroy_actor(a)
            removed += 1
    unreal.log(f"removed sky actors={removed}")

    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("BP_Sky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass

    target = sky_config_target(actor)
    for prop, path in PATHS.items():
        if prop in ("SunPlaneMesh", "SunMaterial", "MoonMaterial"):
            continue
        asset = load(path)
        if asset:
            set_prop(target, prop, asset)
    extra_mpcs = [load(p) for p in SUPPLEMENTARY_MPCS]
    extra_mpcs = [m for m in extra_mpcs if m]
    if extra_mpcs:
        set_prop(target, "SupplementaryMPCs", extra_mpcs)

    import md_sky_component as sky_comp

    sky_comp.configure_lobby_frozen_sky(actor, 12.0)
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

    apply_components(actor)
    push_mpc_midday()

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.EditorAssetLibrary.save_asset(BP_SKY)
    unreal.log("RESET_BP_SKY_CLEAN_OK")


run()
