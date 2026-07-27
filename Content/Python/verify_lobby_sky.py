"""Quick verify BP_LobbyStylizedSky components."""
import unreal

BP_PATH = "/Game/Developers/TaitoP/Environment/Sky/BP_LobbyStylizedSky"
bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
bfl = unreal.SubobjectDataBlueprintFunctionLibrary
handles = sub.k2_gather_subobject_data_for_blueprint(bp)
unreal.log(f"handles={len(handles)}")
for h in handles:
    data = bfl.get_data(h)
    try:
        obj = bfl.get_associated_object(data)
    except Exception:
        obj = bfl.get_object(data)
    name = obj.get_name() if obj else "?"
    cls = obj.get_class().get_name() if obj else "?"
    extra = ""
    if obj and isinstance(obj, unreal.StaticMeshComponent):
        mesh = obj.get_editor_property("static_mesh")
        mat0 = obj.get_material(0)
        scale = obj.get_relative_scale3d()
        extra = (
            f" mesh={mesh.get_name() if mesh else None}"
            f" mat={mat0.get_name() if mat0 else None}"
            f" scale={scale}"
        )
    unreal.log(f"  {name} ({cls}){extra}")

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    lab = a.get_actor_label() or ""
    if any(k in lab for k in ("Sky", "Cloud", "Sun", "Atmosphere", "SkyLight")):
        unreal.log(f"actor {lab} {a.get_class().get_name()}")

unreal.log("VERIFY_SKY_OK")
