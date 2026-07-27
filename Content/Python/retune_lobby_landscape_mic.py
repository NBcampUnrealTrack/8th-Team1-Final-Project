"""Retune MI_LobbyLandscape HeightMin/Max from actual landscape height samples."""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MIC_PATH = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
MAT_PATH = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape"


def find_landscape():
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            return a
    return None


def sample_z_via_trace(landscape):
    origin, extent = landscape.get_actor_bounds(True)
    xs = [origin.x + t * extent.x for t in (-0.8, -0.4, 0.0, 0.4, 0.8)]
    ys = [origin.y + t * extent.y for t in (-0.8, -0.4, 0.0, 0.4, 0.8)]
    world = unreal.EditorLevelLibrary.get_editor_world()
    zs = []
    for x in xs:
        for y in ys:
            start = unreal.Vector(x, y, origin.z + extent.z + 50000.0)
            end = unreal.Vector(x, y, origin.z - extent.z - 50000.0)
            hit = unreal.SystemLibrary.line_trace_single(
                world,
                start,
                end,
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                True,
                [],
                unreal.DrawDebugTrace.NONE,
                True,
            )
            # older API returns hit result differently — try both
            try:
                if hit and hit.to_tuple()[0]:
                    zs.append(float(hit.to_tuple()[1].location.z))
                    continue
            except Exception:
                pass
            try:
                # SystemLibrary.line_trace_single_by_profile / Editor filter
                pass
            except Exception:
                pass
    return zs


def sample_z_via_height_rt(landscape):
    try:
        fmt = unreal.TextureRenderTargetFormat.RTF_RGBA16F
    except Exception:
        fmt = unreal.TextureRenderTargetFormat.RTF_RGBA8
    rt = unreal.RenderingLibrary.create_render_target2d(None, 64, 64, fmt, unreal.LinearColor(0, 0, 0, 1), False)
    ok = landscape.landscape_export_heightmap_to_render_target(rt, False, True)
    if not ok:
        return None
    # Read pixels if available
    try:
        pixels = unreal.RenderingLibrary.read_render_target(None, rt, True)
        vals = []
        for p in pixels:
            # packed height often in R or as encoded
            vals.append(float(p.r))
        if not vals:
            return None
        return min(vals), max(vals), sum(vals) / len(vals)
    except Exception as exc:
        unreal.log_warning(f"RT read failed: {exc}")
        return None


def run():
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    landscape = find_landscape()
    if not landscape:
        raise RuntimeError("no landscape")

    # Prefer world-position style bounds from transform + known scale
    loc = landscape.get_actor_location()
    scale = landscape.get_actor_scale3d()
    unreal.log(f"Landscape loc={loc} scale={scale}")

    # Default sane lobby range (previous material defaults), with slight pad
    zmin, zmax = -400.0, 6500.0

    # Try traces with Editor subsystem
    try:
        zs = []
        origin, extent = landscape.get_actor_bounds(True)
        world = unreal.EditorLevelLibrary.get_editor_world()
        xs = [origin.x + t * extent.x for t in (-0.7, -0.35, 0.0, 0.35, 0.7)]
        ys = [origin.y + t * extent.y for t in (-0.7, -0.35, 0.0, 0.35, 0.7)]
        for x in xs:
            for y in ys:
                hit = unreal.SystemLibrary.line_trace_single_for_objects(
                    world,
                    unreal.Vector(x, y, 100000.0),
                    unreal.Vector(x, y, -100000.0),
                    [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1],
                    True,
                    [],
                    unreal.DrawDebugTrace.NONE,
                    True,
                    unreal.LinearColor.RED,
                    unreal.LinearColor.GREEN,
                    0.0,
                )
                # hit may be HitResult
                try:
                    if bool(hit.blocking_hit):
                        zs.append(float(hit.location.z))
                except Exception:
                    try:
                        # some builds return (bool, hit)
                        if hit and hit[0]:
                            zs.append(float(hit[1].location.z))
                    except Exception:
                        pass
        if zs:
            zmin = min(zs)
            zmax = max(zs)
            unreal.log(f"Trace Z samples n={len(zs)} min={zmin:.1f} max={zmax:.1f}")
        else:
            unreal.log_warning("No trace hits; keeping default Z range")
    except Exception as exc:
        unreal.log_warning(f"trace failed: {exc}")

    # If range still absurd (>20k span), clamp to defaults
    if (zmax - zmin) > 20000.0 or zmax > 30000.0:
        unreal.log_warning(f"Z range suspicious ({zmin},{zmax}); using lobby defaults")
        zmin, zmax = -400.0, 6500.0

    pad = max(80.0, (zmax - zmin) * 0.08)
    hmin = zmin - pad
    hmax = zmax + pad

    mic = unreal.EditorAssetLibrary.load_asset(MIC_PATH)
    if not mic:
        mat = unreal.EditorAssetLibrary.load_asset(MAT_PATH)
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LobbyLandscape",
            "/Game/Developers/TaitoP/Landscape/Materials",
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
        unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)

    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "HeightMin", hmin)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "HeightMax", hmax)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "SeabedEnd", 0.26)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "MountainStart", 0.52)
    unreal.EditorAssetLibrary.save_asset(MIC_PATH)

    landscape.set_editor_property("landscape_material", mic)
    try:
        landscape.post_edit_change()
    except Exception:
        pass

    unreal.EditorLevelLibrary.save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"RETUNE OK HeightMin={hmin:.0f} HeightMax={hmax:.0f} SeabedEnd=0.26 MountainStart=0.52")


run()
