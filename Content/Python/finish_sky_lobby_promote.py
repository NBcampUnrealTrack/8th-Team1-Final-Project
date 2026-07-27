"""Load promoted L_Lobby + BP_MD_StylizedSky, fix redirectors, save (post filesystem restore)."""
import unreal

LEVEL = "/Game/Maps/L_Lobby"
BP_SKY = "/Game/Environment/Sky/BP_MD_StylizedSky"
SKY_ROOT = "/Game/Environment/Sky"
LANDSCAPE_ROOT = "/Game/Environment/Lobby/Landscape"


def fix_redirectors(root: str) -> None:
    try:
        unreal.AssetToolsHelpers.get_asset_tools().fix_redirectors_in_folder(root)
        unreal.log(f"fix_redirectors {root}")
    except Exception as e:
        unreal.log_warning(f"redirectors {root}: {e}")


if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
    unreal.log_error(f"missing level {LEVEL}")
else:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        cls = actor.get_class().get_name()
        label = actor.get_actor_label()
        if "StylizedSky" in cls or "BP_Sky" in cls or label == "BP_Sky":
            unreal.log(f"sky actor: {label} class={cls}")
            try:
                actor.call_method("RefreshSkyNow")
            except Exception:
                pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"saved {LEVEL}")

for root in (SKY_ROOT, LANDSCAPE_ROOT, "/Game/Maps"):
    fix_redirectors(root)

if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
    unreal.EditorAssetLibrary.save_asset(BP_SKY, only_if_is_dirty=False)

unreal.log("FINISH_SKY_LOBBY_PROMOTE_OK")
