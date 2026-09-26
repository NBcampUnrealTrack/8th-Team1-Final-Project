import unreal

PUMP_BP = "/Game/Blueprint/Quest/Day3/BP_MD_CoolingPump"
ZONE_BP = "/Game/Blueprint/Quest/Day3/BP_MD_IsolationBHazard"
MAP_PATH = "/Game/Maps/L_Ocean"

pump_bp = unreal.load_asset(PUMP_BP)
zone_bp = unreal.load_asset(ZONE_BP)
if pump_bp is None or zone_bp is None:
    raise RuntimeError("Missing Day3 cooling blueprint")

pump_cdo = unreal.get_default_object(pump_bp.generated_class())
components = pump_cdo.get_components_by_class(unreal.ActorComponent)
component_names = [c.get_name() for c in components]

link = pump_cdo.get_editor_property("link_trigger")
unreal.log_warning("COOLING_VERIFY pump_components=" + ",".join(component_names))
unreal.log_warning("COOLING_VERIFY start_broken=" + str(pump_cdo.get_editor_property("start_broken")))
unreal.log_warning("COOLING_VERIFY link_extent=" + str(link.get_editor_property("box_extent")))
unreal.log_warning("COOLING_VERIFY link_overlap=" + str(link.get_editor_property("generate_overlap_events")))
unreal.log_warning("COOLING_VERIFY link_collision=" + str(link.get_collision_enabled()))

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if world is None:
    raise RuntimeError("Could not load " + MAP_PATH)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actor_subsystem.get_all_level_actors():
    class_name = actor.get_class().get_name()
    if "IsolationBHazard" in class_name:
        pump_ref = actor.get_editor_property("cooling_pump")
        unreal.log_warning(
            "COOLING_VERIFY zone_actor=" + actor.get_actor_label()
            + " pump_ref=" + str(pump_ref)
        )
    elif "CoolingPump" in class_name:
        unreal.log_warning(
            "COOLING_VERIFY pump_actor=" + actor.get_actor_label()
            + " class=" + class_name
        )
