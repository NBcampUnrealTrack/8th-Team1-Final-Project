"""Inspect SoStylized BP_StylizedSky architecture for recreation (reference only)."""
import unreal

BP = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/BP_StylizedSky"
bp = unreal.EditorAssetLibrary.load_asset(BP)
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
bfl = unreal.SubobjectDataBlueprintFunctionLibrary
handles = sub.k2_gather_subobject_data_for_blueprint(bp)
unreal.log(f"=== BP_StylizedSky subobjects: {len(handles)} ===")
for h in handles:
    data = bfl.get_data(h)
    try:
        obj = bfl.get_associated_object(data)
    except Exception:
        obj = bfl.get_object(data)
    if not obj:
        continue
    name = obj.get_name()
    cls = obj.get_class().get_name()
    extra = ""
    if isinstance(obj, unreal.StaticMeshComponent):
        mesh = obj.get_editor_property("static_mesh")
        mat = obj.get_material(0)
        scale = obj.get_relative_scale3d()
        loc = obj.get_editor_property("relative_location")
        rot = obj.get_editor_property("relative_rotation")
        extra = (
            f" mesh={mesh.get_path_name() if mesh else None}"
            f" mat={mat.get_path_name() if mat else None}"
            f" scale={scale} loc={loc} rot={rot}"
        )
    elif isinstance(obj, unreal.SceneComponent):
        try:
            loc = obj.get_editor_property("relative_location")
            rot = obj.get_editor_property("relative_rotation")
            scale = obj.get_relative_scale3d()
            extra = f" loc={loc} rot={rot} scale={scale}"
        except Exception:
            pass
    unreal.log(f"  {name} ({cls}){extra}")

# Material params
for mpath in (
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedSky",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedSky_Lite",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/MI_StylizedSky_Lite",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
):
    if not unreal.EditorAssetLibrary.does_asset_exist(mpath):
        continue
    mat = unreal.EditorAssetLibrary.load_asset(mpath)
    unreal.log(f"=== {mpath} ({mat.get_class().get_name()}) ===")
    try:
        unreal.log(f"  blend={mat.get_editor_property('blend_mode')} shading={mat.get_editor_property('shading_model')} two_sided={mat.get_editor_property('two_sided')}")
    except Exception as e:
        unreal.log(f"  props err {e}")
    if isinstance(mat, unreal.Material):
        for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
            try:
                pname = e.get_editor_property("parameter_name")
                if pname:
                    unreal.log(f"  param {pname} ({e.get_class().get_name()})")
            except Exception:
                pass
            try:
                tex = e.get_editor_property("texture")
                if tex:
                    unreal.log(f"  texsample {tex.get_path_name()}")
            except Exception:
                pass
    if isinstance(mat, unreal.MaterialInstanceConstant):
        try:
            parent = mat.get_editor_property("parent")
            unreal.log(f"  parent={parent.get_path_name() if parent else None}")
        except Exception:
            pass

# Find cloud materials nearby
asset_reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = asset_reg.get_assets_by_path("/Game/Developers/TaitoP/SoStylized/Environment/Sky", recursive=True)
for a in assets:
    n = str(a.asset_name)
    if any(k in n.lower() for k in ("cloud", "sun", "moon", "star")):
        unreal.log(f"asset {a.package_name}.{a.asset_name} ({a.asset_class_path.asset_name})")

unreal.log("INSPECT_STYLIZED_SKY_OK")
