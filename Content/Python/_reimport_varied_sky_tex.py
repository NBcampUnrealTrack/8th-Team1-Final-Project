"""Reimport varied white-mask cloud textures and refresh LobbyOriginalSky materials."""
import unreal

ART = r"E:/Unreal/MarineDemolition/Art/Textures/Sky"
TEX = "/Game/Developers/TaitoP/Environment/Sky/Textures"
MAPPING = [
    ("T_LobbySky_BGClouds.png", "T_LobbyBGClouds", True),
    ("T_LobbyCloud_Layer01.png", "T_LobbyCloud_L1", True),
    ("T_LobbyCloud_Layer02.png", "T_LobbyCloud_L2", True),
    ("T_LobbyCloud_Layer03.png", "T_LobbyCloud_L3", True),
    ("T_LobbySky_HeightGradient.png", "T_LobbySky_HeightGradient", True),
    ("T_LobbySky_CurveAtlas.png", "T_LobbySky_CurveAtlas", True),
    ("T_LobbySun_Disc.png", "T_LobbySun_Body", True),
]


def import_tex(src, name, srgb=True):
    task = unreal.AssetImportTask()
    task.filename = f"{ART}/{src}".replace("\\", "/")
    task.destination_path = TEX
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(f"{TEX}/{name}")
    if tex:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", srgb)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
        unreal.log(f"reimport {name}")
    return tex


for src, name, srgb in MAPPING:
    import_tex(src, name, srgb)

for p in (
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSky",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalClouds",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSun",
):
    mat = unreal.EditorAssetLibrary.load_asset(p)
    if mat:
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_asset(p)
        unreal.log(f"recompiled {p.split('/')[-1]}")

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") == "LobbyOriginalSky":
        sl = a.get_component_by_class(unreal.SkyLightComponent)
        if sl:
            try:
                sl.recapture_sky()
            except Exception:
                pass

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("REIMPORT_CLOUDS_OK")
