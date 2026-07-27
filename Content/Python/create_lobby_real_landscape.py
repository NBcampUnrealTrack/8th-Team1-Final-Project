"""One-shot: create LobbyLandscape via CDO.call_method (works after Live Coding)."""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
R16 = r"E:/Unreal/MarineDemolition/Saved/HM_Lobby_505.r16"

if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in list(actor_sub.get_all_level_actors()):
    label = a.get_actor_label() or ""
    if label.startswith("LobbyTerrain") or label == "LobbyLandscape" or a.get_class().get_name() == "Landscape":
        actor_sub.destroy_actor(a)
        continue
    mesh = a.get_component_by_class(unreal.StaticMeshComponent)
    if mesh:
        sm = mesh.get_editor_property("static_mesh")
        if sm and "LobbyTerrain" in sm.get_name():
            actor_sub.destroy_actor(a)

cls = unreal.load_class(None, "/Script/MarineDemolition.MD_LandscapeLibrary")
if not cls:
    raise RuntimeError("MD_LandscapeLibrary missing — Live Coding/compile first")

cdo = unreal.get_default_object(cls)
unreal.log(f"CDO={cdo} methods={[n for n in dir(cdo) if 'andscape' in n.lower() or 'create' in n.lower()]}")

# UE Python: static UFUNCTIONs on BlueprintFunctionLibrary CDOs use call_method with PascalCase
landscape = cdo.call_method(
    "CreateLandscapeFromR16",
    (
        R16,
        505,
        505,
        63,
        1,
        8,
        8,
        unreal.Vector(100.0, 100.0, 100.0),
        unreal.Vector(0.0, 0.0, 0.0),
        "LobbyLandscape",
    ),
)

if not landscape:
    raise RuntimeError("CreateLandscapeFromR16 returned None")

try:
    landscape.set_folder_path("Terrain")
except Exception:
    pass

unreal.EditorLevelLibrary.save_current_level()
unreal.EditorAssetLibrary.save_asset(LEVEL)
unreal.log(f"SUCCESS LobbyLandscape={landscape.get_actor_label()}")
