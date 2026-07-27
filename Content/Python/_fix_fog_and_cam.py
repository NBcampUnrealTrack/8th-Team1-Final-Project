import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") != "LobbyOriginalSky":
        continue
    fog = a.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if fog:
        fog.set_editor_property("fog_density", 0.004)
        fog.set_editor_property("fog_max_opacity", 0.25)
        unreal.log("fog softened")
    # ensure sky meshes visible
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        c.set_visibility(True)
        try:
            c.set_hidden_in_game(False)
        except Exception:
            pass
        unreal.log(f"vis {c.get_name()}")
    sl = a.get_component_by_class(unreal.SkyLightComponent)
    if sl:
        try:
            sl.recapture_sky()
        except Exception:
            pass

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ues.set_level_viewport_camera_info(
    unreal.Vector(0.0, -15000.0, 6000.0),
    unreal.Rotator(pitch=-5.0, yaw=90.0, roll=0.0),
)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("CAM_SKY_VIEW_OK")
