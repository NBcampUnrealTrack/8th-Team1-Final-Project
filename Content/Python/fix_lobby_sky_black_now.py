"""
Fix black sky in L_Lobby: restore BP_Sky, wire owned meshes/mats, MPC midday, dome culling.

Run (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/fix_lobby_sky_black_now.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in (
    "md_sky_paths",
    "md_sky_component",
    "md_sky_import",
    "md_sky_mpc",
    "md_sky_owned_assets",
    "restore_bp_sky_full",
    "fix_sky_dome_interior",
):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp
import md_sky_import as md_sky_mpc
import md_sky_owned_assets as owned_assets

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
md_sky_mpc = importlib.reload(md_sky_mpc)
owned_assets = importlib.reload(owned_assets)

MID_DAY = 12.0
LEVEL = paths.LEVEL_LOBBY
PARENT_MATS = (
    f"{paths.SKY_ROOT}/Materials/M_StylizedSky",
    f"{paths.SKY_ROOT}/Materials/M_StylizedClouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedSky",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
    "/Game/SoStylized/Environment/Sky/Materials/M_StylizedSky",
    "/Game/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
)
SKY_MIS = (
    paths.PATHS["SkyMaterial"],
    f"{paths.SKY_ROOT}/Materials/Presets/MI_StylizedSky_Classic",
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
)
CLOUD_MIS = (
    paths.PATHS["CloudMaterial"],
    f"{paths.SKY_ROOT}/Materials/Presets/MI_StylizedClouds_Classic",
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
)


def load_first(*candidates):
    for p in candidates:
        if not unreal.EditorAssetLibrary.does_asset_exist(p):
            continue
        asset = unreal.EditorAssetLibrary.load_asset(p)
        if not asset:
            continue
        if asset.get_class().get_name() == "ObjectRedirector":
            unreal.log_warning(f"skip redirector {p}")
            continue
        return asset, p
    return None, None


def load_key(key: str):
    asset, path = load_first(*paths.candidate_paths(key))
    if asset and key in ("SkyDomeMesh", "CloudDomeMesh"):
        p = path or ""
        if "Sphere" in p or (key == "SkyDomeMesh" and p.endswith("/SM_SkyDome")):
            stylized, sp = load_first(
                f"{paths.SKY_ROOT}/Meshes/SM_StylizedSkyDome",
                "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome",
                "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome",
            )
            if stylized:
                unreal.log_warning(f"mesh fallback {p} -> {sp}")
                return stylized
        if key == "CloudDomeMesh" and "Stylized" not in p:
            stylized, sp = load_first(
                f"{paths.SKY_ROOT}/Meshes/SM_StylizedSkyDome_Clouds",
                "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
                "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
            )
            if stylized:
                unreal.log_warning(f"mesh fallback {p} -> {sp}")
                return stylized
    return asset


def ensure_lobby() -> None:
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception as e:
        unreal.log_warning(f"load_level: {e}")


def set_two_sided(mat_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        return
    mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    try:
        mat.set_editor_property("two_sided", True)
        unreal.EditorAssetLibrary.save_asset(mat_path)
        unreal.log(f"two_sided=True {mat_path}")
    except Exception as e:
        unreal.log_warning(f"two_sided {mat_path}: {e}")


def boost_mi_brightness(mi_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    for name, val in (("Sky Brightness", 1.5), ("Nebula Multiply", 0.0)):
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mi, name, val
            )
        except Exception:
            pass
    unreal.EditorAssetLibrary.save_asset(mi_path)


def apply_actor(actor) -> None:
    ctrl = sky_comp.get_sky_controller(actor) or actor
    for key in paths.PATHS:
        asset = load_key(key)
        if asset:
            sky_comp.set_sky_prop(actor, key, asset)

    sky_comp.configure_lobby_frozen_sky(actor, MID_DAY)
    for prop, val in (
        ("bSuppressLevelSkyActors", True),
        ("SkyDomeScale", unreal.Vector(1, 1, 1)),
        ("CloudDomeScale", unreal.Vector(40, 40, 40)),
        ("MpcDayStartTimeHours", 0.0),
        ("MpcShaderTimeOffsetHours", 12.0),
        ("MpcTimeOfDayName", "Current Time"),
    ):
        sky_comp.set_sky_prop(actor, prop, val)

    sky_mesh = load_key("SkyDomeMesh")
    cloud_mesh = load_key("CloudDomeMesh")
    sky_mat = load_key("SkyMaterial")
    cloud_mat = load_key("CloudMaterial")
    unreal.log(
        f"wire sky_mesh={sky_mesh.get_name() if sky_mesh else None} "
        f"cloud_mesh={cloud_mesh.get_name() if cloud_mesh else None}"
    )

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        try:
            c.set_editor_property("reverse_culling", True)
            c.set_editor_property("bounds_scale", 1000.0)
        except Exception:
            pass
        c.set_visibility(True, True)
        c.set_hidden_in_game(False, True)
        if "SkyDomeClouds" in n:
            if cloud_mesh and isinstance(cloud_mesh, unreal.StaticMesh):
                c.set_static_mesh(cloud_mesh)
            if cloud_mat:
                c.set_material(0, cloud_mat)
            c.set_relative_scale3d(unreal.Vector(40, 40, 40))
        elif n == "SkyDome":
            if sky_mesh and isinstance(sky_mesh, unreal.StaticMesh):
                c.set_static_mesh(sky_mesh)
            if sky_mat:
                c.set_material(0, sky_mat)
            c.set_relative_scale3d(unreal.Vector(1, 1, 1))

    light = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if light:
        try:
            light.set_editor_property("atmosphere_sun_light", False)
        except Exception:
            pass

    try:
        actor.call_method("RefreshSkyNow")
    except Exception:
        pass
    try:
        actor.rerun_construction_scripts()
    except Exception:
        pass


def push_mpc_all() -> None:
    mpc = load_key("EnvironmentMPC")
    if not mpc:
        unreal.log_warning("MPC missing")
        return
    md_sky_mpc.set_mpc_asset_defaults(mpc, MID_DAY)
    for world in md_sky_mpc.all_world_contexts():
        md_sky_mpc.push_midday_scalars(mpc, midday=MID_DAY, day_start=0.0, shader_offset=12.0)


def find_or_spawn_bp_sky():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if "BP_Sky" in a.get_class().get_name() or a.get_actor_label() == "BP_Sky":
            return a

    bp = sky_comp.load_bp_sky_blueprint()
    if not bp:
        import restore_bp_sky_full as restore

        restore = importlib.reload(restore)
        restore.main()
        for a in eas.get_all_level_actors():
            if "BP_Sky" in a.get_class().get_name():
                return a
        return None

    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("BP_Sky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    return actor


def main() -> None:
    ensure_lobby()
    owned_assets.ensure_minimum_owned()

    for p in PARENT_MATS + SKY_MIS + CLOUD_MIS:
        if p.endswith(("M_Sky", "M_Clouds", "M_StylizedSky", "M_StylizedClouds")) or "/M_" in p:
            if "MI_" not in p:
                set_two_sided(p)
    for p in SKY_MIS:
        boost_mi_brightness(p)

    push_mpc_all()

    actor = find_or_spawn_bp_sky()
    if not actor:
        raise RuntimeError("BP_Sky actor missing — open L_Lobby and retry")

    apply_actor(actor)

    import fix_sky_dome_interior as dome_interior

    dome_interior = importlib.reload(dome_interior)
    dome_interior.run()

    # Hide competing level sky
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        if cls == "SkyAtmosphere":
            try:
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"FIX_LOBBY_SKY_BLACK_OK actor={actor.get_path_name()}")


if __name__ == "__main__":
    main()
