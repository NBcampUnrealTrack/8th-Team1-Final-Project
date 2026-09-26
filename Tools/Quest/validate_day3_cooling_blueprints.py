"""Read-only reload validation; does not save assets or maps."""
import unreal

pump_bp = unreal.load_asset("/Game/Blueprint/Quest/Day3/BP_MD_CoolingPump")
zone_bp = unreal.load_asset("/Game/Blueprint/Quest/Day3/BP_MD_IsolationBHazard")
assert pump_bp and zone_bp, "Both placement Blueprints must exist"
pump = unreal.get_default_object(pump_bp.generated_class())
zone = unreal.get_default_object(zone_bp.generated_class())
assert pump.get_editor_property("start_broken"), "Pump must start broken"
assert pump.get_editor_property("repair_requires_tool_input"), "Free E repair must be disabled"
mesh_component = pump.get_component_by_class(unreal.StaticMeshComponent)
mesh = mesh_component.get_editor_property("static_mesh")
assert mesh and "Coolingmachine_vr2Edit" in mesh.get_path_name(), "Saved mesh missing"
fx = pump.get_component_by_class(unreal.NiagaraComponent)
assert fx and fx.get_editor_property("asset"), "Leak Niagara missing"
assert not fx.get_editor_property("auto_activate"), "FX must be state controlled"
assert zone.get_editor_property("damage_per_second") == 8.0
assert zone.get_editor_property("damage_interval") == 1.0
box = zone.get_component_by_class(unreal.BoxComponent)
assert box and box.get_editor_property("generate_overlap_events")
for bp in [pump_bp, zone_bp]:
    unreal.log("COOLING_BP_STATUS " + bp.get_name() + " " + str(bp.get_editor_property("status")))
body = mesh.get_editor_property("body_setup")
geometry = body.get_editor_property("agg_geom")
counts = {name: len(geometry.get_editor_property(name)) for name in ["box_elems", "sphere_elems", "sphyl_elems", "convex_elems"]}
unreal.log("COOLING_MESH_COLLISION " + str(counts) + " mode=" + str(body.get_editor_property("collision_trace_flag")))
unreal.log("COOLING_MESH_BOUNDS " + str(mesh.get_bounding_box()))
unreal.log("COOLING_BP_RELOAD_PASS mesh=" + mesh.get_path_name() + " fx=" + fx.get_editor_property("asset").get_path_name())
