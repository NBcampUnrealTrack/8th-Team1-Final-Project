"""
Lobby sky:
1) Sky dome -> Engine Sphere (near-full sphere, not bowl/hemisphere) + unlit gradient mat
2) Clouds -> soft translucent mat WITHOUT DitherTemporalAA (fixes screen-door dither)
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_mpc", "md_sky_import"):
    sys.modules.pop(_name, None)
import md_sky_import as md_sky_mpc

md_sky_mpc = importlib.reload(md_sky_mpc)

BP_SKY = "/Game/Developers/TaitoP/BP_Sky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MAT_DIR = "/Game/Developers/TaitoP/Environment/Sky/Materials"
SKY_MAT = f"{MAT_DIR}/M_LobbySkyDome"
CLOUD_MAT = f"{MAT_DIR}/M_LobbyCloudsSoft"

ENGINE_SPHERE = "/Engine/BasicShapes/Sphere"
CLOUD_MESH = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds"
SUN_MESH = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_SunAndMoonPlane"
SUN_MAT = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_Sun_Classic"
MPC = "/Game/Developers/TaitoP/SoStylized/Environment/MPC_GlobalEnvironment"
HDR = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/T_Skylight_HDR"

# SoStylized SM_StylizedSkyDome radius ~9.87e7; Engine Sphere radius 50 → scale ~
SKY_SCALE = 1974461.0
CLOUD_SCALE = 40.0  # SoStylized cloud dome authored size

CLOUD_TEXES = [
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers/T_CloudLayer_Overhead04",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers/T_CloudLayer01",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/CloudLayers/T_CloudLayer13",
]


def ensure_folder(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def load(path: str):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    return None


def clear_mat(mat) -> None:
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def build_sky_dome_mat():
    ensure_folder(MAT_DIR)
    mat = load(SKY_MAT)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbySkyDome",
            MAT_DIR,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    clear_mat(mat)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    # CameraVectorWS.Z -> 0..1 height factor (works on any mesh/UV)
    cam = add(mat, unreal.MaterialExpressionCameraVectorWS, -900, 0)
    mask = add(mat, unreal.MaterialExpressionComponentMask, -700, 0)
    mask.set_editor_property("r", False)
    mask.set_editor_property("g", False)
    mask.set_editor_property("b", True)
    mask.set_editor_property("a", False)
    unreal.MaterialEditingLibrary.connect_material_expressions(cam, "", mask, "")

    one = add(mat, unreal.MaterialExpressionConstant, -700, 120)
    one.set_editor_property("r", 1.0)
    addn = add(mat, unreal.MaterialExpressionAdd, -500, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(mask, "", addn, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", addn, "B")
    half = add(mat, unreal.MaterialExpressionConstant, -500, 140)
    half.set_editor_property("r", 0.5)
    mul = add(mat, unreal.MaterialExpressionMultiply, -300, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(addn, "", mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(half, "", mul, "B")
    sat = add(mat, unreal.MaterialExpressionSaturate, -120, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", sat, "")

    powc = add(mat, unreal.MaterialExpressionConstant, -120, 140)
    powc.set_editor_property("r", 1.35)
    pown = add(mat, unreal.MaterialExpressionPower, 40, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(sat, "", pown, "Base")
    unreal.MaterialEditingLibrary.connect_material_expressions(powc, "", pown, "Exp")

    zenith = add(mat, unreal.MaterialExpressionConstant3Vector, 40, -140)
    zenith.set_editor_property("constant", unreal.LinearColor(0.20, 0.50, 0.95, 1.0))
    horiz = add(mat, unreal.MaterialExpressionConstant3Vector, 40, 160)
    horiz.set_editor_property("constant", unreal.LinearColor(0.70, 0.85, 0.98, 1.0))
    lerp = add(mat, unreal.MaterialExpressionLinearInterpolate, 280, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(horiz, "", lerp, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(zenith, "", lerp, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(pown, "", lerp, "Alpha")

    boost = add(mat, unreal.MaterialExpressionConstant, 280, 180)
    boost.set_editor_property("r", 1.2)
    out = add(mat, unreal.MaterialExpressionMultiply, 480, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(lerp, "", out, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(boost, "", out, "B")

    # Soft-cut bottom so the sphere is "slightly incomplete" (open nadir), not a sealed ball.
    # Clip when looking steeply downward (CameraVector.Z very negative).
    clip_thr = add(mat, unreal.MaterialExpressionConstant, -300, 280)
    clip_thr.set_editor_property("r", -0.92)  # keep ~most of sphere; open a small bottom hole
    clip_sub = add(mat, unreal.MaterialExpressionSubtract, -80, 260)
    unreal.MaterialEditingLibrary.connect_material_expressions(mask, "", clip_sub, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(clip_thr, "", clip_sub, "B")
    clip_sat = add(mat, unreal.MaterialExpressionSaturate, 80, 260)
    unreal.MaterialEditingLibrary.connect_material_expressions(clip_sub, "", clip_sat, "")
    # Use as opacity mask with opaque blend via clip: PixelDepthOffset unused; use opacity mask domain
    # Keep opaque: multiply emissive by step so bottom goes black->we prefer clip via opacity mask + masked blend
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.01)
    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(clip_sat, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(SKY_MAT)
    unreal.log(f"built {SKY_MAT}")
    return mat


def build_soft_cloud_mat():
    ensure_folder(MAT_DIR)
    mat = load(CLOUD_MAT)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyCloudsSoft",
            MAT_DIR,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    clear_mat(mat)

    # Soft alpha — NO DitherTemporalAA (that was the screen-door dither)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    try:
        mat.set_editor_property("b_enable_responsive_aa", True)
    except Exception:
        pass
    try:
        mat.set_editor_property("b_output_translucent_velocity", True)
    except Exception:
        pass

    texes = [load(p) for p in CLOUD_TEXES]
    texes = [t for t in texes if t]
    if not texes:
        raise RuntimeError("no cloud textures")

    # Layered soft clouds from texture alpha/luma
    uv = add(mat, unreal.MaterialExpressionTextureCoordinate, -800, 0)
    accum = None
    y = 0
    for i, tex in enumerate(texes[:3]):
        scale = add(mat, unreal.MaterialExpressionConstant, -600, y)
        scale.set_editor_property("r", 1.0 + i * 0.35)
        mul_uv = add(mat, unreal.MaterialExpressionMultiply, -420, y)
        unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", mul_uv, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(scale, "", mul_uv, "B")

        samp = add(mat, unreal.MaterialExpressionTextureSample, -220, y)
        samp.set_editor_property("texture", tex)
        samp.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_expressions(mul_uv, "", samp, "UVs")

        # Soft edge: saturate((a - threshold) * softness)
        thr = add(mat, unreal.MaterialExpressionConstant, -40, y + 40)
        thr.set_editor_property("r", 0.35)
        sub = add(mat, unreal.MaterialExpressionSubtract, 120, y)
        unreal.MaterialEditingLibrary.connect_material_expressions(samp, "a", sub, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(thr, "", sub, "B")
        soft = add(mat, unreal.MaterialExpressionConstant, 120, y + 60)
        soft.set_editor_property("r", 3.5)
        edge = add(mat, unreal.MaterialExpressionMultiply, 280, y)
        unreal.MaterialEditingLibrary.connect_material_expressions(sub, "", edge, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(soft, "", edge, "B")
        sat = add(mat, unreal.MaterialExpressionSaturate, 440, y)
        unreal.MaterialEditingLibrary.connect_material_expressions(edge, "", sat, "")

        strc = add(mat, unreal.MaterialExpressionConstant, 440, y + 60)
        strc.set_editor_property("r", 0.55 if i == 0 else 0.35)
        layer = add(mat, unreal.MaterialExpressionMultiply, 600, y)
        unreal.MaterialEditingLibrary.connect_material_expressions(sat, "", layer, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(strc, "", layer, "B")

        if accum is None:
            accum = layer
        else:
            addn = add(mat, unreal.MaterialExpressionAdd, 760, y)
            unreal.MaterialEditingLibrary.connect_material_expressions(accum, "", addn, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(layer, "", addn, "B")
            accum = addn
        y += 180

    opac = add(mat, unreal.MaterialExpressionSaturate, 940, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(accum, "", opac, "")

    # White/soft blue cloud color
    col = add(mat, unreal.MaterialExpressionConstant3Vector, 940, -80)
    col.set_editor_property("constant", unreal.LinearColor(0.95, 0.97, 1.0, 1.0))
    tinted = add(mat, unreal.MaterialExpressionMultiply, 1100, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(col, "", tinted, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(opac, "", tinted, "B")

    unreal.MaterialEditingLibrary.connect_material_property(tinted, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(CLOUD_MAT)
    unreal.log(f"built {CLOUD_MAT}")
    return mat


def set_prop(obj, name, value) -> bool:
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        unreal.log_warning(f"{name}: {e}")
        return False


def apply_to_actor(actor, sky_mat, cloud_mat, sphere, cloud_mesh, sun_mesh, sun_mat):
    set_prop(actor, "SkyDomeMesh", sphere)
    set_prop(actor, "CloudDomeMesh", cloud_mesh)
    set_prop(actor, "SkyMaterial", sky_mat)
    set_prop(actor, "CloudMaterial", cloud_mat)
    set_prop(actor, "SkyDomeScale", unreal.Vector(SKY_SCALE, SKY_SCALE, SKY_SCALE))
    set_prop(actor, "CloudDomeScale", unreal.Vector(CLOUD_SCALE, CLOUD_SCALE, CLOUD_SCALE))
    set_prop(actor, "bFrozenTime", True)
    set_prop(actor, "FrozenTimeOfDayHours", 12.0)
    set_prop(actor, "MpcTimeOfDayName", "Current Time")
    set_prop(actor, "FogDensity", 0.0015)

    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        n = c.get_name()
        if "SkyDomeClouds" in n:
            if cloud_mesh:
                c.set_static_mesh(cloud_mesh)
            c.set_material(0, cloud_mat)
            c.set_relative_scale3d(unreal.Vector(CLOUD_SCALE, CLOUD_SCALE, CLOUD_SCALE))
            try:
                c.set_editor_property("reverse_culling", True)
            except Exception:
                pass
            c.set_visibility(True)
        elif "SkyDome" in n:
            c.set_static_mesh(sphere)
            c.set_material(0, sky_mat)
            c.set_relative_scale3d(unreal.Vector(SKY_SCALE, SKY_SCALE, SKY_SCALE))
            try:
                c.set_editor_property("reverse_culling", True)
            except Exception:
                pass
            c.set_visibility(True)
        elif "Sun" in n or "SM_Sun" in n:
            if sun_mesh:
                c.set_static_mesh(sun_mesh)
            if sun_mat:
                c.set_material(0, sun_mat)


def push_mpc_midday():
    mpc = load(MPC)
    if not mpc:
        return
    md_sky_mpc.push_scalars(
        mpc,
        {
            "Current Time": 12.0,
            "Day Cycle Progress": 0.5,
        },
    )


def run():
    sky_mat = build_sky_dome_mat()
    cloud_mat = build_soft_cloud_mat()
    sphere = load(ENGINE_SPHERE)
    cloud_mesh = load(CLOUD_MESH)
    sun_mesh = load(SUN_MESH)
    sun_mat = load(SUN_MAT)
    if not sphere:
        raise RuntimeError("Engine Sphere missing")

    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
        cdo = unreal.get_default_object(bp.generated_class())
        apply_to_actor(cdo, sky_mat, cloud_mat, sphere, cloud_mesh, sun_mesh, sun_mat)
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log_warning(f"compile: {e}")
        unreal.EditorAssetLibrary.save_asset(BP_SKY)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        lab = a.get_actor_label() or ""
        if "BP_Sky" in cls or lab == "BP_Sky":
            apply_to_actor(a, sky_mat, cloud_mat, sphere, cloud_mesh, sun_mesh, sun_mat)
            unreal.log(f"updated {lab}/{cls}")

    push_mpc_midday()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("FIX_SKY_SPHERE_AND_CLOUD_DITHER_OK")


run()
