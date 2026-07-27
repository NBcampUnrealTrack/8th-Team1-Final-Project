import unreal

sea = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if (a.get_actor_label() or "") == "LobbySea":
        sea = a
        break

comp = sea.get_component_by_class(unreal.WaterBodyOceanComponent)
try:
    unreal.log("ocean_extents before=" + str(comp.get_editor_property("ocean_extents")))
except Exception as e:
    unreal.log("get ocean_extents: " + str(e))

try:
    comp.set_editor_property("ocean_extents", unreal.Vector2D(100000.0, 100000.0))
    unreal.log("set ocean_extents ok")
except Exception as e:
    unreal.log("set ocean_extents: " + str(e))

loc = sea.get_actor_location()
unreal.log("final sea Z=%.1f" % loc.z)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("DONE")
