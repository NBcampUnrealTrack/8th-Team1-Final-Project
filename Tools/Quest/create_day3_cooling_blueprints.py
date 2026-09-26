"""Run in Unreal Editor Python AFTER a cold C++ build. Creates new assets only."""
import os
import sys
import unreal

project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
sys.path.insert(0, os.path.join(project_dir, "Tools", "MultiUser"))
import mu_guard
mu_guard.assert_safe("compile_blueprint")

asset_dir = "/Game/Blueprint/Quest/Day3"
specs = [
    ("BP_MD_CoolingPump", "/Script/MarineDemolition.MD_CoolingPump"),
    ("BP_MD_IsolationBHazard", "/Script/MarineDemolition.MD_CoolingHazardZone"),
]
parents = []
for name, native_path in specs:
    if unreal.EditorAssetLibrary.does_asset_exist(asset_dir + "/" + name):
        raise RuntimeError("Existing asset preserved: " + asset_dir + "/" + name)
    parent = unreal.load_class(None, native_path)
    if parent is None:
        raise RuntimeError("Cold build and restart required: " + native_path)
    parents.append(parent)

# Use the project's cooling-machine model; require a real StaticMesh before creating anything.
mesh_path = "/Game/Assets/X7/Interior/Objects/Coolingmachine_vr2Edit"
mesh = unreal.load_asset(mesh_path)
if not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError("Choose a verified cooling-pump StaticMesh for mesh_path: " + mesh_path)

tools = unreal.AssetToolsHelpers.get_asset_tools()
created = []
for (name, _), parent in zip(specs, parents):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    bp = tools.create_asset(name, asset_dir, unreal.Blueprint, factory)
    if bp is None:
        raise RuntimeError("Could not create " + name)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    default = unreal.get_default_object(bp.generated_class())
    if name == "BP_MD_CoolingPump":
        component = default.get_component_by_class(unreal.StaticMeshComponent)
        if component is None:
            raise RuntimeError("Native pump Mesh component missing")
        component.set_static_mesh(mesh)
        component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        default.set_editor_property("start_broken", True)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False):
        raise RuntimeError("Save failed: " + name)
    if name == "BP_MD_CoolingPump":
        saved_default = unreal.get_default_object(bp.generated_class())
        if saved_default.get_component_by_class(unreal.StaticMeshComponent).get_editor_property("static_mesh") != mesh:
            raise RuntimeError("Pump mesh default did not survive compilation")
    created.append(bp.get_path_name())

# No map is opened, modified, or saved by this script.
unreal.log("Day3 cooling assets created: " + ", ".join(created))
