"""Diagnose why LobbySea water cuts off / waterfall edge."""
import unreal

sea = zone = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    lab = a.get_actor_label() or ""
    if lab == "LobbySea":
        sea = a
    elif lab == "LobbyWaterZone":
        zone = a

unreal.log(f"sea={sea} zone={zone}")
if sea:
    loc = sea.get_actor_location()
    unreal.log(f"sea_loc=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})")
    comp = sea.get_component_by_class(unreal.WaterBodyOceanComponent)
    for prop in (
        "collision_extents",
        "ocean_extents",
        "affects_landscape",
        "target_wave_mask_depth",
        "max_wave_height_offset",
        "fill_collision_under_water_bodies_module",
    ):
        try:
            unreal.log(f"ocean.{prop}={comp.get_editor_property(prop)}")
        except Exception as e:
            unreal.log(f"ocean.{prop}: {e}")
    names = [n for n in dir(comp) if any(k in n.lower() for k in ("extent", "far", "mesh", "lod", "tile", "size", "bound"))]
    unreal.log("ocean methods=" + ", ".join(names[:50]))

if zone:
    loc = zone.get_actor_location()
    unreal.log(f"zone_loc=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})")
    for prop in (
        "zone_extent",
        "far_distance",
        "far_distance_mesh_extent",
        "tessellation_factor",
        "lod_scale",
        "overlap_priority",
        "owned_by_water_body",
        "render_in_main_pass",
        "half_precision_texture",
    ):
        try:
            unreal.log(f"zone.{prop}={zone.get_editor_property(prop)}")
        except Exception as e:
            unreal.log(f"zone.{prop}: {e}")

    mesh = None
    try:
        mesh = zone.get_editor_property("water_mesh")
    except Exception:
        pass
    unreal.log(f"water_mesh={mesh}")
    if mesh:
        for prop in (
            "extent_in_tiles",
            "tile_size",
            "tessellation_factor",
            "lod_scale",
            "force_collapse_density_level",
            "far_distance_mesh_extent",
            "far_distance",
        ):
            try:
                unreal.log(f"mesh.{prop}={mesh.get_editor_property(prop)}")
            except Exception as e:
                unreal.log(f"mesh.{prop}: {e}")
        names = [n for n in dir(mesh) if any(k in n.lower() for k in ("extent", "far", "tile", "tess", "lod", "size"))]
        unreal.log("mesh props=" + ", ".join(names[:60]))

    znames = [n for n in dir(zone) if any(k in n.lower() for k in ("extent", "far", "mesh", "tile", "tess", "lod"))]
    unreal.log("zone props=" + ", ".join(znames[:60]))

# landscape size for reference
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        origin, extent = a.get_actor_bounds(False)
        unreal.log(
            f"landscape center=({origin.x:.0f},{origin.y:.0f}) "
            f"extent_xy=({extent.x:.0f},{extent.y:.0f}) "
            f"bounds=[{origin.x-extent.x:.0f}..{origin.x+extent.x:.0f}] x "
            f"[{origin.y-extent.y:.0f}..{origin.y+extent.y:.0f}]"
        )

unreal.log("DIAG_DONE")
