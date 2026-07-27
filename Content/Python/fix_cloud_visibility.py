"""
Fix invisible / faint clouds on BP_Sky only (no sun/moon / day-cycle changes).

Run (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/fix_cloud_visibility.py"
"""
from __future__ import annotations

import importlib
import sys
from pathlib import Path

import unreal

for _name in ("md_sky_paths", "md_sky_component", "md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp
import md_sky_import as md_sky_mpc

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
md_sky_mpc = importlib.reload(md_sky_mpc)

BP_SKY = paths.BP_SKY
LEVEL = paths.LEVEL_LOBBY
SKY_ROOT = paths.SKY_ROOT
CLOUD_SCALE = unreal.Vector(40.0, 40.0, 40.0)

ART_VARIED = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_sostylized_varied")
ART_REF = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_ref_sostylized")

CLOUD_STRIPS = (
    "T_CloudLayer01",
    "T_CloudLayer13",
    "T_CloudLayer_Overhead04",
)

PARENT_CLOUD_MATS = (
    f"{SKY_ROOT}/Materials/M_StylizedClouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
    "/Game/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
)

CLOUD_MIS = tuple(dict.fromkeys(paths.candidate_paths("CloudMaterial")))

CLOUD_TEX_PARAMS = (
    ("1 Cloud Texture", "T_CloudLayer_Overhead04"),
    ("2 Cloud Texture", "T_CloudLayer01"),
    ("3 Cloud Texture", "T_CloudLayer13"),
    ("4 Cloud Texture", "T_CloudLayer01"),
)

# Classic pack stretch values — raising Stretch Vertical causes zenith streak artifacts.
CLOUD_MI_SCALARS = {
    "1 Strength": 0.95,
    "2 Strength": 0.88,
    "3 Strength": 0.82,
    "4 Strength": 0.75,
    "1 Stretch Vertical": 0.392,
    "2 Stretch Vertical": 0.424,
    "3 Stretch Vertical": 0.408,
    "4 Stretch Vertical": 1.0,
    "Highlight Strength": 1.7,
    "Highlight Threshold": 0.6,
    "Noise Strength": 0.85,
}

CLOUD_MI_SWITCHES = {
    "AddLayer2?": True,
    "AddLayer3?": True,
    "AddLayer4?": False,
}


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
    if asset and key == "CloudDomeMesh" and path and "Stylized" not in path:
        stylized, sp = load_first(
            f"{SKY_ROOT}/Meshes/SM_StylizedSkyDome_Clouds",
            "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
            "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
        )
        if stylized:
            unreal.log_warning(f"cloud mesh fallback {path} -> {sp}")
            return stylized
    return asset


def pick_png(stem: str) -> str:
    for folder in (ART_VARIED, ART_REF):
        p = folder / f"{stem}.png"
        if p.exists():
            return str(p).replace("\\", "/")
    raise FileNotFoundError(stem)


def import_cloud_strips() -> int:
    dest = f"{SKY_ROOT}/Textures/CloudLayers"
    if not unreal.EditorAssetLibrary.does_directory_exist(dest):
        unreal.EditorAssetLibrary.make_directory(dest)
    n = 0
    for stem in CLOUD_STRIPS:
        src = pick_png(stem)
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = dest
        task.destination_name = stem
        task.replace_existing = True
        task.automated = True
        task.save = True
        task.factory = unreal.TextureFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.EditorAssetLibrary.load_asset(f"{dest}/{stem}")
        if tex:
            tex.set_editor_property("srgb", True)
            tex.set_editor_property(
                "compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT
            )
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
            unreal.log(f"cloud strip {dest}/{stem} <- {src}")
            n += 1
    return n


def wire_cloud_mi_textures(mi_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    for param_name, stem in CLOUD_TEX_PARAMS:
        tex_path = f"{SKY_ROOT}/Textures/CloudLayers/{stem}"
        if not unreal.EditorAssetLibrary.does_asset_exist(tex_path):
            continue
        tex = unreal.EditorAssetLibrary.load_asset(tex_path)
        try:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                mi, param_name, tex
            )
            unreal.log(f"wire {mi_path} {param_name} <- {stem}")
        except Exception:
            for tp in list(mi.texture_parameter_values):
                pname = str(
                    tp.get_editor_property("parameter_info").get_editor_property("name")
                )
                if pname == param_name:
                    tp.set_editor_property("parameter_value", tex)
                    unreal.log(f"wire {mi_path} {param_name} <- {stem}")
                    break
    unreal.EditorAssetLibrary.save_asset(mi_path)


def boost_cloud_mi(mi_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    for name, val in CLOUD_MI_SCALARS.items():
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mi, name, val
            )
        except Exception:
            pass
    for name, val in CLOUD_MI_SWITCHES.items():
        try:
            unreal.MaterialEditingLibrary.set_material_instance_static_switch_parameter_value(
                mi, name, val
            )
        except Exception:
            pass
    try:
        overrides = mi.get_editor_property("base_property_overrides")
        overrides.set_editor_property("override_two_sided", True)
        overrides.set_editor_property("two_sided", True)
        mi.set_editor_property("base_property_overrides", overrides)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_asset(mi_path)
    unreal.log(f"boost cloud MI {mi_path}")


def push_cloud_mpc() -> None:
    mpc = load_key("EnvironmentMPC")
    if not mpc:
        return
    md_sky_mpc.set_mpc_asset_defaults(mpc, 12.0)
    md_sky_mpc.push_midday_scalars(mpc, 12.0)
    unreal.log("pushed MPC for cloud day curves")


def set_parent_two_sided(mat_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        return
    mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    try:
        mat.set_editor_property("two_sided", True)
        unreal.EditorAssetLibrary.save_asset(mat_path)
        unreal.log(f"two_sided=True {mat_path}")
    except Exception as e:
        unreal.log_warning(f"two_sided {mat_path}: {e}")


def bypass_dither(mat_path: str) -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        return False
    mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    sat = None
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        name = e.get_name()
        if "Saturate_0" in name or name.endswith("Saturate_0"):
            sat = e
            break
    if not sat:
        for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
            if "Saturate" in e.get_name():
                sat = e
                break
    if not sat:
        unreal.log_warning(f"no Saturate on {mat_path}")
        return False
    unreal.MaterialEditingLibrary.connect_material_property(
        sat, "", unreal.MaterialProperty.MP_OPACITY
    )
    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(mat_path)
    unreal.log(f"bypassed dither on {mat_path}")
    return True


def bypass_cloud_dither() -> None:
    for mat_path in PARENT_CLOUD_MATS:
        bypass_dither(mat_path)


def configure_cloud_component(c) -> None:
    for prop, val in (
        ("reverse_culling", True),
        ("bounds_scale", 1000.0),
        ("cast_shadow", False),
        ("visible", True),
        ("hidden_in_game", False),
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
    c.set_visibility(True, True)
    c.set_hidden_in_game(False, True)


def apply_cloud_dome(actor, cloud_mesh, cloud_mat) -> None:
    sky_comp.set_sky_prop(actor, "CloudDomeMesh", cloud_mesh)
    sky_comp.set_sky_prop(actor, "CloudMaterial", cloud_mat)
    sky_comp.set_sky_prop(actor, "CloudDomeScale", CLOUD_SCALE)

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        if "SkyDomeClouds" not in n and "Cloud" not in n:
            continue
        configure_cloud_component(c)
        if cloud_mesh and isinstance(cloud_mesh, unreal.StaticMesh):
            c.set_static_mesh(cloud_mesh)
        if cloud_mat:
            c.set_material(0, cloud_mat)
        c.set_relative_scale3d(CLOUD_SCALE)
        mesh = c.get_editor_property("static_mesh")
        mat = c.get_material(0)
        unreal.log(
            f"  cloud dome {n} mesh={mesh.get_name() if mesh else None} "
            f"mat={mat.get_name() if mat else None} scale={CLOUD_SCALE}"
        )

    try:
        actor.call_method("RefreshSkyNow")
    except Exception:
        pass
    try:
        actor.rerun_construction_scripts()
    except Exception:
        pass


def is_bp_sky_actor(actor) -> bool:
    cls = actor.get_class().get_name()
    label = actor.get_actor_label() or ""
    return "BP_Sky" in cls or label == "BP_Sky"


def run() -> None:
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception as e:
        unreal.log_warning(f"load_level: {e}")

    n = import_cloud_strips()
    unreal.log(f"imported {n} cloud strips")

    for mat_path in PARENT_CLOUD_MATS:
        set_parent_two_sided(mat_path)
        if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
            mat = unreal.EditorAssetLibrary.load_asset(mat_path)
            unreal.MaterialEditingLibrary.recompile_material(mat)
            unreal.EditorAssetLibrary.save_asset(mat_path)

    bypass_cloud_dither()

    push_cloud_mpc()

    for mi_path in CLOUD_MIS:
        wire_cloud_mi_textures(mi_path)
        boost_cloud_mi(mi_path)

    cloud_mesh = load_key("CloudDomeMesh")
    cloud_mat, cloud_mat_path = load_first(*paths.candidate_paths("CloudMaterial"))
    if not cloud_mesh:
        unreal.log_error("CloudDomeMesh missing")
    if not cloud_mat:
        unreal.log_error("CloudMaterial missing")
    else:
        unreal.log(f"cloud mat -> {cloud_mat_path}")

    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
        cdo = unreal.get_default_object(bp.generated_class())
        apply_cloud_dome(cdo, cloud_mesh, cloud_mat)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log_warning(f"compile BP_Sky: {e}")
        unreal.EditorAssetLibrary.save_asset(BP_SKY)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if not is_bp_sky_actor(actor):
            continue
        unreal.log(f"fix clouds on {actor.get_actor_label()}")
        apply_cloud_dome(actor, cloud_mesh, cloud_mat)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("FIX_CLOUD_VISIBILITY_OK")


run()
