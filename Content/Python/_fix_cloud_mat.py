import unreal

SKY_MI = "/Game/Developers/TaitoP/Environment/Sky/Owned/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic"
CLOUD_MI = "/Game/Developers/TaitoP/Environment/Sky/Owned/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic"
# fallback SoStylized
if not unreal.EditorAssetLibrary.does_asset_exist(CLOUD_MI):
    CLOUD_MI = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic"

sky_mi = unreal.EditorAssetLibrary.load_asset(SKY_MI)
cloud_mi = unreal.EditorAssetLibrary.load_asset(CLOUD_MI)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") != "LobbyStylizedSky":
        continue
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name().lower()
        if n == "skydome":
            mid = c.create_dynamic_material_instance(0, sky_mi)
            if mid:
                try:
                    mid.set_scalar_parameter_value("Nebula Multiply", 0.0)
                    mid.set_scalar_parameter_value("Star Brightness", 0.0)
                except Exception:
                    pass
            unreal.log("SkyDome -> sky MI nebula0")
        elif "cloud" in n:
            if cloud_mi:
                c.set_material(0, cloud_mi)
                unreal.log("SkyDomeClouds -> cloud MI")
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
loc, rot = ues.get_level_viewport_camera_info()
ues.set_level_viewport_camera_info(loc, unreal.Rotator(pitch=-6.0, yaw=float(rot.yaw), roll=0.0))
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("FIX_CLOUD_MAT_OK")
