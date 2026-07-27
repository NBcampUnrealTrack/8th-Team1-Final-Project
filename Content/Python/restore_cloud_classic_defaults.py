"""
Restore SoStylized Classic cloud look + remove broken MID overrides.

Stretch Vertical was wrongly raised (caused zenith streaks). This resets pack-like
Classic scalars, keeps clouds visible with moderate strength only.

Run (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/restore_cloud_classic_defaults.py"
"""
from __future__ import annotations

import importlib
import shutil
import sys
from pathlib import Path

import unreal

for _name in ("md_sky_paths", "md_sky_component"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)

BP_SKY = paths.BP_SKY
LEVEL = paths.LEVEL_LOBBY
SKY_ROOT = paths.SKY_ROOT
CLOUD_SCALE = unreal.Vector(40.0, 40.0, 40.0)

PACK_MI = Path(
    r"D:/UnrealPack/Stylize/SoStylized/Content/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic.uasset"
)
OWNED_MI = Path(
    r"E:/Unreal/MarineDemolition/Content/Developers/TaitoP/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic.uasset"
)

# Classic MI defaults (from pack / pre-tune dump). Do NOT raise Stretch Vertical.
CLASSIC_SCALARS = {
    "1 Offset Vertical": 0.202812,
    "1 Stretch Vertical": 0.392,
    "1 Strength": 0.95,
    "1 Offset Horizontal": 0.0,
    "1 Speed": 2.0,
    "2 Offset Vertical": -0.176,
    "2 Stretch Vertical": 0.424,
    "2 Strength": 0.88,
    "2 Offset Horizontal": 0.0,
    "2 Speed": 1.4,
    "3 Offset Vertical": -0.064,
    "3 Stretch Vertical": 0.408,
    "3 Strength": 0.82,
    "3 Offset Horizontal": 0.0,
    "3 Speed": 1.4,
    "4 Offset Vertical": 0.0,
    "4 Stretch Vertical": 1.0,
    "4 Strength": 0.75,
    "4 Offset Horizontal": 0.0,
    "4 Speed": 1.4,
    "Highlight Strength": 1.7,
    "Highlight Threshold": 0.6,
    "Highlight Falloff": 3.0,
    "Noise Strength": 0.85,
    "Noise Speed": 1.0,
    "Flow Curve": 4.0,
    "Random Flow Speed": 1.0,
    "Day Cloud Curve": 0.0,
    "Night Cloud Curve": 2.0,
    "Sunrise Cloud Curve": 3.0,
    "Sunset Cloud Curve": 1.0,
}

CLASSIC_SWITCHES = {
    "AddLayer2?": True,
    "AddLayer3?": True,
    "AddLayer4?": False,
}

CLOUD_TEX_PARAMS = (
    ("1 Cloud Texture", "T_CloudLayer_Overhead04"),
    ("2 Cloud Texture", "T_CloudLayer01"),
    ("3 Cloud Texture", "T_CloudLayer13"),
    ("4 Cloud Texture", "T_CloudLayer01"),
)


def copy_pack_mi() -> bool:
    if not PACK_MI.exists():
        unreal.log_warning(f"pack MI missing: {PACK_MI}")
        return False
    try:
        shutil.copy2(PACK_MI, OWNED_MI)
        unreal.log(f"copied pack MI -> {OWNED_MI}")
        return True
    except OSError as e:
        unreal.log_warning(f"copy pack MI failed: {e}")
        return False


def load_first(*candidates):
    for p in candidates:
        if not unreal.EditorAssetLibrary.does_asset_exist(p):
            continue
        asset = unreal.EditorAssetLibrary.load_asset(p)
        if asset and asset.get_class().get_name() != "ObjectRedirector":
            return asset, p
    return None, None


def wire_textures(mi_path: str) -> None:
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
        except Exception:
            pass
    unreal.EditorAssetLibrary.save_asset(mi_path)


def apply_classic(mi) -> None:
    for name, val in CLASSIC_SCALARS.items():
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mi, name, val
            )
        except Exception:
            pass
    for name, val in CLASSIC_SWITCHES.items():
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


def apply_cloud_dome(actor, cloud_mesh, cloud_mat) -> None:
    sky_comp.set_sky_prop(actor, "CloudDomeMesh", cloud_mesh)
    sky_comp.set_sky_prop(actor, "CloudMaterial", cloud_mat)
    sky_comp.set_sky_prop(actor, "CloudDomeScale", CLOUD_SCALE)

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        if "SkyDomeClouds" not in n and "Cloud" not in n:
            continue
        try:
            c.set_editor_property("reverse_culling", True)
            c.set_editor_property("bounds_scale", 1000.0)
        except Exception:
            pass
        c.set_visibility(True, True)
        c.set_hidden_in_game(False, True)
        if cloud_mesh:
            c.set_static_mesh(cloud_mesh)
        if cloud_mat:
            # Use MI directly — no MID (old MID cached bad Stretch values).
            c.set_material(0, cloud_mat)
        c.set_relative_scale3d(CLOUD_SCALE)
        unreal.log(f"  cloud dome {n} mat={cloud_mat.get_name() if cloud_mat else None}")

    try:
        actor.call_method("RefreshSkyNow")
    except Exception:
        pass
    try:
        actor.rerun_construction_scripts()
    except Exception:
        pass


def run() -> None:
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception as e:
        unreal.log_warning(f"load_level: {e}")

    copy_pack_mi()
    mi_path = paths.PATHS["CloudMaterial"]
    if unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        unreal.EditorAssetLibrary.load_asset(mi_path)  # refresh after file copy
        wire_textures(mi_path)
        mi = unreal.EditorAssetLibrary.load_asset(mi_path)
        apply_classic(mi)
        unreal.EditorAssetLibrary.save_asset(mi_path)
        unreal.log(f"restored Classic cloud MI {mi_path}")

    cloud_mesh, _ = load_first(*paths.candidate_paths("CloudDomeMesh"))
    cloud_mat, _ = load_first(*paths.candidate_paths("CloudMaterial"))

    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
        cdo = unreal.get_default_object(bp.generated_class())
        apply_cloud_dome(cdo, cloud_mesh, cloud_mat)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_asset(BP_SKY)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if "BP_Sky" not in actor.get_class().get_name():
            continue
        unreal.log(f"restore clouds on {actor.get_actor_label()}")
        apply_cloud_dome(actor, cloud_mesh, cloud_mat)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("RESTORE_CLOUD_CLASSIC_OK")


run()
