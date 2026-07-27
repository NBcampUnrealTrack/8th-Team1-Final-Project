"""
Import procedural owned sky textures (non-cloud) and recompile sky materials.

Generate PNGs first:
  python E:/Unreal/MarineDemolition/Art/Textures/Sky/build_owned_stylized_sky_textures.py

Then in editor (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/import_owned_stylized_sky_textures.py"
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import unreal

import md_sky_paths as paths

ART = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_owned_stylized")
BUILD = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/build_owned_stylized_sky_textures.py")
ROOT = paths.SKY_ROOT

# rel_path under owned sky → PNG (cloud layers excluded)
IMPORTS: tuple[tuple[str, bool], ...] = (
    ("Textures/T_Skylight_HDR", True),
    ("Textures/T_SkylightReflection_HDR", True),
    ("Textures/T_SkyCurve_Gradient", True),
    ("Textures/Space/T_Sun01_BC", True),
    ("Textures/Space/T_Moon01_BC", True),
    ("Textures/Space/T_Stars01_BC", True),
    ("Textures/Space/T_Nebula", True),
    ("Textures/Noise/T_NoiseRough", False),
    ("Textures/Noise/T_NoiseRough02", False),
    ("Textures/Noise/T_NoiseSmooth", False),
    ("Textures/Noise/T_ChromaNoise_Blurred", True),
    ("Textures/Noise/T_NoisePuddles", False),
    ("Textures/T_WaterRipples", False),
)


def ensure_built() -> None:
    if not ART.is_dir() or not (ART / "T_Skylight_HDR.png").is_file():
        unreal.log("Building owned stylized sky textures...")
        subprocess.check_call([sys.executable, str(BUILD)])


def ensure_dir(package_folder: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(package_folder):
        unreal.EditorAssetLibrary.make_directory(package_folder)


def import_png(png: Path, dest_folder: str, name: str, srgb: bool) -> str | None:
    ensure_dir(dest_folder)
    task = unreal.AssetImportTask()
    task.filename = str(png).replace("\\", "/")
    task.destination_path = dest_folder
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    pkg = f"{dest_folder}/{name}"
    tex = unreal.EditorAssetLibrary.load_asset(pkg)
    if not tex:
        unreal.log_warning(f"import fail {pkg}")
        return None
    tex.set_editor_property("srgb", srgb)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    unreal.log(f"IMPORT {pkg} <- {png.name}")
    return pkg


def recompile_materials() -> None:
    for mat in (
        f"{ROOT}/Materials/M_StylizedSky",
        f"{ROOT}/Materials/M_StylizedClouds",
        f"{ROOT}/Materials/Presets/MI_StylizedSky_Classic",
        f"{ROOT}/Materials/Presets/MI_StylizedClouds_Classic",
        f"{ROOT}/Materials/M_CelestialBody",
    ):
        if not unreal.EditorAssetLibrary.does_asset_exist(mat):
            continue
        m = unreal.EditorAssetLibrary.load_asset(mat)
        if isinstance(m, unreal.Material):
            unreal.MaterialEditingLibrary.recompile_material(m)
        unreal.EditorAssetLibrary.save_asset(mat)
        unreal.log(f"recompiled {mat}")


def main() -> None:
    ensure_built()
    n = 0
    for rel, srgb in IMPORTS:
        name = rel.rsplit("/", 1)[-1]
        sub = rel.rsplit("/", 1)[0] if "/" in rel else ""
        png = ART / f"{sub}/{name}.png" if sub else ART / f"{name}.png"
        if not png.is_file():
            unreal.log_warning(f"missing PNG {png}")
            continue
        dest = f"{ROOT}/{sub}" if sub else ROOT
        if import_png(png, dest, name, srgb):
            n += 1

    recompile_materials()

    bp = paths.BP_SKY
    if unreal.EditorAssetLibrary.does_asset_exist(bp):
        try:
            bp_obj = unreal.EditorAssetLibrary.load_asset(bp)
            unreal.BlueprintEditorLibrary.compile_blueprint(bp_obj)
            unreal.EditorAssetLibrary.save_asset(bp)
        except Exception as e:
            unreal.log_warning(f"BP compile: {e}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"IMPORT_OWNED_STYLIZED_SKY_TEXTURES_OK count={n}")


if __name__ == "__main__":
    main()
