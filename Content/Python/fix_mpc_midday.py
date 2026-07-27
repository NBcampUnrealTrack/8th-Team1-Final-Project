"""Force SoStylized MPC_GlobalEnvironment to midday (fixes black sky when Current Time=0)."""
import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)

MPCS = [
    "/Game/Developers/TaitoP/SoStylized/Environment/MPC_GlobalEnvironment",
    "/Game/SoStylized/Environment/MPC_GlobalEnvironment",
]
MI = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic"


def push_mpc(mpc):
    scalars = list(mpc.get_editor_property("scalar_parameters"))
    new_scalars = []
    for s in scalars:
        name = str(s.get_editor_property("parameter_name"))
        if name == "Current Time":
            s.set_editor_property("default_value", 12.0)
        elif name == "Day Cycle Progress":
            s.set_editor_property("default_value", 0.5)
        elif name == "Orbit Orientation":
            s.set_editor_property("default_value", 12.0 * 15.0 - 90.0)
        elif name == "Orbit Inclination":
            s.set_editor_property("default_value", 28.0)
        new_scalars.append(s)
    mpc.set_editor_property("scalar_parameters", new_scalars)
    unreal.EditorAssetLibrary.save_asset(mpc.get_path_name().split(".")[0])

    md_sky_mpc.push_midday_scalars(mpc, 12.0)
    unreal.log(f"world MPC midday OK: {mpc.get_path_name()}")


def run():
    for path in MPCS:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            push_mpc(unreal.EditorAssetLibrary.load_asset(path))

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        if "BP_Sky" in actor.get_class().get_name() or actor.get_actor_label() == "BP_Sky":
            actor.set_editor_property("bFrozenTime", True)
            actor.set_editor_property("FrozenTimeOfDayHours", 12.0)
            try:
                actor.call_method("RefreshSkyNow")
            except Exception:
                try:
                    actor.rerun_construction_scripts()
                except Exception as e:
                    unreal.log_warning(f"refresh sky: {e}")

    mi = unreal.EditorAssetLibrary.load_asset(MI)
    if mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Sky Brightness", 1.5)
        unreal.EditorAssetLibrary.save_asset(MI)

    unreal.log("FIX_MPC_MIDDAY_OK")


run()
