"""Inspect remaining BP_StylizedSky meshes + editable day-cycle properties."""
import unreal

BP = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/BP_StylizedSky"
bp = unreal.EditorAssetLibrary.load_asset(BP)
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
bfl = unreal.SubobjectDataBlueprintFunctionLibrary

for h in sub.k2_gather_subobject_data_for_blueprint(bp):
    data = bfl.get_data(h)
    try:
        obj = bfl.get_associated_object(data)
    except Exception:
        obj = bfl.get_object(data)
    if not obj:
        continue
    name = obj.get_name()
    cls = obj.get_class().get_name()
    if "StaticMesh" in cls or "Mesh" in cls or "Billboard" in cls:
        extra = ""
        try:
            mesh = obj.get_editor_property("static_mesh")
            extra += f" mesh={mesh.get_name() if mesh else None}"
        except Exception:
            pass
        try:
            mat = obj.get_material(0)
            extra += f" mat={mat.get_name() if mat else None}"
        except Exception:
            pass
        try:
            loc = obj.get_editor_property("relative_location")
            rot = obj.get_editor_property("relative_rotation")
            sc = obj.get_editor_property("relative_scale3d")
            extra += f" loc={loc} rot={rot} scale={sc}"
        except Exception:
            pass
        unreal.log(f"MESH {name} ({cls}){extra}")
    elif any(k in cls for k in ("Light", "Fog", "PostProcess", "Arrow", "Audio", "Niagara")):
        unreal.log(f"SYS {name} ({cls})")

# CDO defaults / exposed props
try:
    gen = unreal.EditorAssetLibrary.load_blueprint_class(BP)
    cdo = unreal.get_default_object(gen)
    unreal.log(f"CDO={cdo}")
    # dump interesting property names
    for prop in (
        "TimeOfDay",
        "time_of_day",
        "DayLength",
        "SkyPreset",
        "WeatherState",
        "weather_state",
        "AnimateTimeOfDay",
        "bAnimateTimeOfDay",
        "SunAngle",
        "CloudCoverage",
        "UseLiteMaterials",
    ):
        try:
            v = cdo.get_editor_property(prop)
            unreal.log(f"  prop {prop}={v}")
        except Exception:
            pass
    # list all editable-looking
    for attr in dir(cdo):
        if attr.startswith("_"):
            continue
        low = attr.lower()
        if any(k in low for k in ("time", "day", "weather", "cloud", "sun", "moon", "preset", "animate", "orbit")):
            try:
                v = getattr(cdo, attr)
                if callable(v):
                    continue
                unreal.log(f"  attr {attr}={v}")
            except Exception:
                pass
except Exception as e:
    unreal.log_warning(f"cdo err {e}")

unreal.log("INSPECT2_OK")
