"""
Restore LobbyLandscape shape after WaterBodyOcean sculpted it.
1) Disable sea affects_landscape
2) Remove WaterBrushManager
3) Recreate landscape from HM_Lobby_505_smoothed.r16 with prior transform
4) Reassign material + paint layers + Seabed seed
"""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
R16 = r"E:/Unreal/MarineDemolition/Saved/HM_Lobby_505_smoothed.r16"
MIC = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
SEA_LABEL = "LobbySea"
ZONE_LABEL = "LobbyWaterZone"

# Matching current LobbyLandscape transform (before water flattened heights)
SCALE = unreal.Vector(225.0, 225.0, 234.375)
# CreateLandscapeFromR16 spawns at Location+Offset where Offset= -252*Scale XY
LOCATION = unreal.Vector(0.0, 0.0, -268.8)


def find_by_label(label: str):
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if (a.get_actor_label() or "") == label:
            return a
    return None


def disable_water_sculpt(sea):
    if not sea:
        return
    comp = sea.get_component_by_class(unreal.WaterBodyOceanComponent)
    if not comp:
        return
    try:
        comp.set_editor_property("affects_landscape", False)
        unreal.log("affects_landscape=False")
    except Exception as e:
        unreal.log_warning(f"set affects_landscape: {e}")
    # Soften curve so accidental re-enable won't carve deep
    try:
        curve = comp.get_editor_property("curve_settings")
        curve.set_editor_property("channel_depth", 0.0)
        curve.set_editor_property("curve_ramp_width", 0.0)
        curve.set_editor_property("use_curve_channel", False)
        comp.set_editor_property("curve_settings", curve)
        unreal.log("curve_settings neutralized")
    except Exception as e:
        unreal.log(f"curve_settings: {e}")
    try:
        sea.post_edit_change()
    except Exception:
        pass


def destroy_brush_managers(actor_sub):
    for a in list(actor_sub.get_all_level_actors()):
        cls = a.get_class().get_name()
        lab = a.get_actor_label() or ""
        if cls == "WaterBrushManager" or "WaterBrushManager" in lab:
            actor_sub.destroy_actor(a)
            unreal.log(f"destroyed brush manager {lab}")


def recreate_landscape(actor_sub):
    # Destroy existing landscape only
    for a in list(actor_sub.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        if lab == "LobbyLandscape" or a.get_class().get_name() == "Landscape":
            actor_sub.destroy_actor(a)
            unreal.log(f"destroyed landscape {lab}")

    cls = unreal.load_class(None, "/Script/MarineDemolition.MD_LandscapeLibrary")
    cdo = unreal.get_default_object(cls)
    landscape = cdo.call_method(
        "CreateLandscapeFromR16",
        (
            R16,
            505,
            505,
            63,
            1,
            8,
            8,
            SCALE,
            LOCATION,
            "LobbyLandscape",
        ),
    )
    if not landscape:
        raise RuntimeError("CreateLandscapeFromR16 failed")
    try:
        landscape.set_folder_path("Terrain")
    except Exception:
        pass
    return landscape


def restore_paint(landscape):
    mic = unreal.EditorAssetLibrary.load_asset(MIC)
    if mic:
        landscape.set_editor_property("landscape_material", mic)
        try:
            landscape.post_edit_change()
        except Exception:
            pass

    method = unreal.LandscapeTargetLayerBlendMethod.FINAL_WEIGHT_BLENDING
    for name in ("Seabed", "Plains", "Mountain"):
        path = f"/Game/Developers/TaitoP/Landscape/Layers/LI_{name}"
        li = unreal.EditorAssetLibrary.load_asset(path)
        if li:
            try:
                li.set_editor_property("blend_method", method)
            except Exception:
                pass
            unreal.EditorAssetLibrary.save_asset(path)

    unreal.SystemLibrary.execute_console_command(None, "MD.RegisterLobbyPaintLayers")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    size = 505
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, size, size, unreal.TextureRenderTargetFormat.RTF_R8,
        unreal.LinearColor(0, 0, 0, 1), False,
    )
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(1, 0, 0, 1))
    ok_s = landscape.landscape_import_weightmap_from_render_target(rt, "Seabed")
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
    ok_p = landscape.landscape_import_weightmap_from_render_target(rt, "Plains")
    ok_m = landscape.landscape_import_weightmap_from_render_target(rt, "Mountain")
    unreal.log(f"seed Seabed={ok_s} Plains={ok_p} Mountain={ok_m}")


def run():
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sea = find_by_label(SEA_LABEL)
    zone = find_by_label(ZONE_LABEL)
    sea_loc = sea.get_actor_location() if sea else None
    zone_loc = zone.get_actor_location() if zone else None

    disable_water_sculpt(sea)
    destroy_brush_managers(actor_sub)

    landscape = recreate_landscape(actor_sub)
    loc = landscape.get_actor_location()
    scale = landscape.get_actor_scale3d()
    origin, extent = landscape.get_actor_bounds(False)
    unreal.log(
        f"restored ls loc=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f}) "
        f"scale=({scale.x:.3f},{scale.y:.3f},{scale.z:.3f}) "
        f"bounds_z=[{origin.z-extent.z:.1f}..{origin.z+extent.z:.1f}]"
    )

    restore_paint(landscape)

    # Re-disable in case WaterBrushManager respawned on landscape create
    sea = find_by_label(SEA_LABEL)
    disable_water_sculpt(sea)
    destroy_brush_managers(actor_sub)

    # Keep sea height
    if sea and sea_loc:
        sea.set_actor_location(sea_loc, False, True)
    if zone and zone_loc:
        zone.set_actor_location(zone_loc, False, True)

    # Final verify affects_landscape
    sea = find_by_label(SEA_LABEL)
    if sea:
        comp = sea.get_component_by_class(unreal.WaterBodyOceanComponent)
        unreal.log(f"final affects_landscape={comp.get_editor_property('affects_landscape')}")
        unreal.log(f"sea_z={sea.get_actor_location().z:.1f}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("LANDSCAPE_RESTORE_OK")


run()
