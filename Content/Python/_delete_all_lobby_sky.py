"""Delete Lobby sky we created (not Owned/SoStylized reference)."""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
SKY = "/Game/Developers/TaitoP/Environment/Sky"
MAT = f"{SKY}/Materials"
TEX = f"{SKY}/Textures"

# Explicit assets we authored
ASSETS = [
    f"{SKY}/BP_LobbyOriginalSky",
    f"{SKY}/BP_LobbyStylizedSky",
    f"{SKY}/MPC_LobbySky",
    f"{MAT}/M_LobbyOriginalSky",
    f"{MAT}/M_LobbyOriginalClouds",
    f"{MAT}/M_LobbyOriginalSun",
    f"{MAT}/M_LobbyStylizedSky",
    f"{MAT}/M_LobbyStylizedClouds",
    f"{MAT}/M_LobbySun",
    f"{MAT}/M_LobbySunDisc",
    f"{MAT}/MI_LobbyStylizedSky",
    f"{MAT}/MI_LobbyStylizedClouds",
    f"{MAT}/MI_LobbySunDisc",
    f"{MAT}/M_LobbyOriginalSky",
    # bisect / test junk
    f"{MAT}/M_Bisect_A_AtlasHeight",
    f"{MAT}/M_Bisect_B_OnePanner",
    f"{MAT}/M_Bisect_C_SunEmissive",
    f"{MAT}/M_Bisect_D_SunOpacity",
    f"{MAT}/M_Bisect_E_BaseAndEmissive",
    f"{MAT}/M_TestSun_ScalarOnly",
    f"{MAT}/M_TestSun_WithVectorMPC",
    f"{MAT}/M_TestSun_VectorWithId",
]

TEX_NAMES = (
    "T_LobbySky_CurveAtlas",
    "T_LobbySky_HeightGradient",
    "T_LobbyBGClouds",
    "T_LobbyCloud_L1",
    "T_LobbyCloud_L2",
    "T_LobbyCloud_L3",
    "T_LobbySun_Body",
    "T_LobbySky_Noise",
    "T_LobbySky_Gradient",
    "T_LobbyClouds_Mask",
    "T_LobbyClouds_Noise",
    "T_LobbySun_Disc",
)


def destroy_actors():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    n = 0
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if any(k in lab or k in cls for k in ("LobbyOriginalSky", "LobbyStylizedSky", "BP_LobbyOriginal", "BP_LobbyStylized")):
            eas.destroy_actor(a)
            n += 1
            unreal.log(f"destroyed {lab}")
    return n


def delete_asset(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        ok = unreal.EditorAssetLibrary.delete_asset(path)
        unreal.log(f"del {path} -> {ok}")
        return bool(ok)
    return False


def run():
    unreal.log("DELETE_LOBBY_SKY_START")
    n = destroy_actors()
    unreal.log(f"actors={n}")

    deleted = 0
    for p in ASSETS:
        if delete_asset(p):
            deleted += 1

    # any leftover Lobby* / Bisect* / TestSun* in Materials
    if unreal.EditorAssetLibrary.does_directory_exist(MAT):
        for p in unreal.EditorAssetLibrary.list_assets(MAT, False, False):
            pkg = p.split(".")[0]
            name = pkg.rsplit("/", 1)[-1]
            if name.startswith(("M_Lobby", "MI_Lobby", "M_Bisect", "M_TestSun")):
                if delete_asset(pkg):
                    deleted += 1

    for name in TEX_NAMES:
        if delete_asset(f"{TEX}/{name}"):
            deleted += 1

    if unreal.EditorAssetLibrary.does_directory_exist(TEX):
        for p in unreal.EditorAssetLibrary.list_assets(TEX, False, False):
            pkg = p.split(".")[0]
            name = pkg.rsplit("/", 1)[-1]
            if "Lobby" in name or name.startswith("T_Lobby"):
                if delete_asset(pkg):
                    deleted += 1

    # BP / MPC leftovers at Sky root (not Owned)
    if unreal.EditorAssetLibrary.does_directory_exist(SKY):
        for p in unreal.EditorAssetLibrary.list_assets(SKY, False, False):
            pkg = p.split(".")[0]
            name = pkg.rsplit("/", 1)[-1]
            if name.startswith(("BP_Lobby", "MPC_Lobby")):
                if delete_asset(pkg):
                    deleted += 1

    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    except Exception as e:
        unreal.log_warning(f"save: {e}")

    left_mat = []
    if unreal.EditorAssetLibrary.does_directory_exist(MAT):
        left_mat = [p.split(".")[0].rsplit("/", 1)[-1] for p in unreal.EditorAssetLibrary.list_assets(MAT, False, False)]
    left_tex = []
    if unreal.EditorAssetLibrary.does_directory_exist(TEX):
        left_tex = [p.split(".")[0].rsplit("/", 1)[-1] for p in unreal.EditorAssetLibrary.list_assets(TEX, False, False) if "Lobby" in p]

    unreal.log(f"DELETE_LOBBY_SKY_OK deleted={deleted} left_mat={left_mat} left_lobby_tex={left_tex}")


run()
