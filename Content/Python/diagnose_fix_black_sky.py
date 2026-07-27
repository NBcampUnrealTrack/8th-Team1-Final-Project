"""Diagnose + fix black sky: all worlds MPC, mesh/mat validation, MI texture repair."""
import importlib
import sys

import unreal

# UE keeps stale md_sky_* in sys.modules between `py` runs — always purge first.
for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)

import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)
import md_sky_paths as sky_paths

BP_SKY = sky_paths.BP_SKY
ENGINE_SPHERE = "/Engine/BasicShapes/Sphere"

SKY_MESH_PATHS = sky_paths.candidate_paths("SkyDomeMesh")
SKY_MAT_PATHS = sky_paths.candidate_paths("SkyMaterial")
SKY_PARENT_PATHS = (
    f"{sky_paths.SKY_ROOT}/Materials/M_Sky",
    f"{sky_paths.SKY_ROOT}/Materials/M_StylizedSky",
    "/Game/SoStylized/Environment/Sky/Materials/M_StylizedSky",
)
MPC_PATHS = sky_paths.MPC_PATHS


def load_first(*paths):
    for path in paths:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            return unreal.EditorAssetLibrary.load_asset(path), path
    return None, None


def mesh_path(mesh):
    if not mesh:
        return None
    try:
        return mesh.get_path_name()
    except Exception:
        return str(mesh)


def is_bad_sky_mesh(mesh):
    path = mesh_path(mesh) or ""
    if not path:
        return True
    if path.endswith("Sphere.Sphere") or "/Engine/BasicShapes/Sphere" in path:
        return True
    if "SkyDome" in path or "StylizedSkyDome" in path:
        return False
    return True


def repair_sky_mi(mi_path, ref_path=None):
    mi, _ = load_first(mi_path)
    ref = None
    if ref_path:
        ref, _ = load_first(ref_path)
    if not ref:
        for candidate in SKY_MAT_PATHS:
            if candidate != mi_path:
                ref, _ = load_first(candidate)
                if ref:
                    break
    if not mi:
        return False

    changed = False
    parent = mi.get_editor_property("parent")
    parent_path = parent.get_path_name() if parent else ""
    if parent and "Lite" in parent_path:
        classic_parent, _ = load_first(*SKY_PARENT_PATHS)
        if classic_parent:
            mi.set_editor_property("parent", classic_parent)
            changed = True
            unreal.log(f"MI parent Lite -> Classic on {mi_path}")

    if ref:
        try:
            names = unreal.MaterialEditingLibrary.get_texture_parameter_names(ref)
        except Exception:
            names = []
        for pname in names:
            try:
                ref_tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
                    ref, pname
                )
                cur_tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
                    mi, pname
                )
            except Exception:
                continue
            if ref_tex and (not cur_tex or cur_tex.get_path_name() != ref_tex.get_path_name()):
                unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                    mi, pname, ref_tex
                )
                changed = True
                unreal.log(f"MI texture {pname} <- {ref_tex.get_name()}")

    for name, val in (("Sky Brightness", 1.5), ("Nebula Multiply", 0.0)):
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, name, val)
            changed = True
        except Exception:
            pass

    if changed:
        unreal.EditorAssetLibrary.save_asset(mi_path)
    return changed


def dump_actor(actor):
    label = actor.get_actor_label()
    cls = actor.get_class().get_name()
    unreal.log(f"=== ACTOR {label} ({cls}) ===")
    for prop in (
        "bFrozenTime",
        "FrozenTimeOfDayHours",
        "LocalTimeOfDayHours",
        "bDriveFromGameState",
        "SkyDomeScale",
    ):
        try:
            unreal.log(f"  {prop}={actor.get_editor_property(prop)}")
        except Exception:
            pass
    try:
        sky_mat = actor.get_editor_property("SkyMaterial")
        unreal.log(f"  SkyMaterial={sky_mat.get_path_name() if sky_mat else None}")
    except Exception:
        pass

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.get_editor_property("static_mesh")
        mat = c.get_material(0)
        vis = c.get_visible_flag()
        scale = c.get_relative_scale3d()
        bad_mesh = is_bad_sky_mesh(mesh)
        unreal.log(
            f"  COMP {c.get_name()} mesh={mesh_path(mesh)} bad_mesh={bad_mesh} "
            f"mat={mat.get_path_name() if mat else None} vis={vis} scale={scale}"
        )


def configure_dome_component(c):
    for prop, val in (
        ("reverse_culling", True),
        ("bounds_scale", 1000.0),
        ("cast_shadow", False),
    ):
        try:
            c.set_editor_property(prop, val)
        except Exception:
            pass
    try:
        c.set_force_disable_nanite(True)
    except Exception:
        try:
            c.set_editor_property("bDisallowNanite", True)
        except Exception:
            pass


def fix_component(c, sky_mesh, sky_mat, cloud_mesh, cloud_mat, sun_mesh, sun_mat, moon_mat):
    name = c.get_name()
    if "Cloud" in name or "SkyDomeClouds" in name:
        configure_dome_component(c)
        if cloud_mesh:
            c.set_static_mesh(cloud_mesh)
        if cloud_mat:
            c.set_material(0, cloud_mat)
        c.set_relative_scale3d(unreal.Vector(40, 40, 40))
        c.set_visibility(True, True)
        c.set_hidden_in_game(False, True)
        return

    if "SkyDome" in name and "Cloud" not in name:
        configure_dome_component(c)
        if sky_mesh:
            c.set_static_mesh(sky_mesh)
        if sky_mat:
            c.set_material(0, sky_mat)
            try:
                mid = c.create_dynamic_material_instance(0, sky_mat)
                if mid:
                    mid.set_scalar_parameter_value("Sky Brightness", 1.5)
            except Exception:
                pass
        c.set_relative_scale3d(unreal.Vector(1, 1, 1))
        c.set_visibility(True, True)
        c.set_hidden_in_game(False, True)
        return

    if "Sun" in name and "Moon" not in name:
        if sun_mesh:
            c.set_static_mesh(sun_mesh)
        if sun_mat:
            c.set_material(0, sun_mat)
    elif "Moon" in name:
        if sun_mesh:
            c.set_static_mesh(sun_mesh)
        if moon_mat:
            c.set_material(0, moon_mat)


def run():
    midday = 12.0
    worlds = md_sky_mpc.all_world_contexts()
    unreal.log(f"world contexts={len(worlds)}")

    for path in MPC_PATHS:
        mpc = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if mpc:
            md_sky_mpc.set_mpc_asset_defaults(mpc, midday)
            md_sky_mpc.push_midday_scalars(
                mpc, midday, day_start=0.0, shader_offset=12.0
            )

    if SKY_MAT_PATHS:
        ref_path = SKY_MAT_PATHS[1] if len(SKY_MAT_PATHS) > 1 else None
        repair_sky_mi(SKY_MAT_PATHS[0], ref_path)

    sky_mesh, sky_mesh_path = load_first(*SKY_MESH_PATHS)
    sky_mat, sky_mat_path = load_first(*SKY_MAT_PATHS)
    cloud_mesh, _ = load_first(*sky_paths.candidate_paths("CloudDomeMesh"))
    cloud_mat, _ = load_first(*sky_paths.candidate_paths("CloudMaterial"))
    sun_mesh, _ = load_first(f"{sky_paths.SKY_ROOT}/Meshes/SM_SunAndMoonPlane")
    sun_mat, _ = load_first(f"{sky_paths.SKY_ROOT}/Materials/Presets/MI_Sun_Classic")
    moon_mat, _ = load_first(
        f"{sky_paths.SKY_ROOT}/Materials/Presets/MI_Moon_Classic",
        "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_Moon_Classic",
    )

    unreal.log(f"using sky_mesh={sky_mesh_path} sky_mat={sky_mat_path}")

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = []
    for actor in eas.get_all_level_actors():
        blob = f"{actor.get_actor_label()}|{actor.get_class().get_name()}"
        if "BP_Sky" in blob or "MD_StylizedSky" in blob:
            actors.append(actor)

    if not actors:
        unreal.log_warning("no BP_Sky actor in level")

    for actor in actors:
        dump_actor(actor)
        for prop, val in (
            ("bFrozenTime", True),
            ("bSuppressLevelSkyActors", True),
            ("FrozenTimeOfDayHours", midday),
            ("LocalTimeOfDayHours", midday),
            ("bDriveFromGameState", False),
            ("SkyDomeScale", unreal.Vector(1, 1, 1)),
            ("CloudDomeScale", unreal.Vector(40, 40, 40)),
            ("MpcDayStartTimeHours", 0.0),
            ("MpcShaderTimeOffsetHours", 12.0),
        ):
            try:
                actor.set_editor_property(prop, val)
            except Exception:
                pass
        if sky_mesh:
            try:
                actor.set_editor_property("SkyDomeMesh", sky_mesh)
            except Exception:
                pass
        if sky_mat:
            try:
                actor.set_editor_property("SkyMaterial", sky_mat)
            except Exception:
                pass
        for c in actor.get_components_by_class(unreal.StaticMeshComponent):
            fix_component(c, sky_mesh, sky_mat, cloud_mesh, cloud_mat, sun_mesh, sun_mat, moon_mat)
        try:
            actor.call_method("RefreshSkyNow")
        except Exception:
            try:
                actor.rerun_construction_scripts()
            except Exception as e:
                unreal.log_warning(f"refresh failed: {e}")
        dump_actor(actor)

    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        if cls in ("SkyAtmosphere", "DirectionalLight", "SkyLight"):
            a.set_actor_hidden_in_game(True)
            try:
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("DIAGNOSE_FIX_BLACK_SKY_OK — stop PIE (Esc) if running, then check editor viewport")


run()
