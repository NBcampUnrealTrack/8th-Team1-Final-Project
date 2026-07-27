"""Place Lobby sea at 2/3 of pier post height using WaterBodyOcean + WaterZone."""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
WATER_MAT = "/Water/Materials/WaterSurface/Water_Material"
SEA_LABEL = "LobbySea"
ZONE_LABEL = "LobbyWaterZone"


def pier_envelope():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    zs = []
    xs = []
    ys = []
    for a in actors:
        label = a.get_actor_label() or ""
        if not label.lower().startswith("sm_pier"):
            continue
        origin, extent = a.get_actor_bounds(False)
        zs.append((origin.z - extent.z, origin.z + extent.z))
        xs.append(origin.x)
        ys.append(origin.y)
    if not zs:
        raise RuntimeError("no SM_pier actors found")
    zmin = min(z[0] for z in zs)
    zmax = max(z[1] for z in zs)
    cx = sum(xs) / len(xs)
    cy = sum(ys) / len(ys)
    return zmin, zmax, cx, cy


def destroy_existing(labels):
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(sub.get_all_level_actors()):
        if (a.get_actor_label() or "") in labels:
            sub.destroy_actor(a)
            unreal.log(f"destroyed {a}")


def spawn_actor(cls, loc, label):
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = sub.spawn_actor_from_class(cls, loc, unreal.Rotator(0, 0, 0))
    if not actor:
        raise RuntimeError(f"failed to spawn {cls}")
    actor.set_actor_label(label)
    return actor


def run():
    zmin, zmax, cx, cy = pier_envelope()
    height = zmax - zmin
    water_z = zmin + height * (2.0 / 3.0)
    unreal.log(f"pier z=[{zmin:.1f}..{zmax:.1f}] h={height:.1f} water_z={water_z:.1f} center=({cx:.0f},{cy:.0f})")

    destroy_existing([SEA_LABEL, ZONE_LABEL, "LobbySeaPlane"])

    loc = unreal.Vector(cx, cy, water_z)

    # WaterZone required for Water bodies to render
    zone = spawn_actor(unreal.WaterZone, loc, ZONE_LABEL)
    try:
        # Large enough to cover lobby landscape / dock area
        zone.set_editor_property("zone_extent", unreal.Vector2D(120000.0, 120000.0))
    except Exception as e:
        unreal.log(f"zone_extent: {e}")
    try:
        zone.set_editor_property("far_distance_mesh_extent", 100000.0)
    except Exception:
        pass

    # Ocean body
    ocean = spawn_actor(unreal.WaterBodyOcean, loc, SEA_LABEL)

    # CRITICAL: do not let Water sculpt/flatten the Lobby landscape
    try:
        ocomp = ocean.get_component_by_class(unreal.WaterBodyOceanComponent)
        if ocomp:
            ocomp.set_editor_property("affects_landscape", False)
            try:
                curve = ocomp.get_editor_property("curve_settings")
                curve.set_editor_property("use_curve_channel", False)
                curve.set_editor_property("channel_depth", 0.0)
                curve.set_editor_property("curve_ramp_width", 0.0)
                ocomp.set_editor_property("curve_settings", curve)
            except Exception:
                pass
            unreal.log("ocean affects_landscape=False")
    except Exception as e:
        unreal.log_warning(f"disable sculpt: {e}")

    # Collision / extent
    for prop, val in (
        ("collision_extents", unreal.Vector(80000.0, 80000.0, 5000.0)),
        ("ocean_extent", unreal.Vector2D(80000.0, 80000.0)),
    ):
        try:
            ocean.set_editor_property(prop, val)
            unreal.log(f"set ocean.{prop}")
        except Exception as e:
            unreal.log(f"ocean.{prop}: {e}")

    try:
        ocomp = ocean.get_component_by_class(unreal.WaterBodyOceanComponent)
        if ocomp:
            ocomp.set_editor_property("collision_extents", unreal.Vector(100000.0, 100000.0, 10000.0))
            ocomp.set_editor_property("ocean_extents", unreal.Vector2D(100000.0, 100000.0))
    except Exception as e:
        unreal.log(f"ocean comp extent: {e}")

    # Try set water material if component exposes it
    try:
        comp = ocean.get_component_by_class(unreal.WaterBodyOceanComponent)
        if comp:
            unreal.log(f"ocean_comp={comp}")
            for prop in ("water_material", "water_info_material", "underwater_post_process_material"):
                try:
                    unreal.log(f"  has {prop}")
                except Exception:
                    pass
    except Exception as e:
        unreal.log(f"comp: {e}")

    # Also ensure Water_Material exists / assign via WaterBody
    mat = unreal.EditorAssetLibrary.load_asset(WATER_MAT) if unreal.EditorAssetLibrary.does_asset_exist(WATER_MAT) else None
    if mat:
        for prop in ("water_material",):
            try:
                ocean.set_editor_property(prop, mat)
                unreal.log(f"assigned {prop}")
            except Exception as e:
                unreal.log(f"set {prop}: {e}")
        try:
            # WaterBody base often uses SetWaterMaterial
            if hasattr(ocean, "set_water_material"):
                ocean.set_water_material(mat)
                unreal.log("set_water_material ok")
        except Exception as e:
            unreal.log(f"set_water_material: {e}")

    try:
        ocean.post_edit_change()
    except Exception:
        pass
    try:
        zone.post_edit_change()
    except Exception:
        pass

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"SEA_OK water_z={water_z:.1f} pier_top={zmax:.1f} pier_bottom={zmin:.1f}")


run()
