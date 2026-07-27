"""Restore SoStylized sky dome mesh + Classic MI (Engine Sphere makes sky black)."""
import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)

BP_SKY = "/Game/Developers/TaitoP/BP_Sky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
SKY_MESH = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome"
CLOUD_MESH = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds"
SKY_MAT = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic"
CLOUD_MAT = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic"
SUN_MAT = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_Sun_Classic"
MPC = "/Game/Developers/TaitoP/SoStylized/Environment/MPC_GlobalEnvironment"

SKY_SCALE = unreal.Vector(1.0, 1.0, 1.0)
CLOUD_SCALE = unreal.Vector(40.0, 40.0, 40.0)


def load(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    unreal.log_error(f"missing {path}")
    return None


def set_prop(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        unreal.log_warning(f"{name}: {e}")
        return False


def apply_actor(actor, sky_mesh, cloud_mesh, sky_mat, cloud_mat):
    set_prop(actor, "bUseFullSphereSkyDome", False)
    set_prop(actor, "SkyDomeScale", SKY_SCALE)
    set_prop(actor, "CloudDomeScale", CLOUD_SCALE)
    set_prop(actor, "bFrozenTime", True)
    set_prop(actor, "FrozenTimeOfDayHours", 12.0)
    if sky_mesh:
        set_prop(actor, "SkyDomeMesh", sky_mesh)
    if cloud_mesh:
        set_prop(actor, "CloudDomeMesh", cloud_mesh)
    if sky_mat:
        set_prop(actor, "SkyMaterial", sky_mat)
    if cloud_mat:
        set_prop(actor, "CloudMaterial", cloud_mat)

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        if "SkyDomeClouds" in n:
            if cloud_mesh:
                c.set_static_mesh(cloud_mesh)
            if cloud_mat:
                c.set_material(0, cloud_mat)
            c.set_relative_scale3d(CLOUD_SCALE)
            c.set_visibility(True)
        elif "SkyDome" in n:
            if sky_mesh:
                c.set_static_mesh(sky_mesh)
            if sky_mat:
                c.set_material(0, sky_mat)
            c.set_relative_scale3d(SKY_SCALE)
            c.set_visibility(True)
            # viewed from inside the dome
            try:
                c.set_editor_property("reverse_culling", True)
            except Exception:
                pass


def run():
    sky_mesh = load(SKY_MESH)
    cloud_mesh = load(CLOUD_MESH)
    sky_mat = load(SKY_MAT)
    cloud_mat = load(CLOUD_MAT)
    sun_mat = load(SUN_MAT)
    mpc = load(MPC)

    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
        cdo = unreal.get_default_object(bp.generated_class())
        apply_actor(cdo, sky_mesh, cloud_mesh, sky_mat, cloud_mat)
        if sun_mat:
            set_prop(cdo, "SunMaterial", sun_mat)
        if mpc:
            set_prop(cdo, "EnvironmentMPC", mpc)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log_warning(f"compile: {e}")
        unreal.EditorAssetLibrary.save_asset(BP_SKY)
        unreal.log("BP_Sky CDO fixed")

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    fixed = 0
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        lab = a.get_actor_label() or ""
        if "BP_Sky" in cls or lab in ("BP_Sky", "MD_StylizedSky"):
            apply_actor(a, sky_mesh, cloud_mesh, sky_mat, cloud_mat)
            fixed += 1
            unreal.log(f"fixed instance {lab}/{cls}")

    # Push midday into MPC so Classic day atlas is used
    if mpc:
        if md_sky_mpc.push_midday_scalars(mpc, 12.0):
            unreal.log("world MPC midday pushed")
        else:
            unreal.log_warning("world MPC midday push failed")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"FIX_BLACK_SKY_OK instances={fixed}")


run()
