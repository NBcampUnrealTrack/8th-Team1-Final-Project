"""Tune LobbySea WaterBodyOceanComponent extent / verify placement."""
import unreal

SEA_LABEL = "LobbySea"
ZONE_LABEL = "LobbyWaterZone"

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
sea = zone = None
for a in actors:
    label = a.get_actor_label() or ""
    if label == SEA_LABEL:
        sea = a
    elif label == ZONE_LABEL:
        zone = a

unreal.log(f"sea={sea} zone={zone}")
if not sea:
    raise RuntimeError("LobbySea missing")

loc = sea.get_actor_location()
unreal.log(f"sea_loc=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})")

comp = sea.get_component_by_class(unreal.WaterBodyOceanComponent)
unreal.log(f"comp={comp}")

# Dump editable props containing extent / collision / size
for prop in (
    "collision_height_offset",
    "collision_extents",
    "ocean_extent",
    "target_wave_mask_depth",
    "max_wave_height_offset",
    "water_heightmap",
    "layer_weightmap_settings",
):
    try:
        unreal.log(f"comp.{prop}={comp.get_editor_property(prop)}")
    except Exception as e:
        unreal.log(f"comp.{prop}: {e}")

# Try set collision extents on component
for prop, val in (
    ("collision_extents", unreal.Vector(100000.0, 100000.0, 10000.0)),
):
    try:
        comp.set_editor_property(prop, val)
        unreal.log(f"set comp.{prop}={val}")
    except Exception as e:
        unreal.log(f"set {prop}: {e}")

if zone:
    zloc = zone.get_actor_location()
    unreal.log(f"zone_loc=({zloc.x:.1f},{zloc.y:.1f},{zloc.z:.1f})")
    for prop in ("zone_extent", "far_distance_mesh_extent", "overlap_radius", "water_mesh"):
        try:
            unreal.log(f"zone.{prop}={zone.get_editor_property(prop)}")
        except Exception as e:
            unreal.log(f"zone.{prop}: {e}")
    try:
        zone.set_editor_property("zone_extent", unreal.Vector2D(150000.0, 150000.0))
    except Exception as e:
        unreal.log(f"zone extent set: {e}")

# List dir of component for extent-like methods
names = [n for n in dir(comp) if any(k in n.lower() for k in ("extent", "collision", "ocean", "size", "radius"))]
unreal.log("comp methods=" + ", ".join(names[:40]))

try:
    sea.post_edit_change()
    if zone:
        zone.post_edit_change()
except Exception:
    pass

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("SEA_TUNE_OK")
