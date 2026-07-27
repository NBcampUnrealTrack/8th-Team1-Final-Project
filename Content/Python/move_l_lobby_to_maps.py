"""Move L_Lobby to /Game/Maps/L_Lobby (duplicate fallback if rename fails)."""
import unreal

SRC = "/Game/Developers/TaitoP/Maps/L_Lobby"
DST = "/Game/Maps/L_Lobby"
TERRAIN_SRC = "/Game/Developers/TaitoP/Maps/SM_LobbyTerrain"
TERRAIN_DST = "/Game/Environment/Lobby/Landscape/SM_LobbyTerrain"

if not unreal.EditorAssetLibrary.does_directory_exist("/Game/Maps"):
    unreal.EditorAssetLibrary.make_directory("/Game/Maps")

if unreal.EditorAssetLibrary.does_asset_exist(DST):
    unreal.log(f"level already at {DST}")
elif unreal.EditorAssetLibrary.does_asset_exist(SRC):
    ok = unreal.EditorAssetLibrary.rename_asset(SRC, DST)
    if not ok:
        dup = unreal.EditorAssetLibrary.duplicate_asset(SRC, DST)
        if dup and unreal.EditorAssetLibrary.does_asset_exist(DST):
            unreal.EditorAssetLibrary.save_asset(DST, only_if_is_dirty=False)
            if unreal.EditorAssetLibrary.does_asset_exist(SRC):
                unreal.EditorAssetLibrary.delete_asset(SRC)
            unreal.log(f"duplicated L_Lobby -> {DST}")
        else:
            unreal.log_error(f"failed move {SRC}")
    else:
        unreal.log(f"renamed L_Lobby -> {DST}")

if unreal.EditorAssetLibrary.does_asset_exist(TERRAIN_SRC):
    if not unreal.EditorAssetLibrary.does_directory_exist("/Game/Environment/Lobby/Landscape"):
        unreal.EditorAssetLibrary.make_directory("/Game/Environment/Lobby/Landscape")
    if not unreal.EditorAssetLibrary.does_asset_exist(TERRAIN_DST):
        ok = unreal.EditorAssetLibrary.rename_asset(TERRAIN_SRC, TERRAIN_DST)
        if not ok:
            unreal.EditorAssetLibrary.duplicate_asset(TERRAIN_SRC, TERRAIN_DST)
            unreal.EditorAssetLibrary.delete_asset(TERRAIN_SRC)
        unreal.log(f"moved SM_LobbyTerrain -> {TERRAIN_DST}")

try:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(DST)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
except Exception as e:
    unreal.log_warning(f"save level: {e}")

unreal.log("MOVE_L_LOBBY_OK")
