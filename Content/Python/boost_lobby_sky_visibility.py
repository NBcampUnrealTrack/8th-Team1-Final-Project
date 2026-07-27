"""Boost clouds + rebuild sun as soft emissive sphere (SphereMask)."""
from __future__ import annotations

import unreal

MAT_DST = "/Game/Developers/TaitoP/Environment/Sky/Materials"
TEX_DST = "/Game/Developers/TaitoP/Environment/Sky/Textures"
BP_PATH = "/Game/Developers/TaitoP/Environment/Sky/BP_LobbyStylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def rebuild_sun():
    path = f"{MAT_DST}/M_LobbySunDisc"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    _clear(mat)
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -600, 0)
    # distance from center
    half = _add(mat, unreal.MaterialExpressionConstant2Vector, -600, 120)
    half.set_editor_property("r", 0.5)
    half.set_editor_property("g", 0.5)
    sub = _add(mat, unreal.MaterialExpressionSubtract, -380, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", sub, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(half, "", sub, "B")
    # length via DotProduct of self
    dot = _add(mat, unreal.MaterialExpressionDotProduct, -180, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(sub, "", dot, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(sub, "", dot, "B")
    sq = _add(mat, unreal.MaterialExpressionSquareRoot, 0, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(dot, "", sq, "")

    # soft disc: saturate(1 - dist/radius)
    rad = _add(mat, unreal.MaterialExpressionScalarParameter, -180, 200)
    rad.set_editor_property("parameter_name", "SunRadius")
    rad.set_editor_property("default_value", 0.45)
    div = _add(mat, unreal.MaterialExpressionDivide, 160, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(sq, "", div, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(rad, "", div, "B")
    one = _add(mat, unreal.MaterialExpressionConstant, 160, 160)
    one.set_editor_property("r", 1.0)
    inv = _add(mat, unreal.MaterialExpressionSubtract, 340, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", inv, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(div, "", inv, "B")
    sat = _add(mat, unreal.MaterialExpressionSaturate, 520, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(inv, "", sat, "")
    # softer falloff
    pow_n = _add(mat, unreal.MaterialExpressionPower, 700, 40)
    exp = _add(mat, unreal.MaterialExpressionConstant, 520, 160)
    exp.set_editor_property("r", 1.6)
    unreal.MaterialEditingLibrary.connect_material_expressions(sat, "", pow_n, "Base")
    unreal.MaterialEditingLibrary.connect_material_expressions(exp, "", pow_n, "Exp")

    col = _add(mat, unreal.MaterialExpressionVectorParameter, 520, -120)
    col.set_editor_property("parameter_name", "SunColor")
    col.set_editor_property("default_value", unreal.LinearColor(1.0, 0.9, 0.65, 1.0))
    inten = _add(mat, unreal.MaterialExpressionScalarParameter, 520, -40)
    inten.set_editor_property("parameter_name", "SunIntensity")
    inten.set_editor_property("default_value", 4.0)
    m1 = _add(mat, unreal.MaterialExpressionMultiply, 880, -40)
    unreal.MaterialEditingLibrary.connect_material_expressions(col, "", m1, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inten, "", m1, "B")
    m2 = _add(mat, unreal.MaterialExpressionMultiply, 1080, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(m1, "", m2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(pow_n, "", m2, "B")

    unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(pow_n, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log("rebuilt sun mat")


def rebuild_clouds_stronger():
    path = f"{MAT_DST}/M_LobbyStylizedClouds"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    mask = unreal.EditorAssetLibrary.load_asset(f"{TEX_DST}/T_LobbyClouds_Mask")
    noise = unreal.EditorAssetLibrary.load_asset(f"{TEX_DST}/T_LobbyClouds_Noise")
    _clear(mat)
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property(
            "translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL,
        )
    except Exception:
        pass
    # Disable depth test so clouds always draw as sky layer
    try:
        mat.set_editor_property("disable_depth_test", True)
    except Exception:
        pass

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    tiling = _add(mat, unreal.MaterialExpressionScalarParameter, -900, 100)
    tiling.set_editor_property("parameter_name", "CloudTiling")
    tiling.set_editor_property("default_value", 1.0)
    uv_s = _add(mat, unreal.MaterialExpressionMultiply, -700, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", uv_s, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tiling, "", uv_s, "B")

    speed = _add(mat, unreal.MaterialExpressionScalarParameter, -900, 200)
    speed.set_editor_property("parameter_name", "CloudSpeed")
    speed.set_editor_property("default_value", 0.01)
    time = _add(mat, unreal.MaterialExpressionTime, -900, 280)
    tmul = _add(mat, unreal.MaterialExpressionMultiply, -700, 220)
    unreal.MaterialEditingLibrary.connect_material_expressions(time, "", tmul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(speed, "", tmul, "B")
    pan = _add(mat, unreal.MaterialExpressionPanner, -480, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv_s, "", pan, "Coordinate")
    unreal.MaterialEditingLibrary.connect_material_expressions(tmul, "", pan, "Time")

    s_mask = _add(mat, unreal.MaterialExpressionTextureSample, -220, 0)
    s_mask.set_editor_property("texture", mask)
    s_mask.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(pan, "", s_mask, "UVs")

    tint = _add(mat, unreal.MaterialExpressionVectorParameter, 40, -100)
    tint.set_editor_property("parameter_name", "CloudTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.05, 0.98, 1.0, 1.0))
    col = _add(mat, unreal.MaterialExpressionMultiply, 260, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_mask, "", col, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", col, "B")

    dens = _add(mat, unreal.MaterialExpressionScalarParameter, 40, 200)
    dens.set_editor_property("parameter_name", "CloudDensity")
    dens.set_editor_property("default_value", 1.1)
    # opacity = saturate(mask.a * density * 1.4)
    boost = _add(mat, unreal.MaterialExpressionConstant, 40, 300)
    boost.set_editor_property("r", 1.4)
    opac = _add(mat, unreal.MaterialExpressionMultiply, 280, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_mask, "A", opac, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(dens, "", opac, "B")
    opac2 = _add(mat, unreal.MaterialExpressionMultiply, 480, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(opac, "", opac2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(boost, "", opac2, "B")
    sat = _add(mat, unreal.MaterialExpressionSaturate, 680, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(opac2, "", sat, "")

    # slight emissive lift so clouds read brighter
    bright = _add(mat, unreal.MaterialExpressionScalarParameter, 260, 100)
    bright.set_editor_property("parameter_name", "CloudBrightness")
    bright.set_editor_property("default_value", 1.25)
    col2 = _add(mat, unreal.MaterialExpressionMultiply, 480, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(col, "", col2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bright, "", col2, "B")

    unreal.MaterialEditingLibrary.connect_material_property(col2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(col2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(sat, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log("rebuilt clouds stronger")
    _ = noise  # kept for future


def apply_and_respawn():
    sky_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedSky")
    cloud_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedClouds")
    sun_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbySunDisc")
    if cloud_mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(cloud_mi, "CloudDensity", 1.1)
        unreal.EditorAssetLibrary.save_loaded_asset(cloud_mi)
    if sun_mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sun_mi, "SunIntensity", 4.0)
        unreal.EditorAssetLibrary.save_loaded_asset(sun_mi)

    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        data = bfl.get_data(h)
        try:
            obj = bfl.get_associated_object(data)
        except Exception:
            obj = bfl.get_object(data)
        if not obj or not isinstance(obj, unreal.StaticMeshComponent):
            continue
        n = obj.get_name()
        if "SkyDome" in n and sky_mi:
            obj.set_material(0, sky_mi)
            obj.set_editor_property("relative_scale3d", unreal.Vector(6000, 6000, 6000))
        elif "CloudDome" in n and cloud_mi:
            obj.set_material(0, cloud_mi)
            obj.set_editor_property("relative_scale3d", unreal.Vector(5800, 5800, 5800))
        elif "SunDisc" in n:
            obj.set_static_mesh(sphere)
            if sun_mi:
                obj.set_material(0, sun_mi)
            obj.set_editor_property("relative_location", unreal.Vector(5000.0, -2200.0, 3800.0))
            obj.set_editor_property("relative_scale3d", unreal.Vector(450.0, 450.0, 450.0))
        try:
            obj.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            obj.set_cast_shadow(False)
        except Exception:
            pass
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab == "LobbyStylizedSky" or "BP_LobbyStylizedSky" in cls:
            eas.destroy_actor(a)
    spawned = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if spawned:
        spawned.set_actor_label("LobbyStylizedSky")
        try:
            spawned.set_folder_path("Environment/Sky")
        except Exception:
            pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("BOOST_SKY_OK")


rebuild_sun()
rebuild_clouds_stronger()
apply_and_respawn()
