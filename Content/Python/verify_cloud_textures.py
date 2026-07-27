"""Log cloud texture sources wired on MI + import path hint."""
import unreal

MI = "/Game/Developers/TaitoP/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic"
TEX_ROOT = "/Game/Developers/TaitoP/Environment/Sky/Textures/CloudLayers"

if unreal.EditorAssetLibrary.does_asset_exist(MI):
    mi = unreal.EditorAssetLibrary.load_asset(MI)
    for p in unreal.MaterialEditingLibrary.get_texture_parameter_names(mi):
        tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, p)
        path = tex.get_path_name() if tex else None
        unreal.log(f"MI texture {p}={path}")

for stem in ("T_CloudLayer01", "T_CloudLayer13", "T_CloudLayer_Overhead04"):
    p = f"{TEX_ROOT}/{stem}"
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        tex = unreal.EditorAssetLibrary.load_asset(p)
        try:
            src = tex.get_editor_property("asset_import_data")
            fname = src.get_first_filename() if src else ""
        except Exception:
            fname = "?"
        try:
            sz = tex.get_editor_property("source_size")
            size = f"{sz.x}x{sz.y}" if sz else "?"
        except Exception:
            size = "?"
        unreal.log(f"asset {stem} size={size} import={fname}")

unreal.log("VERIFY_CLOUD_TEX_OK")
