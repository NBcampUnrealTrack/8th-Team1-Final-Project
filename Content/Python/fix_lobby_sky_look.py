"""
Fix Lobby stylized sky look:
- sky gradient by CameraVector Z (not mesh UV)
- denser soft clouds
- sun as small sphere (no edge-on plane)
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


def rebuild_sky_mat(grad):
    path = f"{MAT_DST}/M_LobbyStylizedSky"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    _clear(mat)
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass

    # Camera vector -> sky height 0..1 (zenith high, horizon mid)
    cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -900, 0)
    mask = _add(mat, unreal.MaterialExpressionComponentMask, -700, 0)
    mask.set_editor_property("r", False)
    mask.set_editor_property("g", False)
    mask.set_editor_property("b", True)
    mask.set_editor_property("a", False)
    unreal.MaterialEditingLibrary.connect_material_expressions(cam, "", mask, "")

    # remap Z from [-1,1] to [0,1]: (z+1)*0.5 ; invert so zenith (look up, z~+1) -> V~0
    one = _add(mat, unreal.MaterialExpressionConstant, -700, 120)
    one.set_editor_property("r", 1.0)
    add = _add(mat, unreal.MaterialExpressionAdd, -500, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(mask, "", add, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", add, "B")
    half = _add(mat, unreal.MaterialExpressionConstant, -500, 160)
    half.set_editor_property("r", 0.5)
    mul = _add(mat, unreal.MaterialExpressionMultiply, -320, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(add, "", mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(half, "", mul, "B")
    # Invert: 1 - t  so look-up (1) -> 0 (zenith of our gradient texture)
    one2 = _add(mat, unreal.MaterialExpressionConstant, -320, 160)
    one2.set_editor_property("r", 1.0)
    inv = _add(mat, unreal.MaterialExpressionSubtract, -140, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(one2, "", inv, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", inv, "B")

    # UV = (0.5, height)
    uconst = _add(mat, unreal.MaterialExpressionConstant, -140, -80)
    uconst.set_editor_property("r", 0.5)
    app = _add(mat, unreal.MaterialExpressionAppendVector, 40, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(uconst, "", app, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inv, "", app, "B")

    s = _add(mat, unreal.MaterialExpressionTextureSample, 260, 0)
    s.set_editor_property("texture", grad)
    s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(app, "", s, "UVs")

    tint = _add(mat, unreal.MaterialExpressionVectorParameter, 260, -160)
    tint.set_editor_property("parameter_name", "SkyTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    m1 = _add(mat, unreal.MaterialExpressionMultiply, 500, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(s, "", m1, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", m1, "B")

    bright = _add(mat, unreal.MaterialExpressionScalarParameter, 500, 140)
    bright.set_editor_property("parameter_name", "Brightness")
    bright.set_editor_property("default_value", 1.45)
    m2 = _add(mat, unreal.MaterialExpressionMultiply, 700, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(m1, "", m2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bright, "", m2, "B")

    unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"rebuilt {path}")
    return mat


def bump_cloud_density():
    path = f"{MAT_DST}/M_LobbyStylizedClouds"
    mat = unreal.EditorAssetLibrary.load_asset(path)
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        try:
            if e.get_editor_property("parameter_name") == "CloudDensity":
                e.set_editor_property("default_value", 0.55)
        except Exception:
            pass
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    # MIC
    mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedClouds")
    if mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "CloudDensity", 0.55)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
    unreal.log("CloudDensity -> 0.55")


def fix_sun_as_sphere():
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    sun_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbySunDisc")
    if not sun_mi:
        sun_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/M_LobbySunDisc")
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
        if "SunDisc" not in obj.get_name():
            continue
        obj.set_static_mesh(sphere)
        if sun_mi:
            obj.set_material(0, sun_mi)
        obj.set_editor_property("relative_location", unreal.Vector(4200.0, -1800.0, 3200.0))
        obj.set_editor_property("relative_rotation", unreal.Rotator(0, 0, 0))
        obj.set_editor_property("relative_scale3d", unreal.Vector(350.0, 350.0, 350.0))
        try:
            obj.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            obj.set_cast_shadow(False)
        except Exception:
            pass
        unreal.log("SunDisc -> sphere")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)


def respawn():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
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
    unreal.log("FIX_SKY_LOOK_OK")


def run():
    grad = unreal.EditorAssetLibrary.load_asset(f"{TEX_DST}/T_LobbySky_Gradient")
    try:
        grad.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        grad.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_loaded_asset(grad)
    except Exception:
        pass
    rebuild_sky_mat(grad)
    # refresh sky MIC parent params
    sky_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedSky")
    if sky_mi:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(sky_mi, "Brightness", 1.45)
        unreal.EditorAssetLibrary.save_loaded_asset(sky_mi)
    bump_cloud_density()
    fix_sun_as_sphere()
    # ensure BP uses MICs
    cloud_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedClouds")
    sky_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbyStylizedSky")
    sun_mi = unreal.EditorAssetLibrary.load_asset(f"{MAT_DST}/MI_LobbySunDisc")
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
        elif "CloudDome" in n and cloud_mi:
            obj.set_material(0, cloud_mi)
        elif "SunDisc" in n and sun_mi:
            obj.set_material(0, sun_mi)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)
    respawn()


run()
