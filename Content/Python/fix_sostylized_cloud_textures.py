"""
Restore SoStylized Classic cloud layers to proper 8192x1024 strip format.

Use when square lobby masks were wrongly imported (concentric ring clouds).
Sources (in order):
  1) Art/Textures/Sky/_sostylized_varied  — subtle pattern shift
  2) Art/Textures/Sky/_ref_sostylized     — original pack export
"""
from __future__ import annotations

from pathlib import Path

import unreal

ART_VARIED = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_sostylized_varied")
ART_REF = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_ref_sostylized")
TEX_ROOTS = [
    "/Game/SoStylized/Environment/Sky/Textures/CloudLayers",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers",
]

MAPPING = [
    "T_CloudLayer01",
    "T_CloudLayer13",
    "T_CloudLayer_Overhead04",
]


def pick_png(stem: str) -> str:
    for folder in (ART_VARIED, ART_REF):
        p = folder / f"{stem}.png"
        if p.exists():
            return str(p).replace("\\", "/")
    raise FileNotFoundError(stem)


def ensure_folder(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def import_strip(src: str, dest_root: str, name: str) -> None:
    ensure_folder(dest_root)
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = dest_root
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(f"{dest_root}/{name}")
    if tex:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
        unreal.log(f"imported {dest_root}/{name} <- {src}")


for stem in MAPPING:
    src = pick_png(stem)
    for root in TEX_ROOTS:
        import_strip(src, root, stem)

for mat_path in (
    "/Game/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
):
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        mat = unreal.EditorAssetLibrary.load_asset(mat_path)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_asset(mat_path)
        unreal.log(f"recompiled {mat_path.split('/')[-1]}")

for mi in (
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
):
    if unreal.EditorAssetLibrary.does_asset_exist(mi):
        unreal.EditorAssetLibrary.save_asset(mi)

# Ensure BP_Sky uses Classic cloud MI, not lobby square-mask material
BP = "/Game/Developers/TaitoP/BP_Sky"
CLASSIC = "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic"
if unreal.EditorAssetLibrary.does_asset_exist(BP):
    bp = unreal.EditorAssetLibrary.load_asset(BP)
    cdo = unreal.get_default_object(bp.generated_class())
    cloud_mi = unreal.EditorAssetLibrary.load_asset(CLASSIC)
    if cloud_mi:
        try:
            cdo.set_editor_property("CloudMaterial", cloud_mi)
        except Exception as e:
            unreal.log_warning(f"CloudMaterial: {e}")
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_asset(BP)

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("FIX_SOSTYLIZED_CLOUD_TEXTURES_OK")
