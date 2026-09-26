import unreal

QUEST_PATH = "/Game/Data/Quest/X7/Main/X7-M03"
SPAWNER_PATH = "/Game/Blueprint/Sample/BP_QuestSampleSpawner"
DAY3_SPAWNER_PATH = "/Game/Blueprint/Quest/Day3/BP_MD_IsolationBSampleSpawner"
SAMPLE_PATH = "/Game/Blueprint/Sample/BP_SampleBase"

quest = unreal.load_asset(QUEST_PATH)
spawner_bp = unreal.load_asset(SPAWNER_PATH)
day3_spawner_bp = unreal.load_asset(DAY3_SPAWNER_PATH)
sample_bp = unreal.load_asset(SAMPLE_PATH)

if quest is None:
    raise RuntimeError("Missing quest: " + QUEST_PATH)
if spawner_bp is None:
    raise RuntimeError("Missing spawner: " + SPAWNER_PATH)
if day3_spawner_bp is None:
    raise RuntimeError("Missing Day3 spawner: " + DAY3_SPAWNER_PATH)
if sample_bp is None:
    raise RuntimeError("Missing sample: " + SAMPLE_PATH)

phases = quest.get_editor_property("phases")
unreal.log_warning("DAY3_SAMPLE QUEST=" + QUEST_PATH + " phases=" + str(len(phases)))
for index, phase in enumerate(phases):
    required_class = phase.get_editor_property("required_item_class")
    required_definition = phase.get_editor_property("required_item_definition")
    accepted = phase.get_editor_property("accepted_grade_tags")
    unreal.log_warning(
        "DAY3_SAMPLE PHASE "
        + str(index)
        + " objective=" + str(phase.get_editor_property("objective_type"))
        + " count=" + str(phase.get_editor_property("required_count"))
        + " zone=" + str(phase.get_editor_property("spawn_zone_tag").get_editor_property("tag_name"))
        + " class=" + str(required_class)
        + " definition=" + str(required_definition)
        + " grades=" + str(accepted)
        + " hud=" + str(phase.get_editor_property("hud_description"))
    )

spawner_default = unreal.get_default_object(spawner_bp.generated_class())
unreal.log_warning(
    "DAY3_SAMPLE SPAWNER sample_class=" + str(spawner_default.get_editor_property("sample_class"))
    + " count=" + str(spawner_default.get_editor_property("samples_to_spawn"))
    + " zone=" + str(spawner_default.get_editor_property("spawn_zone_tag"))
    + " protection=" + str(spawner_default.get_editor_property("sample_spawn_protection_duration"))
)

day3_default = unreal.get_default_object(day3_spawner_bp.generated_class())
unreal.log_warning(
    "DAY3_SAMPLE DAY3_SPAWNER count=" + str(day3_default.get_editor_property("samples_to_spawn"))
    + " zone=" + str(day3_default.get_editor_property("spawn_zone_tag").get_editor_property("tag_name"))
)
unreal.log_warning("DAY3_SAMPLE SAMPLE_CLASS=" + str(sample_bp.generated_class()))

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/L_Ocean")
if world is None:
    raise RuntimeError("Could not load /Game/Maps/L_Ocean")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actor_subsystem.get_all_level_actors():
    class_name = actor.get_class().get_name()
    label = actor.get_actor_label()
    if "QuestSampleSpawner" in class_name or "IsolationBSampleSpawner" in class_name:
        unreal.log_warning(
            "DAY3_SAMPLE MAP_SPAWNER label=" + label
            + " class=" + class_name
            + " location=" + str(actor.get_actor_location())
            + " zone=" + str(actor.get_editor_property("spawn_zone_tag").get_editor_property("tag_name"))
        )
        continue

    mesh_path = ""
    if isinstance(actor, unreal.StaticMeshActor):
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        mesh = component.get_editor_property("static_mesh") if component is not None else None
        mesh_path = mesh.get_path_name() if mesh is not None else ""

    searchable = (label + " " + actor.get_name() + " " + class_name + " " + mesh_path).lower()
    if "isolation" not in searchable and "samplestorage" not in searchable:
        continue

    unreal.log_warning(
        "DAY3_SAMPLE MAP_HINT label=" + label
        + " class=" + class_name
        + " location=" + str(actor.get_actor_location())
        + " rotation=" + str(actor.get_actor_rotation())
        + " scale=" + str(actor.get_actor_scale3d())
        + " bounds=" + str(actor.get_actor_bounds(False))
        + " mesh=" + mesh_path
    )
