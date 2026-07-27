"""
Export SoStylized Classic Day sky textures to Art for visual matching (reference only).
Also dump mesh bounds / BP component scales.
"""
from __future__ import annotations

from pathlib import Path

import unreal

OUT = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky/_ref_sostylized")
OUT.mkdir(parents=True, exist_ok=True)

TEXTS = [
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/T_SkyCurve_Gradient",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/BackgroundClouds/T_BackroundClouds1A",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers/T_CloudLayer_Overhead04",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers/T_CloudLayer01",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers/T_CloudLayer13",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/Space/T_Sun01_BC",
    "/Game/Developers/TaitoP/SoStylized/Textures/Noise/T_NoiseRough",
]


def export_tex(path: str):
    tex = unreal.EditorAssetLibrary.load_asset(path)
    if not tex:
        unreal.log_warning(f"missing {path}")
        return
    name = path.rsplit("/", 1)[-1]
    dest = str(OUT / f"{name}.png").replace("\\", "/")
    # TextureExporterPNG
    try:
        task = unreal.AssetExportTask()
        task.set_editor_property("object", tex)
        task.set_editor_property("filename", dest)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("prompt", False)
        exporter = unreal.TextureExporterPNG()
        task.set_editor_property("exporter", exporter)
        ok = unreal.Exporter.run_asset_export_task(task)
        unreal.log(f"export {name} -> {ok} {dest}")
    except Exception as e:
        unreal.log_warning(f"export fail {name}: {e}")


for p in TEXTS:
    export_tex(p)

# Mesh bounds
for mp in (
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_SunAndMoonPlane",
):
    m = unreal.EditorAssetLibrary.load_asset(mp)
    if m:
        unreal.log(f"mesh {mp} box={m.get_bounding_box()}")

unreal.log("REF_EXPORT_OK")
