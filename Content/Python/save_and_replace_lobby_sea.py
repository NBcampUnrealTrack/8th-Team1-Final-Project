"""Save L_Lobby, then re-place sea at 2/3 pier height without sculpting landscape."""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
WATER_MAT = "/Water/Materials/WaterSurface/Water_Material"
SEA_LABEL = "LobbySea"
ZONE_LABEL = "LobbyWaterZone"


def pier_envelope():
    zs = []
    xs = []
    ys = []
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
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


def destroy_labels(labels):
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(sub.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab in labels or cls == "WaterBrushManager" or "WaterBrushManager" in lab:
            sub.destroy_actor(a)
            unreal.log(f"destroyed {lab or cls}")


def spawn_actor(cls, loc, label):
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = sub.spawn_actor_from_class(cls, loc, unreal.Rotator(0, 0, 0))
    if not actor:
        raise RuntimeError(f"failed to spawn {cls}")
    actor.set_actor_label(label)
    return actor


def disable_sculpt(ocean):
    ocomp = ocean.get_component_by_class(unreal.WaterBodyOceanComponent)
    if not ocomp:
        return
    ocomp.set_editor_property("affects_landscape", False)
    try:
        curve = ocomp.get_editor_property("curve_settings")
        curve.set_editor_property("use_curve_channel", False)
        curve.set_editor_property("channel_depth", 0.0)
        curve.set_editor_property("curve_ramp_width", 0.0)
        ocomp.set_editor_property("curve_settings", curve)
    except Exception as e:
        unreal.log(f"curve: {e}")
    try:
        ocomp.set_editor_property("collision_extents", unreal.Vector(100000.0, 100000.0, 10000.0))
        ocomp.set_editor_property("ocean_extents", unreal.Vector2D(100000.0, 100000.0))
    except Exception as e:
        unreal.log(f"extents: {e}")
    unreal.log(f"affects_landscape={ocomp.get_editor_property('affects_landscape')}")


def log_landscape():
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            origin, extent = a.get_actor_bounds(False)
            unreal.log(
                f"landscape bounds_z=[{origin.z-extent.z:.1f}..{origin.z+extent.z:.1f}]"
            )
            return


def run():
    # 1) Save current map first
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("MAP_SAVED")
    log_landscape()

    # 2) Re-place sea
    zmin, zmax, cx, cy = pier_envelope()
    height = zmax - zmin
    water_z = zmin + height * (2.0 / 3.0)
    unreal.log(f"pier z=[{zmin:.1f}..{zmax:.1f}] water_z={water_z:.1f}")

    destroy_labels([SEA_LABEL, ZONE_LABEL, "LobbySeaPlane"])

    loc = unreal.Vector(cx, cy, water_z)
    zone = spawn_actor(unreal.WaterZone, loc, ZONE_LABEL)
    try:
        zone.set_editor_property("zone_extent", unreal.Vector2D(150000.0, 150000.0))
    except Exception:
        pass

    ocean = spawn_actor(unreal.WaterBodyOcean, loc, SEA_LABEL)
    disable_sculpt(ocean)

    mat = unreal.EditorAssetLibrary.load_asset(WATER_MAT) if unreal.EditorAssetLibrary.does_asset_exist(WATER_MAT) else None
    if mat and hasattr(ocean, "set_water_material"):
        try:
            ocean.set_water_material(mat)
        except Exception:
            pass

    try:
        ocean.post_edit_change()
        zone.post_edit_change()
    except Exception:
        pass

    # Destroy any brush manager that spawned
    destroy_labels([])  # only managers via class check inside... fix:
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(sub.get_all_level_actors()):
        if a.get_class().get_name() == "WaterBrushManager":
            sub.destroy_actor(a)
            unreal.log("destroyed WaterBrushManager")

    # Re-assert no sculpt
    ocean = None
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if (a.get_actor_label() or "") == SEA_LABEL:
            ocean = a
            break
    if ocean:
        disable_sculpt(ocean)

    log_landscape()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"SEA_REPLACED_OK water_z={water_z:.1f}")


run()
