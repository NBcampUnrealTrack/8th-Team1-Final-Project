"""Fix sky/cloud dome invisible from inside: two-sided parent mats + component culling."""
import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)

PARENT_MATS = (
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_Sky",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_Clouds",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedSky",
    "/Game/SoStylized/Environment/Sky/Materials/M_StylizedSky",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
    "/Game/SoStylized/Environment/Sky/Materials/M_StylizedClouds",
)

SKY_MIS = (
    "/Game/Developers/TaitoP/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
)

CLOUD_MIS = (
    "/Game/Developers/TaitoP/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
)


def set_material_two_sided(mat_path):
    if not unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        return False
    mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    try:
        mat.set_editor_property("two_sided", True)
        unreal.EditorAssetLibrary.save_asset(mat_path)
        unreal.log(f"two_sided=True {mat_path}")
        return True
    except Exception as e:
        unreal.log_warning(f"two_sided failed {mat_path}: {e}")
        return False


def set_mi_two_sided_override(mi_path):
    if not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return False
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    changed = False
    try:
        overrides = mi.get_editor_property("base_property_overrides")
        overrides.set_editor_property("override_two_sided", True)
        overrides.set_editor_property("two_sided", True)
        mi.set_editor_property("base_property_overrides", overrides)
        changed = True
    except Exception as e:
        unreal.log_warning(f"MI override_two_sided failed {mi_path}: {e}")
    if changed:
        unreal.EditorAssetLibrary.save_asset(mi_path)
        unreal.log(f"MI two-sided override {mi_path}")
    return changed


def configure_dome_component(c):
    for prop, val in (
        ("reverse_culling", True),
        ("bounds_scale", 1000.0),
        ("cast_shadow", False),
        ("visible", True),
        ("hidden_in_game", False),
    ):
        try:
            c.set_editor_property(prop, val)
        except Exception:
            pass
    try:
        c.set_force_disable_nanite(True)
    except Exception:
        try:
            c.set_editor_property("bDisallowNanite", True)
        except Exception:
            pass
    try:
        c.set_visibility(True, True)
        c.set_hidden_in_game(False, True)
    except Exception:
        pass


def fix_actor(actor):
    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        name = c.get_name()
        if "SkyDome" in name or "Cloud" in name:
            configure_dome_component(c)
            mesh = c.get_editor_property("static_mesh")
            mesh_path = mesh.get_path_name() if mesh else "None"
            rev = c.get_editor_property("reverse_culling")
            bounds = c.get_editor_property("bounds_scale")
            unreal.log(f"  dome {name} mesh={mesh_path} reverse_culling={rev} bounds_scale={bounds}")


def run():
    for path in PARENT_MATS:
        set_material_two_sided(path)
    for path in SKY_MIS + CLOUD_MIS:
        set_mi_two_sided_override(path)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        blob = f"{actor.get_actor_label()}|{actor.get_class().get_name()}"
        if "BP_Sky" not in blob and "MD_StylizedSky" not in blob:
            continue
        unreal.log(f"fix interior dome {actor.get_actor_label()}")
        fix_actor(actor)
        try:
            actor.call_method("RefreshSkyNow")
        except Exception:
            actor.rerun_construction_scripts()

    bp_path = "/Game/Developers/TaitoP/BP_Sky"
    if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        bp = unreal.EditorAssetLibrary.load_asset(bp_path)
        cdo = unreal.get_default_object(bp.generated_class())
        fix_actor(cdo)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log_warning(f"compile: {e}")
        unreal.EditorAssetLibrary.save_asset(bp_path)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("FIX_SKY_DOME_INTERIOR_OK — re-open BP_Sky preview or rerun RefreshSkyNow")


run()
