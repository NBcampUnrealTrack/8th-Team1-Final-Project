"""Finish: parent MI to master, assign to landscape, save. No heavy paint."""
import unreal
import traceback

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MASTER = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
MIC = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
LAYER_DST = "/Game/Developers/TaitoP/Landscape/Layers"

TINTS = {
    "Seabed": (0.55, 0.72, 0.82),
    "Plains": (0.62, 0.78, 0.58),
    "Mountain": (0.72, 0.66, 0.78),
}

SCALARS = {
    "Seabed_Scale": 0.55,
    "Seabed_Saturation": -0.28,
    "Seabed_HUE": 0.22,
    "Seabed_Luminosity": 0.14,
    "Seabed_Roughness": 0.93,
    "Seabed_Normal_Intensity": 0.55,
    "Plains_Scale": 0.42,
    "Plains_Saturation": -0.18,
    "Plains_HUE": 0.16,
    "Plains_Luminosity": 0.10,
    "Plains_Roughness": 0.96,
    "Plains_Normal_Intensity": 0.50,
    "Mountain_Scale": 0.18,
    "Mountain_Saturation": -0.22,
    "Mountain_HUE": 0.20,
    "Mountain_Luminosity": 0.06,
    "Mountain_Roughness": 0.99,
    "Mountain_Normal_Intensity": 1.45,
    "Micro_Variation": 0.035,
}


def run():
    try:
        master = unreal.EditorAssetLibrary.load_asset(MASTER)
        if not master:
            raise RuntimeError("master missing")

        if unreal.EditorAssetLibrary.does_asset_exist(MIC):
            mic = unreal.EditorAssetLibrary.load_asset(MIC)
        else:
            mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "MI_LobbyLandscape",
                "/Game/Developers/TaitoP/Landscape/Materials",
                unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew(),
            )
        unreal.log("Setting MIC parent...")
        unreal.MaterialEditingLibrary.set_material_instance_parent(mic, master)

        for k, v in SCALARS.items():
            try:
                unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, k, float(v))
            except Exception as e:
                unreal.log_warning(f"scalar {k}: {e}")

        for name, rgb in TINTS.items():
            try:
                unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                    mic, f"{name}_Tint", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)
                )
            except Exception as e:
                unreal.log_warning(f"tint {name}: {e}")

        unreal.EditorAssetLibrary.save_asset(MIC)
        unreal.log(f"Saved MIC {MIC}")

        # Layer infos best-effort
        for name in ("Seabed", "Plains", "Mountain"):
            path = f"{LAYER_DST}/LI_{name}"
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                unreal.log(f"LI exists {path}")
                continue
            try:
                factory = None
                if hasattr(unreal, "LandscapeLayerInfoObjectFactory"):
                    factory = unreal.LandscapeLayerInfoObjectFactory()
                li = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                    f"LI_{name}", LAYER_DST, unreal.LandscapeLayerInfoObject, factory
                )
                if li:
                    try:
                        li.set_editor_property("layer_name", name)
                    except Exception:
                        pass
                    unreal.EditorAssetLibrary.save_asset(path)
                    unreal.log(f"Created {path}")
            except Exception as e:
                unreal.log_warning(f"LI {name}: {e}")

        landscape = None
        for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
            if a.get_class().get_name() == "Landscape":
                landscape = a
                break
        if not landscape:
            raise RuntimeError("no landscape")

        landscape.set_editor_property("landscape_material", mic)
        try:
            landscape.post_edit_change()
        except Exception:
            pass
        unreal.log(f"Assigned {mic.get_name()} to {landscape.get_actor_label()}")

        # Fill default layer so something visible without paint: paint Plains as base via RT avg
        try:
            rt = unreal.RenderingLibrary.create_render_target2d(
                None, 64, 64, unreal.TextureRenderTargetFormat.RTF_R8,
                unreal.LinearColor(1, 0, 0, 1), False
            )
            unreal.RenderingLibrary.clear_render_target2d(None, rt, unreal.LinearColor(1, 0, 0, 1))
            ok = landscape.landscape_import_weightmap_from_render_target(rt, "Plains")
            unreal.log(f"Seed Plains weight: {ok}")
            unreal.RenderingLibrary.clear_render_target2d(None, rt, unreal.LinearColor(0.01, 0, 0, 1))
            for n in ("Seabed", "Mountain"):
                ok2 = landscape.landscape_import_weightmap_from_render_target(rt, n)
                unreal.log(f"Seed {n} weight: {ok2}")
        except Exception as e:
            unreal.log_warning(f"weight seed: {e}")

        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        unreal.EditorAssetLibrary.save_asset(LEVEL)
        unreal.log("SUCCESS_FINISH: MI_LobbyLandscape on LobbyLandscape")
    except Exception:
        unreal.log_error(traceback.format_exc())


run()
