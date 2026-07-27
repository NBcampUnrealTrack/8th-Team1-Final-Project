"""
Tune BP_Sky cloud size — strength only (never touch Stretch Vertical).

Run:
  py "E:/Unreal/MarineDemolition/Content/Python/tune_cloud_scale.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths", "md_sky_component"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)

LEVEL = paths.LEVEL_LOBBY
BP_SKY = paths.BP_SKY

# Smaller = lower strength. Stretch Vertical changes UV mapping — do not edit here.
STRENGTH_ONLY = {
    "1 Strength": 0.95,
    "2 Strength": 0.88,
    "3 Strength": 0.82,
    "4 Strength": 0.75,
}


def apply_strength(mi) -> None:
    for name, val in STRENGTH_ONLY.items():
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mi, name, val
            )
        except Exception:
            pass


def run() -> None:
    unreal.log_warning(
        "tune_cloud_scale.py is deprecated — use restore_cloud_classic_defaults.py"
    )
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception:
        pass
    mi_path = paths.PATHS["CloudMaterial"]
    if unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        mi = unreal.EditorAssetLibrary.load_asset(mi_path)
        apply_strength(mi)
        unreal.EditorAssetLibrary.save_asset(mi_path)
    unreal.log("TUNE_CLOUD_SCALE_OK (strength only)")


run()
