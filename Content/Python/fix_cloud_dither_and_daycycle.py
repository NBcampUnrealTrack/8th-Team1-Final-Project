"""
1) Bypass DitherTemporalAA in M_StylizedClouds (soft Opacity).
2) Enable local day cycle on BP_Sky / level instance.
Run after Live Coding (Ctrl+Alt+F11) so bLocalDayCycle exists.
"""
import unreal

CLOUD_MAT = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/M_StylizedClouds"
BP_SKY = "/Game/Developers/TaitoP/BP_Sky"
# Also patch restored original-path copy if present
CLOUD_MAT_ALT = "/Game/SoStylized/Environment/Sky/Materials/M_StylizedClouds"


def bypass_dither(mat_path: str) -> bool:
    if not unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        unreal.log_warning(f"missing {mat_path}")
        return False
    mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    sat = None
    dither = None
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        name = e.get_name()
        if "Saturate_0" in name or name.endswith("Saturate_0"):
            sat = e
        if e.get_class().get_name() == "MaterialExpressionMaterialFunctionCall":
            try:
                mf = e.get_editor_property("material_function")
            except Exception:
                mf = None
            if mf and "DitherTemporalAA" in str(mf.get_path_name()):
                dither = e
    if not sat:
        # fallback: first Saturate
        for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
            if "Saturate" in e.get_name():
                sat = e
                break
    if not sat:
        unreal.log_error(f"no Saturate on {mat_path}")
        return False

    unreal.MaterialEditingLibrary.connect_material_property(
        sat, "", unreal.MaterialProperty.MP_OPACITY
    )
    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass
    try:
        mat.set_editor_property("b_enable_responsive_aa", True)
    except Exception:
        pass
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(mat_path)
    unreal.log(f"bypassed dither on {mat_path} (dither_node={bool(dither)})")
    return True


def set_prop(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        unreal.log_warning(f"skip {name}: {e}")
        return False


def enable_day_cycle():
    bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
    cdo = unreal.get_default_object(bp.generated_class())
    for prop, val in (
        ("b_frozen_time", False),
        ("b_drive_from_game_state", False),
        ("b_local_day_cycle", True),
        ("day_length_seconds", 600.0),
        ("local_time_of_day_hours", 12.0),
        ("frozen_time_of_day_hours", 12.0),
    ):
        set_prop(cdo, prop, val)
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_asset(BP_SKY)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        if "BP_Sky" in cls or "MD_StylizedSky" in cls:
            for prop, val in (
                ("b_frozen_time", False),
                ("b_drive_from_game_state", False),
                ("b_local_day_cycle", True),
                ("day_length_seconds", 600.0),
                ("local_time_of_day_hours", 12.0),
            ):
                set_prop(a, prop, val)
            unreal.log(f"day cycle enabled on {a.get_actor_label()}")


def run():
    bypass_dither(CLOUD_MAT)
    bypass_dither(CLOUD_MAT_ALT)
    enable_day_cycle()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("FIX_CLOUD_DITHER_AND_DAYCYCLE_OK")


run()
