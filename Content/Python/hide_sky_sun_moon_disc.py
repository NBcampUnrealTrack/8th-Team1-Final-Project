"""
Hide sun/moon discs on BP_Sky — mesh components AND Classic sky shader billboard.

Classic MI draws sun/moon inside the dome via MPC SunStrength + T_Sun01_BC.
Deleting SM_Sun/SM_Moon components does not remove that shader disc.

Run (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/hide_sky_sun_moon_disc.py"
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_paths", "md_sky_component", "md_sky_import"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp
import md_sky_import as md_sky_mpc

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
md_sky_mpc = importlib.reload(md_sky_mpc)

SKY_MIS = (
    paths.PATHS["SkyMaterial"],
    f"{paths.SKY_ROOT}/Materials/Presets/MI_StylizedSky_Classic",
)
CELESTIAL_NAME_BITS = ("SM_Sun", "SM_Moon", "SunDisc", "Moon", "Ring", "PlanetRing")


def set_prop(obj, name, value) -> bool:
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception:
        return False


def zero_mi_celestial(mi_path: str) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    for name, val in (
        ("Nebula Multiply", 0.0),
        ("Sun Brightness", 0.0),
        ("Sun Size", 0.0),
        ("Moon Brightness", 0.0),
        ("Moon Size", 0.0),
        ("Stars Brightness", 0.0),
        ("Star Brightness", 0.0),
    ):
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mi, name, val
            )
        except Exception:
            pass
    unreal.EditorAssetLibrary.save_asset(mi_path)
    unreal.log(f"MI celestial off {mi_path}")


def push_sun_strength_zero() -> None:
    mpc_path = paths.PATHS["EnvironmentMPC"]
    if not unreal.EditorAssetLibrary.does_asset_exist(mpc_path):
        return
    mpc = unreal.EditorAssetLibrary.load_asset(mpc_path)
    md_sky_mpc.push_scalars(mpc, {"SunStrength": 0.0})
    for world in md_sky_mpc.all_world_contexts():
        md_sky_mpc.set_mpc_scalar(world, mpc, "SunStrength", 0.0)


def hide_celestial_components(actor) -> int:
    n = 0
    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        name = c.get_name()
        if not any(bit in name for bit in CELESTIAL_NAME_BITS):
            continue
        if name in ("SkyDome", "SkyDomeClouds"):
            continue
        try:
            c.set_static_mesh(None)
        except Exception:
            pass
        c.set_visibility(False, True)
        c.set_hidden_in_game(True, True)
        set_prop(c, "visible", False)
        set_prop(c, "hidden_in_game", True)
        set_prop(c, "bounds_scale", 0.01)
        n += 1
        unreal.log(f"hide celestial {name}")
    return n


def apply_controller(actor) -> None:
    sky_comp.set_sky_prop(actor, "MpcShaderSunStrengthScale", 0.0)
    sky_comp.set_sky_prop(actor, "SunVisibilityGlowThreshold", 999.0)


def find_sky_actors():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    out = []
    for a in eas.get_all_level_actors():
        blob = f"{a.get_actor_label()}|{a.get_class().get_name()}"
        if any(k in blob for k in ("BP_Sky", "MD_StylizedSky", "BP_StylizedSky")):
            out.append(a)
    return out


def main() -> None:
    for mi in SKY_MIS:
        zero_mi_celestial(mi)

    push_sun_strength_zero()

    bp = sky_comp.load_bp_sky_blueprint()
    if bp:
        cdo = unreal.get_default_object(bp.generated_class())
        apply_controller(cdo)
        hide_celestial_components(cdo)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log_warning(f"compile: {e}")
        unreal.EditorAssetLibrary.save_asset(paths.BP_SKY)

    hidden = 0
    for actor in find_sky_actors():
        apply_controller(actor)
        hidden += hide_celestial_components(actor)
        try:
            actor.call_method("RefreshSkyNow")
        except Exception:
            pass
        try:
            actor.rerun_construction_scripts()
        except Exception:
            pass

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"HIDE_SKY_SUN_MOON_DISC_OK actors={len(find_sky_actors())} hidden_components={hidden}")


if __name__ == "__main__":
    main()
