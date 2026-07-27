"""Recreate LobbyLandscape from smoothed heightmap r16."""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
R16 = r"E:/Unreal/MarineDemolition/Saved/HM_Lobby_505_smoothed.r16"


def run():
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(actor_sub.get_all_level_actors()):
        label = a.get_actor_label() or ""
        if label == "LobbyLandscape" or a.get_class().get_name() == "Landscape":
            actor_sub.destroy_actor(a)

    cls = unreal.load_class(None, "/Script/MarineDemolition.MD_LandscapeLibrary")
    cdo = unreal.get_default_object(cls)
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
        raise RuntimeError("CreateLandscapeFromR16 failed")

    try:
        landscape.set_folder_path("Terrain")
    except Exception:
        pass

    unreal.EditorLevelLibrary.save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("SUCCESS: LobbyLandscape rebuilt from smoothed heightmap")


run()
