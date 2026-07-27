"""
Import all varied sky textures from Art/_sostylized_varied into owned BP_Sky paths.

Generate PNGs first:
  python "E:/Unreal/MarineDemolition/Art/Textures/Sky/vary_sostylized_sky_textures.py"

Run in editor (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/import_varied_sostylized_sky_textures.py"
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import unreal

import md_sky_paths as paths

ART = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_sostylized_varied")
GEN = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/vary_sostylized_sky_textures.py")
ROOT = paths.SKY_ROOT

# rel_path under SKY_ROOT -> sRGB
IMPORTS: tuple[tuple[str, bool], ...] = (
    ("Textures/CloudLayers/T_CloudLayer01", True),
    ("Textures/CloudLayers/T_CloudLayer13", True),
    ("Textures/CloudLayers/T_CloudLayer_Overhead04", True),
    ("Textures/BackgroundClouds/T_BackroundClouds1A", False),
    ("Textures/Noise/T_NoiseRough", False),
    ("Textures/Noise/T_NoiseRough02", False),
    ("Textures/Noise/T_NoiseSmooth", False),
    ("Textures/T_SkyCurve_Gradient", True),
    ("Textures/T_Skylight_HDR", True),
    ("Textures/T_SkylightReflection_HDR", True),
    ("Textures/Space/T_Sun01_BC", True),
    ("Textures/Space/T_Stars01_BC", True),
    ("Textures/Space/T_Nebula", True),
    ("Textures/Space/T_Moon01_BC", True),
)


def ensure_generated() -> None:
    need = ART / "BackgroundClouds" / "T_BackroundClouds1A.png"
    if not need.is_file():
        unreal.log("Generating varied sky textures...")
        subprocess.check_call([sys.executable, str(GEN)])


def ensure_dir(package_folder: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(package_folder):
        unreal.EditorAssetLibrary.make_directory(package_folder)


def png_for_rel(rel: str) -> Path:
    name = rel.rsplit("/", 1)[-1]
    sub = rel[len("Textures/") : -len(f"/{name}")] if rel.startswith("Textures/") else ""
    if sub:
        return ART / sub / f"{name}.png"
    return ART / f"{name}.png"


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


def fix_sun_mi_path() -> None:
    sun_mi = f"{ROOT}/Materials/Presets/MI_Sun_Classic"
    owned_sun = f"{ROOT}/Textures/Space/T_Sun01_BC"
    if not unreal.EditorAssetLibrary.does_asset_exist(sun_mi):
        return
    if not unreal.EditorAssetLibrary.does_asset_exist(owned_sun):
        return
    mi = unreal.EditorAssetLibrary.load_asset(sun_mi)
    tex = unreal.EditorAssetLibrary.load_asset(owned_sun)
    for pname in ("Body Texture", "Sun Texture"):
        try:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, pname, tex)
        except Exception:
            pass
    unreal.EditorAssetLibrary.save_asset(sun_mi)
    unreal.log(f"wire {sun_mi} -> {owned_sun}")


def recompile_materials() -> None:
    for mat in (
        f"{ROOT}/Materials/M_StylizedSky",
        f"{ROOT}/Materials/M_StylizedClouds",
        f"{ROOT}/Materials/M_CelestialBody",
        f"{ROOT}/Materials/Presets/MI_StylizedSky_Classic",
        f"{ROOT}/Materials/Presets/MI_StylizedClouds_Classic",
        f"{ROOT}/Materials/Presets/MI_Sun_Classic",
    ):
        if not unreal.EditorAssetLibrary.does_asset_exist(mat):
            continue
        m = unreal.EditorAssetLibrary.load_asset(mat)
        if isinstance(m, unreal.Material):
            unreal.MaterialEditingLibrary.recompile_material(m)
        unreal.EditorAssetLibrary.save_asset(mat)
        unreal.log(f"recompiled {mat}")


def refresh_bp_sky() -> None:
    bp = paths.BP_SKY
    if not unreal.EditorAssetLibrary.does_asset_exist(bp):
        return
    bp_obj = unreal.EditorAssetLibrary.load_asset(bp)
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp_obj)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_asset(bp)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if "BP_Sky" not in actor.get_class().get_name():
            continue
        try:
            actor.call_method("RefreshSkyNow")
        except Exception:
            pass


def main() -> None:
    ensure_generated()
    n = 0
    for rel, srgb in IMPORTS:
        png = png_for_rel(rel)
        if not png.is_file():
            unreal.log_warning(f"missing PNG {png}")
            continue
        name = rel.rsplit("/", 1)[-1]
        sub = rel[len("Textures/") : -len(f"/{name}")]
        dest = f"{ROOT}/Textures/{sub}" if sub else f"{ROOT}/Textures"
        if import_png(png, dest, name, srgb):
            n += 1

    fix_sun_mi_path()
    recompile_materials()
    refresh_bp_sky()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"IMPORT_VARIED_SOSTYLIZED_SKY_TEXTURES_OK count={n}")


if __name__ == "__main__":
    main()
