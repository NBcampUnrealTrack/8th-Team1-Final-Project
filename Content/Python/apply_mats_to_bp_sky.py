"""Apply owned sky materials to BP_Sky (SkyController soft refs)."""
import unreal

import md_sky_component as sky_comp
import md_sky_paths as sky_paths

CANDIDATES = (
    sky_paths.BP_SKY,
    "/Game/Developers/TaitoP/Environment/Sky/BP_Sky",
)


def find_bp_sky():
    for p in CANDIDATES:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            return p
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in ar.get_all_assets() or []:
        if str(a.asset_name) == "BP_Sky":
            return str(a.package_name)
    return None


def apply(target):
    for prop, path in sky_paths.PATHS.items():
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset:
            sky_comp.set_sky_prop(target, prop, asset)
            unreal.log(f"set {prop} -> {path}")
        else:
            unreal.log_warning(f"MISSING {path}")


def run():
    bp_path = find_bp_sky()
    if not bp_path:
        raise RuntimeError("BP_Sky not found — run migrate_bp_sky_off_sostylized.py first")
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    apply(cdo)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(bp_path)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if "BP_Sky" in (a.get_actor_label() or ""):
            apply(a)
            a.call_method("RefreshSkyNow")
    unreal.log("APPLY_OWNED_SKY_TO_BP_SKY_OK")


run()
