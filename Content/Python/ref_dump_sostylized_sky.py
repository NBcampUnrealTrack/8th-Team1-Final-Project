"""Reference-only: dump SoStylized sky/cloud/sun material params & texture slots (no copy)."""
import unreal

MATS = [
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedSky",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_CelestialBody",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_Sun_Classic",
]

MESHES = [
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_SunAndMoonPlane",
]


def dump_mat(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return
    mat = unreal.EditorAssetLibrary.load_asset(path)
    unreal.log(f"=== {path} ({mat.get_class().get_name()}) ===")
    try:
        unreal.log(
            f"  blend={mat.get_editor_property('blend_mode')} "
            f"shading={mat.get_editor_property('shading_model')} "
            f"two_sided={mat.get_editor_property('two_sided')}"
        )
    except Exception:
        pass
    if isinstance(mat, unreal.MaterialInstance):
        try:
            parent = mat.get_editor_property("parent")
            unreal.log(f"  parent={parent.get_path_name() if parent else None}")
        except Exception:
            pass
        # static/texture/scalar/vector params via MaterialEditingLibrary
        try:
            for p in unreal.MaterialEditingLibrary.get_scalar_parameter_names(mat):
                v = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(mat, p)
                unreal.log(f"  scalar {p}={v}")
        except Exception as e:
            unreal.log(f"  scalar err {e}")
        try:
            for p in unreal.MaterialEditingLibrary.get_vector_parameter_names(mat):
                v = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(mat, p)
                unreal.log(f"  vector {p}={v}")
        except Exception as e:
            unreal.log(f"  vector err {e}")
        try:
            for p in unreal.MaterialEditingLibrary.get_texture_parameter_names(mat):
                v = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, p)
                unreal.log(f"  texture {p}={v.get_path_name() if v else None}")
        except Exception as e:
            unreal.log(f"  texture err {e}")
    if isinstance(mat, unreal.Material):
        for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
            try:
                pname = e.get_editor_property("parameter_name")
                if pname:
                    cls = e.get_class().get_name()
                    extra = ""
                    try:
                        extra = f" def={e.get_editor_property('default_value')}"
                    except Exception:
                        pass
                    unreal.log(f"  param {pname} ({cls}){extra}")
            except Exception:
                pass
            try:
                tex = e.get_editor_property("texture")
                if tex:
                    unreal.log(f"  hardtex {tex.get_name()}")
            except Exception:
                pass


def dump_mesh(path):
    m = unreal.EditorAssetLibrary.load_asset(path)
    if not m:
        return
    try:
        box = m.get_bounding_box()
        unreal.log(f"mesh {path} box={box}")
    except Exception:
        unreal.log(f"mesh {path} loaded")
    try:
        unreal.log(f"  materials={m.get_num_sections(0) if hasattr(m,'get_num_sections') else '?'}")
    except Exception:
        pass


for p in MATS:
    dump_mat(p)
for p in MESHES:
    dump_mesh(p)
unreal.log("REF_DUMP_OK")
