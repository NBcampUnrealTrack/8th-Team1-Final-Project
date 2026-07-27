"""Assign M_LobbyLandscape to LobbyLandscape and save L_Lobby."""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MAT_PATH = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape"


def find_landscape():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    for a in actors:
        if a.get_class().get_name() != "Landscape":
            continue
        label = a.get_actor_label() or ""
        if label == "LobbyLandscape" or label.startswith("Landscape"):
            return a
    for a in actors:
        if a.get_class().get_name() == "Landscape":
            return a
    return None


def run():
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    mat = unreal.EditorAssetLibrary.load_asset(MAT_PATH)
    if not mat:
        raise RuntimeError(f"Missing material: {MAT_PATH}")

    landscape = find_landscape()
    if not landscape:
        raise RuntimeError("Landscape not found in current level")

    # Sample height range so HeightMin/Max match sculpt
    zmin = zmax = None
    try:
        origin, extent = landscape.get_actor_bounds(False)
        zmin = float(origin.z - extent.z)
        zmax = float(origin.z + extent.z)
        unreal.log(f"Landscape Z bounds: {zmin:.1f} .. {zmax:.1f}")
    except Exception as exc:
        unreal.log_warning(f"bounds failed: {exc}")

    landscape.set_editor_property("landscape_material", mat)
    try:
        landscape.post_edit_change()
    except Exception:
        pass

    try:
        landscape.set_actor_label("LobbyLandscape")
        landscape.set_folder_path("Terrain")
    except Exception:
        pass

    # Soft-tune scalar defaults on material if bounds look sane
    if zmin is not None and zmax is not None and (zmax - zmin) > 50.0:
        try:
            # Material params are baked defaults; create MIC override for tunability
            mic_path = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
            if unreal.EditorAssetLibrary.does_asset_exist(mic_path):
                mic = unreal.EditorAssetLibrary.load_asset(mic_path)
            else:
                mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                    "MI_LobbyLandscape",
                    "/Game/Developers/TaitoP/Landscape/Materials",
                    unreal.MaterialInstanceConstant,
                    unreal.MaterialInstanceConstantFactoryNew(),
                )
            unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)
            pad = max(50.0, (zmax - zmin) * 0.05)
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mic, "HeightMin", zmin - pad
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mic, "HeightMax", zmax + pad
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mic, "SeabedEnd", 0.28
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mic, "MountainStart", 0.55
            )
            unreal.EditorAssetLibrary.save_asset(mic_path)
            landscape.set_editor_property("landscape_material", mic)
            try:
                landscape.post_edit_change()
            except Exception:
                pass
            unreal.log(f"Assigned MIC {mic_path} HeightMin={zmin-pad:.0f} HeightMax={zmax+pad:.0f}")
        except Exception as exc:
            unreal.log_warning(f"MIC tune skipped, using parent mat: {exc}")

    unreal.EditorLevelLibrary.save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    mat_now = landscape.get_editor_property("landscape_material")
    unreal.log(
        f"SUCCESS: assigned {mat_now.get_name() if mat_now else None} "
        f"to {landscape.get_actor_label()}"
    )


run()
