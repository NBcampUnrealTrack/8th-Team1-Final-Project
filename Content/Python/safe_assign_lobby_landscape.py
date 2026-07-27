"""Safe assign only — no weightmap RT (that crashes Landscape)."""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MIC = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
MASTER = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"


def run():
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    master = unreal.EditorAssetLibrary.load_asset(MASTER)
    mic = unreal.EditorAssetLibrary.load_asset(MIC)
    if not master or not mic:
        raise RuntimeError("master/mic missing")

    parent = mic.get_editor_property("parent")
    unreal.log(f"MIC parent={parent.get_path_name() if parent else None}")
    if parent != master:
        unreal.MaterialEditingLibrary.set_material_instance_parent(mic, master)
        unreal.EditorAssetLibrary.save_asset(MIC)

    landscape = None
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            landscape = a
            break
    if not landscape:
        raise RuntimeError("no landscape")

    landscape.set_editor_property("landscape_material", mic)
    try:
        landscape.post_edit_change()
    except Exception:
        pass

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    cur = landscape.get_editor_property("landscape_material")
    unreal.log(f"SAFE_OK assigned={cur.get_name() if cur else None} actor={landscape.get_actor_label()}")


run()
