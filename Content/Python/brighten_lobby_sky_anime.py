"""
Push Lobby stylized sky toward bright anime day look (ref: vivid cyan + white clouds + bloom sun).
"""
from __future__ import annotations

import unreal

TEX_DST = "/Game/Developers/TaitoP/Environment/Sky/Textures"
MAT_DST = "/Game/Developers/TaitoP/Environment/Sky/Materials"
BP_PATH = "/Game/Developers/TaitoP/Environment/Sky/BP_LobbyStylizedSky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
SRC = r"E:/Unreal/MarineDemolition/Art/Textures/Sky"

TEX_FILES = [
    ("T_LobbySky_Gradient.png", "T_LobbySky_Gradient", False),
    ("T_LobbyClouds_Mask.png", "T_LobbyClouds_Mask", False),
    ("T_LobbyClouds_Noise.png", "T_LobbyClouds_Noise", True),
    ("T_LobbySun_Disc.png", "T_LobbySun_Disc", False),
]


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def import_textures():
    out = {}
    for fname, name, linear in TEX_FILES:
        task = unreal.AssetImportTask()
        task.filename = f"{SRC}/{fname}"
        task.destination_path = TEX_DST
        task.destination_name = name
        task.replace_existing = True
        task.automated = True
        task.save = True
        task.factory = unreal.TextureFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.EditorAssetLibrary.load_asset(f"{TEX_DST}/{name}")
        if not tex:
            raise RuntimeError(name)
        # Never normalmap — force color/grayscale
        if linear:
            try:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            except Exception:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            tex.set_editor_property("srgb", False)
        else:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            tex.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
        out[name] = tex
        unreal.log(f"imported {name}")
    return out


def rebuild_sky(grad, mask):
    path = f"{MAT_DST}/M_LobbyStylizedSky"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    _clear(mat)
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass

    # Gradient by CameraVector Z
    cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -1100, -40)
    zmask = _add(mat, unreal.MaterialExpressionComponentMask, -900, -40)
    zmask.set_editor_property("r", False)
    zmask.set_editor_property("g", False)
    zmask.set_editor_property("b", True)
    zmask.set_editor_property("a", False)
    unreal.MaterialEditingLibrary.connect_material_expressions(cam, "", zmask, "")
    one = _add(mat, unreal.MaterialExpressionConstant, -900, 80)
    one.set_editor_property("r", 1.0)
    add = _add(mat, unreal.MaterialExpressionAdd, -700, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(zmask, "", add, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", add, "B")
    half = _add(mat, unreal.MaterialExpressionConstant, -700, 100)
    half.set_editor_property("r", 0.5)
    mul = _add(mat, unreal.MaterialExpressionMultiply, -520, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(add, "", mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(half, "", mul, "B")
    one2 = _add(mat, unreal.MaterialExpressionConstant, -520, 100)
    one2.set_editor_property("r", 1.0)
    inv = _add(mat, unreal.MaterialExpressionSubtract, -340, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(one2, "", inv, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", inv, "B")
    uconst = _add(mat, unreal.MaterialExpressionConstant, -340, -100)
    uconst.set_editor_property("r", 0.5)
    app = _add(mat, unreal.MaterialExpressionAppendVector, -160, -40)
    unreal.MaterialEditingLibrary.connect_material_expressions(uconst, "", app, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inv, "", app, "B")

    s_grad = _add(mat, unreal.MaterialExpressionTextureSample, 40, -80)
    s_grad.set_editor_property("texture", grad)
    s_grad.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(app, "", s_grad, "UVs")

    sky_tint = _add(mat, unreal.MaterialExpressionVectorParameter, 40, -240)
    sky_tint.set_editor_property("parameter_name", "SkyTint")
    # slight cyan push
    sky_tint.set_editor_property("default_value", unreal.LinearColor(0.95, 1.05, 1.15, 1.0))
    sky_col = _add(mat, unreal.MaterialExpressionMultiply, 280, -80)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_grad, "", sky_col, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(sky_tint, "", sky_col, "B")

    # Clouds
    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 280)
    tiling = _add(mat, unreal.MaterialExpressionScalarParameter, -1100, 380)
    tiling.set_editor_property("parameter_name", "CloudTiling")
    tiling.set_editor_property("default_value", 1.1)
    uv_s = _add(mat, unreal.MaterialExpressionMultiply, -880, 300)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", uv_s, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tiling, "", uv_s, "B")
    speed = _add(mat, unreal.MaterialExpressionScalarParameter, -1100, 480)
    speed.set_editor_property("parameter_name", "CloudSpeed")
    speed.set_editor_property("default_value", 0.01)
    time = _add(mat, unreal.MaterialExpressionTime, -1100, 560)
    tmul = _add(mat, unreal.MaterialExpressionMultiply, -880, 480)
    unreal.MaterialEditingLibrary.connect_material_expressions(time, "", tmul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(speed, "", tmul, "B")
    pan = _add(mat, unreal.MaterialExpressionPanner, -660, 340)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv_s, "", pan, "Coordinate")
    unreal.MaterialEditingLibrary.connect_material_expressions(tmul, "", pan, "Time")

    s_cloud = _add(mat, unreal.MaterialExpressionTextureSample, -400, 320)
    s_cloud.set_editor_property("texture", mask)
    s_cloud.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(pan, "", s_cloud, "UVs")

    c_tint = _add(mat, unreal.MaterialExpressionVectorParameter, -180, 220)
    c_tint.set_editor_property("parameter_name", "CloudTint")
    c_tint.set_editor_property("default_value", unreal.LinearColor(1.15, 1.15, 1.2, 1.0))
    c_col = _add(mat, unreal.MaterialExpressionMultiply, 40, 280)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_cloud, "", c_col, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(c_tint, "", c_col, "B")

    dens = _add(mat, unreal.MaterialExpressionScalarParameter, -180, 420)
    dens.set_editor_property("parameter_name", "CloudDensity")
    dens.set_editor_property("default_value", 0.75)
    boost = _add(mat, unreal.MaterialExpressionConstant, -180, 500)
    boost.set_editor_property("r", 1.35)
    cov = _add(mat, unreal.MaterialExpressionMultiply, 40, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_cloud, "A", cov, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(dens, "", cov, "B")
    cov2 = _add(mat, unreal.MaterialExpressionMultiply, 220, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(cov, "", cov2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(boost, "", cov2, "B")
    cov_s = _add(mat, unreal.MaterialExpressionSaturate, 400, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(cov2, "", cov_s, "")

    lerp = _add(mat, unreal.MaterialExpressionLinearInterpolate, 520, 80)
    unreal.MaterialEditingLibrary.connect_material_expressions(sky_col, "", lerp, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(c_col, "", lerp, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(cov_s, "", lerp, "Alpha")

    bright = _add(mat, unreal.MaterialExpressionScalarParameter, 520, 260)
    bright.set_editor_property("parameter_name", "Brightness")
    bright.set_editor_property("default_value", 2.2)
    out = _add(mat, unreal.MaterialExpressionMultiply, 740, 80)
    unreal.MaterialEditingLibrary.connect_material_expressions(lerp, "", out, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bright, "", out, "B")

    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log("rebuilt bright sky mat")


def rebuild_sun():
    path = f"{MAT_DST}/M_LobbySunDisc"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    _clear(mat)
    try:
        # Additive soft glow ball
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    half = _add(mat, unreal.MaterialExpressionConstant2Vector, -700, 120)
    half.set_editor_property("r", 0.5)
    half.set_editor_property("g", 0.5)
    sub = _add(mat, unreal.MaterialExpressionSubtract, -480, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", sub, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(half, "", sub, "B")
    dot = _add(mat, unreal.MaterialExpressionDotProduct, -280, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(sub, "", dot, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(sub, "", dot, "B")
    dist = _add(mat, unreal.MaterialExpressionSquareRoot, -100, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(dot, "", dist, "")

    rad = _add(mat, unreal.MaterialExpressionScalarParameter, -280, 180)
    rad.set_editor_property("parameter_name", "SunRadius")
    rad.set_editor_property("default_value", 0.55)
    div = _add(mat, unreal.MaterialExpressionDivide, 80, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(dist, "", div, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(rad, "", div, "B")
    one = _add(mat, unreal.MaterialExpressionConstant, 80, 140)
    one.set_editor_property("r", 1.0)
    inv = _add(mat, unreal.MaterialExpressionSubtract, 260, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", inv, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(div, "", inv, "B")
    sat = _add(mat, unreal.MaterialExpressionSaturate, 440, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(inv, "", sat, "")
    pow_n = _add(mat, unreal.MaterialExpressionPower, 620, 40)
    exp = _add(mat, unreal.MaterialExpressionConstant, 440, 140)
    exp.set_editor_property("r", 2.2)
    unreal.MaterialEditingLibrary.connect_material_expressions(sat, "", pow_n, "Base")
    unreal.MaterialEditingLibrary.connect_material_expressions(exp, "", pow_n, "Exp")

    col = _add(mat, unreal.MaterialExpressionVectorParameter, 440, -100)
    col.set_editor_property("parameter_name", "SunColor")
    col.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.95, 1.0))
    inten = _add(mat, unreal.MaterialExpressionScalarParameter, 440, -20)
    inten.set_editor_property("parameter_name", "SunIntensity")
    inten.set_editor_property("default_value", 18.0)
    m1 = _add(mat, unreal.MaterialExpressionMultiply, 780, -20)
    unreal.MaterialEditingLibrary.connect_material_expressions(col, "", m1, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inten, "", m1, "B")
    m2 = _add(mat, unreal.MaterialExpressionMultiply, 980, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(m1, "", m2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(pow_n, "", m2, "B")

    unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(pow_n, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log("rebuilt bloom sun mat")


def update_mics():
    sky_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedSky")
    sun_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbySunDisc")
    if sky_mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sky_mi, "Brightness", 2.2)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sky_mi, "CloudDensity", 0.75)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            sky_mi, "SkyTint", unreal.LinearColor(0.95, 1.05, 1.15, 1.0)
        )
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            sky_mi, "CloudTint", unreal.LinearColor(1.15, 1.15, 1.2, 1.0)
        )
        unreal.EditorAssetLibrary.save_loaded_asset(sky_mi)
    if sun_mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sun_mi, "SunIntensity", 18.0)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            sun_mi, "SunColor", unreal.LinearColor(1.0, 1.0, 0.95, 1.0)
        )
        unreal.EditorAssetLibrary.save_loaded_asset(sun_mi)


def apply_bp_and_lighting():
    sky_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedSky")
    sun_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbySunDisc")
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
        if "SkyDome" in n:
            obj.set_static_mesh(sphere)
            if sky_mi:
                obj.set_material(0, sky_mi)
            obj.set_editor_property("relative_scale3d", unreal.Vector(6500, 6500, 6500))
            try:
                obj.set_visibility(True)
            except Exception:
                pass
        elif "CloudDome" in n:
            try:
                obj.set_visibility(False)
            except Exception:
                pass
        elif "SunDisc" in n:
            obj.set_static_mesh(sphere)
            if sun_mi:
                obj.set_material(0, sun_mi)
            # high in frame, large bloom ball
            obj.set_editor_property("relative_location", unreal.Vector(3800.0, -1600.0, 2800.0))
            obj.set_editor_property("relative_scale3d", unreal.Vector(1100.0, 1100.0, 1100.0))
            try:
                obj.set_visibility(True)
            except Exception:
                pass
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
        # Brighten skylight if present
        if "SkyLight" in cls or "SkyLight" in lab:
            try:
                a.set_editor_property("intensity", 3.5)
            except Exception:
                pass
            try:
                comp = a.get_component_by_class(unreal.SkyLightComponent)
                if comp:
                    comp.set_editor_property("intensity", 3.5)
                    try:
                        comp.set_editor_property("lower_hemisphere_is_black", False)
                    except Exception:
                        pass
            except Exception as e:
                unreal.log(f"skylight: {e}")
        if "DirectionalLight" in cls:
            try:
                comp = a.get_component_by_class(unreal.DirectionalLightComponent)
                if comp:
                    comp.set_editor_property("intensity", 8.0)
                    comp.set_editor_property(
                        "light_color", unreal.Color(255, 250, 240, 255)
                    )
            except Exception:
                pass
        if "SkyAtmosphere" in cls or "Atmosphere" in lab:
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass
        # Post process volume exposure bump if any
        if "PostProcess" in cls or "PostProcess" in lab:
            try:
                a.set_editor_property("settings", a.get_editor_property("settings"))
            except Exception:
                pass

    spawned = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if spawned:
        spawned.set_actor_label("LobbyStylizedSky")
        try:
            spawned.set_folder_path("Environment/Sky")
        except Exception:
            pass

    # Try raise exposure via console
    try:
        unreal.SystemLibrary.execute_console_command(None, "r.EyeAdaptationQuality 0")
        unreal.SystemLibrary.execute_console_command(None, "r.BloomQuality 5")
    except Exception:
        pass

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("BRIGHT_ANIME_SKY_OK")


def run():
    tex = import_textures()
    rebuild_sky(tex["T_LobbySky_Gradient"], tex["T_LobbyClouds_Mask"])
    rebuild_sun()
    update_mics()
    apply_bp_and_lighting()


run()
