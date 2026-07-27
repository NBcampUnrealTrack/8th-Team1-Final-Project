"""Reimport cloud strips to owned Environment/Sky paths (no SoStylized)."""
import unreal

import md_sky_paths as sky_paths

ART_VARIED = r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_sostylized_varied"
ART_REF = r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_ref_sostylized"
TEX = f"{sky_paths.SKY_ROOT}/Textures"

MAPPING = [
    "T_CloudLayer01",
    "T_CloudLayer13",
    "T_CloudLayer_Overhead04",
]


def pick_png(stem: str) -> str:
    from pathlib import Path

    for folder in (Path(ART_VARIED), Path(ART_REF)):
        p = folder / f"{stem}.png"
        if p.exists():
            return str(p).replace("\\", "/")
    raise FileNotFoundError(stem)


def import_strip(src: str, name: str):
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = f"{TEX}/CloudLayers"
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(f"{TEX}/CloudLayers/{name}")
    if tex:
        tex.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
        unreal.log(f"reimport {name}")


for stem in MAPPING:
    import_strip(pick_png(stem), stem)

for mat in (
    f"{sky_paths.SKY_ROOT}/Materials/M_StylizedClouds",
    f"{sky_paths.SKY_ROOT}/Materials/Presets/MI_StylizedClouds_Classic",
):
    if unreal.EditorAssetLibrary.does_asset_exist(mat):
        m = unreal.EditorAssetLibrary.load_asset(mat)
        unreal.MaterialEditingLibrary.recompile_material(m)
        unreal.EditorAssetLibrary.save_asset(mat)

unreal.log("FIX_OWNED_CLOUD_TEXTURES_OK")
