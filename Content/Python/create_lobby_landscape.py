"""
Create L_Lobby, import heightmap terrain mesh, place in level.
Also prints exact Landscape Import settings for a true Landscape actor.

  py "E:/Unreal/MarineDemolition/Content/Python/create_lobby_landscape.py"
"""
import unreal
from pathlib import Path

LEVEL_DIR = "/Game/Developers/TaitoP/Maps"
LEVEL_NAME = "L_Lobby"
LEVEL_PATH = f"{LEVEL_DIR}/{LEVEL_NAME}"

HEIGHTMAP_PNG = r"E:/Unreal/MarineDemolition/Art/Heightmaps/HM_Lobby.png"
TERRAIN_OBJ = r"E:/Unreal/MarineDemolition/Art/Heightmaps/SM_LobbyTerrain.obj"
MESH_DIR = "/Game/Developers/TaitoP/Maps"
MESH_NAME = "SM_LobbyTerrain"
MESH_PATH = f"{MESH_DIR}/{MESH_NAME}"


def ensure_dir(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def create_or_open_level() -> None:
    ensure_dir(LEVEL_DIR)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
        unreal.log(f"Loaded {LEVEL_PATH}")
        return

    try:
        unreal.EditorLevelLibrary.new_level(LEVEL_PATH)
    except Exception:
        factory = unreal.WorldFactory()
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            LEVEL_NAME, LEVEL_DIR, unreal.World, factory
        )
        unreal.EditorAssetLibrary.save_asset(LEVEL_PATH)
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)

    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(f"Created {LEVEL_PATH}")


def import_terrain_mesh():
    if not Path(TERRAIN_OBJ).exists():
        unreal.log_error(f"Missing OBJ: {TERRAIN_OBJ}")
        return None

    task = unreal.AssetImportTask()
    task.filename = TERRAIN_OBJ.replace("\\", "/")
    task.destination_path = MESH_DIR
    task.destination_name = MESH_NAME
    task.replace_existing = True
    task.automated = True
    task.save = True

    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = False
    options.import_materials = False
    options.import_textures = False
    options.static_mesh_import_data.set_editor_property("combine_meshes", True)
    options.static_mesh_import_data.set_editor_property("auto_generate_collision", True)
    options.static_mesh_import_data.set_editor_property("generate_lightmap_u_vs", True)
    # OBJ may use different factory — try Interchange / Automated
    task.options = options

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if unreal.EditorAssetLibrary.does_asset_exist(MESH_PATH):
        unreal.log(f"Imported {MESH_PATH}")
        return unreal.EditorAssetLibrary.load_asset(MESH_PATH)

    # Retry without FBX options (OBJ factory)
    task2 = unreal.AssetImportTask()
    task2.filename = TERRAIN_OBJ.replace("\\", "/")
    task2.destination_path = MESH_DIR
    task2.destination_name = MESH_NAME
    task2.replace_existing = True
    task2.automated = True
    task2.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task2])
    if unreal.EditorAssetLibrary.does_asset_exist(MESH_PATH):
        return unreal.EditorAssetLibrary.load_asset(MESH_PATH)
    unreal.log_error("Terrain mesh import failed")
    return None


def place_mesh(mesh) -> None:
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # Remove previous
    for a in list(actor_sub.get_all_level_actors()):
        if a.get_actor_label().startswith("LobbyTerrain") or a.get_name().startswith("SM_LobbyTerrain"):
            actor_sub.destroy_actor(a)

    actor = actor_sub.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
    if actor:
        actor.set_actor_label("LobbyTerrain_FromHeightmap")
        unreal.log(f"Placed terrain actor: {actor.get_name()}")


def print_landscape_steps() -> None:
    unreal.log(
        "\n=== True Landscape (optional, recommended) ===\n"
        "Modes -> Landscape -> Manage -> Import from File\n"
        f"Heightmap: {HEIGHTMAP_PNG}\n"
        "Section Size=63 | Sections/Component=1 | Components=8x8 (505x505)\n"
        "Scale Z ~ 100-150 -> Import\n"
        "Then you can delete LobbyTerrain_FromHeightmap mesh.\n"
    )


def run() -> None:
    if not Path(HEIGHTMAP_PNG).exists():
        unreal.log_error(f"Missing heightmap {HEIGHTMAP_PNG}")
        return

    create_or_open_level()
    mesh = import_terrain_mesh()
    if mesh:
        place_mesh(mesh)

    unreal.EditorLevelLibrary.save_current_level()
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        unreal.EditorAssetLibrary.save_asset(LEVEL_PATH)

    print_landscape_steps()
    unreal.log(f"Lobby ready: {LEVEL_PATH}")


run()
