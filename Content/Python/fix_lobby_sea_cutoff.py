"""Fix LobbySea cutoff: center on map, massively expand extents, set far mesh."""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
SEA_LABEL = "LobbySea"
ZONE_LABEL = "LobbyWaterZone"
WATER_Z = 4302.8  # keep pier 2/3 height


def find(label):
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if (a.get_actor_label() or "") == label:
            return a
    return None


def find_far_mat():
    candidates = [
        "/Water/Materials/WaterSurface/Water_FarMesh",
        "/Water/Materials/WaterSurface/MI_Water_FarMesh",
        "/Water/Materials/WaterSurface/Water_Material",
        "/Water/Materials/WaterSurface/WaterInfo/WaterInfoMaterial",
    ]
    # search /Water for far
    assets = unreal.EditorAssetLibrary.list_assets("/Water/Materials", True, False)
    far_hits = [a for a in assets if "far" in a.lower()]
    unreal.log("far hits=" + ", ".join(far_hits[:20]))
    for p in candidates + far_hits:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            return unreal.EditorAssetLibrary.load_asset(p)
    return None


sea = find(SEA_LABEL)
zone = find(ZONE_LABEL)
if not sea or not zone:
    raise RuntimeError(f"missing sea/zone sea={sea} zone={zone}")

# Center on world/landscape origin so coverage is symmetric
center = unreal.Vector(0.0, 0.0, WATER_Z)
sea.set_actor_location(center, False, True)
zone.set_actor_location(center, False, True)

# Huge extents (cm) — cover landscape (~113km? actually ~1.1km) many times over
HALF = 500000.0  # 5 km
ocomp = sea.get_component_by_class(unreal.WaterBodyOceanComponent)
ocomp.set_editor_property("affects_landscape", False)
ocomp.set_editor_property("ocean_extents", unreal.Vector2D(HALF, HALF))
ocomp.set_editor_property("collision_extents", unreal.Vector(HALF, HALF, 50000.0))
try:
    ocomp.set_editor_property("always_generate_water_mesh_tiles", True)
except Exception as e:
    unreal.log(f"always_generate: {e}")

zone.set_editor_property("zone_extent", unreal.Vector2D(HALF * 2.0, HALF * 2.0))

mesh = zone.get_editor_property("water_mesh")
if mesh:
    # Larger tiles / more tessellation reduces visible edge
    try:
        mesh.set_editor_property("tile_size", 4800.0)
    except Exception as e:
        unreal.log(f"tile_size: {e}")
    try:
        mesh.set_editor_property("tessellation_factor", 12)
    except Exception as e:
        unreal.log(f"tess: {e}")
    try:
        mesh.set_editor_property("far_distance_mesh_extent", 5000000.0)
    except Exception as e:
        unreal.log(f"far extent: {e}")
    try:
        mesh.set_editor_property("lod_scale", 0.75)
    except Exception:
        pass

far_mat = find_far_mat()
if far_mat:
    try:
        zone.set_far_mesh_material(far_mat)
        unreal.log(f"set_far_mesh_material {far_mat.get_name()}")
    except Exception as e:
        unreal.log(f"set_far_mesh_material: {e}")
    try:
        zone.set_editor_property("far_distance_material", far_mat)
    except Exception:
        pass

# Neutralize curve again
try:
    curve = ocomp.get_editor_property("curve_settings")
    curve.set_editor_property("use_curve_channel", False)
    curve.set_editor_property("channel_depth", 0.0)
    ocomp.set_editor_property("curve_settings", curve)
except Exception:
    pass

# Kill brush managers if any
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in list(sub.get_all_level_actors()):
    if a.get_class().get_name() == "WaterBrushManager":
        sub.destroy_actor(a)

try:
    sea.post_edit_change()
    zone.post_edit_change()
    if mesh:
        mesh.mark_render_state_dirty()
except Exception:
    pass

unreal.log(f"ocean_extents={ocomp.get_editor_property('ocean_extents')}")
unreal.log(f"zone_extent={zone.get_editor_property('zone_extent')}")
unreal.log(f"sea_loc={sea.get_actor_location()}")
unreal.log(f"affects={ocomp.get_editor_property('affects_landscape')}")

# Verify landscape unchanged
for a in sub.get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        origin, extent = a.get_actor_bounds(False)
        unreal.log(f"ls z=[{origin.z-extent.z:.1f}..{origin.z+extent.z:.1f}]")

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.EditorAssetLibrary.save_asset(LEVEL)
unreal.log("SEA_EXTENT_FIX_OK")
