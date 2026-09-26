import os
import sys
import unreal

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
sys.path.insert(0, os.path.join(project_dir, "Tools", "MultiUser"))
import mu_guard

mu_guard.assert_safe("compile_blueprint")

QUEST_PATH = "/Game/Data/Quest/X7/Main/X7-M03"
MAP_PATH = "/Game/Maps/L_Ocean"
BASE_SPAWNER_PATH = "/Game/Blueprint/Sample/BP_QuestSampleSpawner"
DAY3_DIR = "/Game/Blueprint/Quest/Day3"
DAY3_SPAWNER_NAME = "BP_MD_IsolationBSampleSpawner"
DAY3_SPAWNER_PATH = DAY3_DIR + "/" + DAY3_SPAWNER_NAME
ZONE_TAG_NAME = "Env.Zone.X7.IsolationB"
TARGET_CABINET_LABEL = "SampleStorageEdit2"

def make_tag(tag_name):
    tag = unreal.GameplayTag()
    if not tag.import_text(tag_name):
        raise RuntimeError("GameplayTag import failed: " + tag_name)
    if not unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag):
        raise RuntimeError("GameplayTag invalid: " + tag_name)
    return tag

quest = unreal.load_asset(QUEST_PATH)
if quest is None:
    raise RuntimeError("Missing quest: " + QUEST_PATH)

phases = quest.get_editor_property("phases")
if len(phases) < 2:
    raise RuntimeError("X7-M03 requires phase index 1")

zone_tag = make_tag(ZONE_TAG_NAME)
old_phase = phases[1]
phase = unreal.MD_QuestPhaseData(
    objective_type=old_phase.get_editor_property("objective_type"),
    required_item_class=old_phase.get_editor_property("required_item_class"),
    required_item_definition=old_phase.get_editor_property("required_item_definition"),
    accepted_grade_tags=unreal.GameplayTagContainer(),
    required_count=old_phase.get_editor_property("required_count"),
    spawn_zone_tag=zone_tag,
    hud_description=old_phase.get_editor_property("hud_description"),
    completion_dialogue=old_phase.get_editor_property("completion_dialogue"),
    phase_completion_reward=old_phase.get_editor_property("phase_completion_reward"),
)
phases[1] = phase
quest.set_editor_property("phases", phases)

if not unreal.EditorAssetLibrary.save_loaded_asset(quest, only_if_is_dirty=False):
    raise RuntimeError("Failed to save " + QUEST_PATH)

if unreal.EditorAssetLibrary.does_asset_exist(DAY3_SPAWNER_PATH):
    spawner_bp = unreal.load_asset(DAY3_SPAWNER_PATH)
else:
    base_bp = unreal.load_asset(BASE_SPAWNER_PATH)
    if base_bp is None:
        raise RuntimeError("Missing base spawner: " + BASE_SPAWNER_PATH)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    duplicated = tools.duplicate_asset(DAY3_SPAWNER_NAME, DAY3_DIR, base_bp)
    if duplicated is None:
        raise RuntimeError("Failed to duplicate Day3 spawner")
    spawner_bp = duplicated

unreal.BlueprintEditorLibrary.compile_blueprint(spawner_bp)
spawner_default = unreal.get_default_object(spawner_bp.generated_class())
spawner_default.set_editor_property("spawn_zone_tag", zone_tag)
spawner_default.set_editor_property("samples_to_spawn", 1)
unreal.BlueprintEditorLibrary.compile_blueprint(spawner_bp)

if not unreal.EditorAssetLibrary.save_loaded_asset(spawner_bp, only_if_is_dirty=False):
    raise RuntimeError("Failed to save " + DAY3_SPAWNER_PATH)

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if world is None:
    raise RuntimeError("Failed to load " + MAP_PATH)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actor_subsystem.get_all_level_actors()

target_cabinet = None
existing_spawner = None
for actor in actors:
    label = actor.get_actor_label()
    if label == TARGET_CABINET_LABEL:
        target_cabinet = actor
    if label == "Day3_IsolationB_SampleSpawner":
        existing_spawner = actor

if target_cabinet is None:
    raise RuntimeError("Target cabinet not found: " + TARGET_CABINET_LABEL)

cabinet_location = target_cabinet.get_actor_location()
spawn_location = unreal.Vector(cabinet_location.x, cabinet_location.y + 38.0, cabinet_location.z + 168.0)

if existing_spawner is None:
    existing_spawner = actor_subsystem.spawn_actor_from_class(
        spawner_bp.generated_class(),
        spawn_location,
        unreal.Rotator(0.0, 0.0, 0.0),
        transient=False,
    )
    if existing_spawner is None:
        raise RuntimeError("Failed to place Day3 spawner")
    existing_spawner.set_actor_label("Day3_IsolationB_SampleSpawner")
else:
    existing_spawner.set_actor_location(spawn_location, False, False)

existing_spawner.set_editor_property("spawn_zone_tag", zone_tag)
existing_spawner.set_editor_property("samples_to_spawn", 1)

if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError("Failed to save map")

saved_phase = quest.get_editor_property("phases")[1]
saved_grades = saved_phase.get_editor_property("accepted_grade_tags")
saved_zone = saved_phase.get_editor_property("spawn_zone_tag")

unreal.log_warning(
    "DAY3_SETUP phase1 zone=" + str(saved_zone)
    + " grades=" + str(saved_grades)
    + " spawner=" + existing_spawner.get_actor_label()
    + " location=" + str(existing_spawner.get_actor_location())
)
