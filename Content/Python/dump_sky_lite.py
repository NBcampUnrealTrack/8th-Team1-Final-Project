"""Inspect Lite sky materials — may be usable without full day-cycle MPC."""
import unreal

for path in (
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedSky_Lite",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/MI_StylizedSky_Lite",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedClouds_Lite",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/MI_StylizedClouds_Lite",
):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log(f"missing {path}")
        continue
    a = unreal.EditorAssetLibrary.load_asset(path)
    unreal.log(f"=== {path} ({a.get_class().get_name()}) ===")
    if isinstance(a, unreal.MaterialInstance):
        try:
            unreal.log(f"  parent={a.get_editor_property('parent').get_path_name()}")
        except Exception:
            pass
        try:
            for p in unreal.MaterialEditingLibrary.get_texture_parameter_names(a):
                t = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(a, p)
                unreal.log(f"  tex {p}={t.get_path_name() if t else None}")
        except Exception as e:
            unreal.log(str(e))
        try:
            for p in unreal.MaterialEditingLibrary.get_scalar_parameter_names(a):
                v = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(a, p)
                unreal.log(f"  sc {p}={v}")
        except Exception:
            pass
    if isinstance(a, unreal.Material):
        for e in unreal.MaterialEditingLibrary.get_material_expressions(a):
            try:
                pn = e.get_editor_property("parameter_name")
                if pn:
                    unreal.log(f"  param {pn}")
            except Exception:
                pass
            try:
                t = e.get_editor_property("texture")
                if t:
                    unreal.log(f"  hardtex {t.get_path_name()}")
            except Exception:
                pass
unreal.log("LITE_DUMP_OK")
