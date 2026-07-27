"""
Composite clouds into sky material (reliable) + opaque emissive sun sphere.
"""
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


def rebuild_sky_with_clouds(grad, mask):
    path = f"{MAT_DST}/M_LobbyStylizedSky"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    _clear(mat)
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass

    # --- Gradient via CameraVector Z ---
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
    sky_tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    sky_col = _add(mat, unreal.MaterialExpressionMultiply, 280, -80)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_grad, "", sky_col, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(sky_tint, "", sky_col, "B")

    # --- Clouds panned on mesh UV ---
    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 280)
    tiling = _add(mat, unreal.MaterialExpressionScalarParameter, -1100, 380)
    tiling.set_editor_property("parameter_name", "CloudTiling")
    tiling.set_editor_property("default_value", 1.25)
    uv_s = _add(mat, unreal.MaterialExpressionMultiply, -880, 300)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", uv_s, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tiling, "", uv_s, "B")
    speed = _add(mat, unreal.MaterialExpressionScalarParameter, -1100, 480)
    speed.set_editor_property("parameter_name", "CloudSpeed")
    speed.set_editor_property("default_value", 0.012)
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
    c_tint.set_editor_property("default_value", unreal.LinearColor(1.05, 0.97, 0.98, 1))
    c_col = _add(mat, unreal.MaterialExpressionMultiply, 40, 280)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_cloud, "", c_col, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(c_tint, "", c_col, "B")

    dens = _add(mat, unreal.MaterialExpressionScalarParameter, -180, 420)
    dens.set_editor_property("parameter_name", "CloudDensity")
    dens.set_editor_property("default_value", 0.85)
    # coverage boost
    boost = _add(mat, unreal.MaterialExpressionConstant, -180, 500)
    boost.set_editor_property("r", 1.6)
    cov = _add(mat, unreal.MaterialExpressionMultiply, 40, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_cloud, "A", cov, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(dens, "", cov, "B")
    cov2 = _add(mat, unreal.MaterialExpressionMultiply, 220, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(cov, "", cov2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(boost, "", cov2, "B")
    cov_s = _add(mat, unreal.MaterialExpressionSaturate, 400, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(cov2, "", cov_s, "")

    # Lerp sky -> clouds
    lerp = _add(mat, unreal.MaterialExpressionLinearInterpolate, 520, 80)
    unreal.MaterialEditingLibrary.connect_material_expressions(sky_col, "", lerp, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(c_col, "", lerp, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(cov_s, "", lerp, "Alpha")

    bright = _add(mat, unreal.MaterialExpressionScalarParameter, 520, 260)
    bright.set_editor_property("parameter_name", "Brightness")
    bright.set_editor_property("default_value", 1.4)
    out = _add(mat, unreal.MaterialExpressionMultiply, 740, 80)
    unreal.MaterialEditingLibrary.connect_material_expressions(lerp, "", out, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bright, "", out, "B")

    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log("sky+clouds composite OK")


def rebuild_opaque_sun():
    path = f"{MAT_DST}/M_LobbySunDisc"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    _clear(mat)
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass
    col = _add(mat, unreal.MaterialExpressionVectorParameter, -200, 0)
    col.set_editor_property("parameter_name", "SunColor")
    col.set_editor_property("default_value", unreal.LinearColor(1.0, 0.88, 0.55, 1.0))
    inten = _add(mat, unreal.MaterialExpressionScalarParameter, -200, 120)
    inten.set_editor_property("parameter_name", "SunIntensity")
    inten.set_editor_property("default_value", 6.0)
    mul = _add(mat, unreal.MaterialExpressionMultiply, 80, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(col, "", mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inten, "", mul, "B")
    # soft fresnel rim so it reads as a glowing ball
    fres = _add(mat, unreal.MaterialExpressionFresnel, -200, 240)
    try:
        fres.set_editor_property("exponent_in", 3.0)
        fres.set_editor_property("base_reflect_fraction_in", 0.04)
    except Exception:
        pass
    rim = _add(mat, unreal.MaterialExpressionConstant, 80, 200)
    rim.set_editor_property("r", 0.35)
    rim_m = _add(mat, unreal.MaterialExpressionMultiply, 260, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(fres, "", rim_m, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(rim, "", rim_m, "B")
    one = _add(mat, unreal.MaterialExpressionConstant, 260, 280)
    one.set_editor_property("r", 1.0)
    core = _add(mat, unreal.MaterialExpressionSubtract, 440, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", core, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(rim_m, "", core, "B")
    out = _add(mat, unreal.MaterialExpressionMultiply, 620, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", out, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(core, "", out, "B")
    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log("opaque sun OK")


def apply_bp():
    # denser cloud source again
    try:
        task = unreal.AssetImportTask()
        task.filename = "E:/Unreal/MarineDemolition/Art/Textures/Sky/T_LobbyClouds_Mask.png"
        task.destination_path = TEX_DST
        task.destination_name = "T_LobbyClouds_Mask"
        task.replace_existing = True
        task.automated = True
        task.save = True
        task.factory = unreal.TextureFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    except Exception:
        pass

    sky_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedSky")
    sun_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbySunDisc")
    if sky_mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sky_mi, "CloudDensity", 0.9)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sky_mi, "Brightness", 1.4)
        unreal.EditorAssetLibrary.save_loaded_asset(sky_mi)
    if sun_mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sun_mi, "SunIntensity", 6.0)
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
            # hide separate cloud dome — clouds now in sky mat
            try:
                obj.set_visibility(False)
            except Exception:
                pass
            try:
                obj.set_editor_property("visible", False)
            except Exception:
                pass
        elif "SunDisc" in n:
            obj.set_static_mesh(sphere)
            if sun_mi:
                obj.set_material(0, sun_mi)
            # place high in sky, large enough to read
            obj.set_editor_property("relative_location", unreal.Vector(5500.0, -2500.0, 4200.0))
            obj.set_editor_property("relative_scale3d", unreal.Vector(550.0, 550.0, 550.0))
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
    spawned = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if spawned:
        spawned.set_actor_label("LobbyStylizedSky")
        try:
            spawned.set_folder_path("Environment/Sky")
        except Exception:
            pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("COMPOSITE_SKY_OK")


def run():
    # regenerate denser clouds on disk then import
    import subprocess
    subprocess.run(
        ["python", r"E:\Unreal\MarineDemolition\Art\Textures\Sky\build_lobby_sky_textures.py"],
        check=False,
    )
    # temporarily denser alpha for visibility — override mask gen via quick inline
    try:
        from pathlib import Path
        import numpy as np
        from PIL import Image, ImageFilter

        p = Path(r"E:\Unreal\MarineDemolition\Art\Textures\Sky\T_LobbyClouds_Mask.png")
        im = Image.open(p).convert("RGBA")
        arr = np.asarray(im).astype(np.float32) / 255.0
        # boost alpha coverage
        arr[..., 3] = np.clip(arr[..., 3] * 2.2 + 0.08, 0, 0.95)
        # brighten rgb
        arr[..., :3] = np.clip(arr[..., :3] * 1.05, 0, 1)
        Image.fromarray((arr * 255).astype(np.uint8), "RGBA").save(p)
        unreal.log("boosted cloud mask alpha on disk")
    except Exception as e:
        unreal.log_warning(f"mask boost: {e}")

    grad = unreal.EditorAssetLibrary.load_asset(f"{TEX_DST}/T_LobbySky_Gradient")
    try:
        grad.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        grad.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_loaded_asset(grad)
    except Exception:
        pass

    # import boosted mask
    task = unreal.AssetImportTask()
    task.filename = "E:/Unreal/MarineDemolition/Art/Textures/Sky/T_LobbyClouds_Mask.png"
    task.destination_path = TEX_DST
    task.destination_name = "T_LobbyClouds_Mask"
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mask = unreal.EditorAssetLibrary.load_asset(f"{TEX_DST}/T_LobbyClouds_Mask")
    if mask:
        mask.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_loaded_asset(mask)

    rebuild_sky_with_clouds(grad, mask)
    rebuild_opaque_sun()
    apply_bp()


run()
