"""Dump SoStylized MPC + verify key sky assets."""
import unreal

MPC = "/Game/SoStylized/Environment/MPC_GlobalEnvironment"
mpc = unreal.EditorAssetLibrary.load_asset(MPC)
if not mpc:
    unreal.log_error("MPC_MISS")
else:
    for p in mpc.get_editor_property("scalar_parameters") or []:
        unreal.log(f"S {p.get_editor_property('parameter_name')}={p.get_editor_property('default_value')}")
    for p in mpc.get_editor_property("vector_parameters") or []:
        unreal.log(f"V {p.get_editor_property('parameter_name')}={p.get_editor_property('default_value')}")

for p in (
    "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome",
    "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
    "/Game/SoStylized/Environment/Sky/Meshes/SM_SunAndMoonPlane",
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
    "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_Sun_Classic",
    "/Game/SoStylized/Environment/Sky/BP_StylizedSky",
    "/Game/SoStylized/Environment/Sky/Textures/T_Skylight_HDR",
    "/Game/SoStylized/Environment/Sky/Curves/Curve_SunColor_Classic",
    "/Game/SoStylized/Environment/Sky/Curves/Curve_SunGlow_Classic",
    "/Game/SoStylized/Environment/Sky/Curves/Curve_Sky_Classic_Day",
):
    unreal.log(("OK" if unreal.EditorAssetLibrary.does_asset_exist(p) else "MISS") + " " + p)

unreal.log("DUMP_OK")
