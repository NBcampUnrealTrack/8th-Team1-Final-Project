import unreal

MPC = "/Game/Developers/TaitoP/Environment/Sky/MPC_LobbySky"
mpc = unreal.EditorAssetLibrary.load_asset(MPC)
vals = {
    "DayCycleProgress": 0.48,
    "DayLengthSeconds": 600.0,
    "SkyBrightness": 1.35,
    "BGCloudsStrength": 0.42,
    "CloudStrength": 0.85,
    "SunStrength": 10.0,
    "Frozen": 1.0,
    "PhaseRow": 0.0,
}
arr = []
for name, val in vals.items():
    p = unreal.CollectionScalarParameter()
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", float(val))
    arr.append(p)
mpc.set_editor_property("scalar_parameters", arr)
unreal.EditorAssetLibrary.save_loaded_asset(mpc)

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") != "LobbyOriginalSky":
        continue
    # hide oversized sun billboard if it reads as a grey slab
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if "Sun" in c.get_name():
            c.set_visibility(True)
            c.set_editor_property("relative_scale3d", unreal.Vector(180.0, 180.0, 180.0))
            c.set_editor_property("relative_location", unreal.Vector(52000.0, 0.0, 8000.0))
    sl = a.get_component_by_class(unreal.SkyLightComponent)
    if sl:
        try:
            sl.set_editor_property("intensity", 1.4)
            sl.recapture_sky()
        except Exception:
            pass

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("MPC_CLOUD_BOOST_OK")
