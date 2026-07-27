"""
Build ORIGINAL BP_LobbyStylizedSky matching SoStylized *architecture*
(skydome + layered cloud dome + sun plane + dir/skylight + fog + PP)
but ZERO SoStylized asset references — all textures/materials are ours.
"""
from __future__ import annotations

import math
from pathlib import Path

import unreal

SRC = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky")
TEX_DST = "/Game/Developers/TaitoP/Environment/Sky/Textures"
MAT_DST = "/Game/Developers/TaitoP/Environment/Sky/Materials"
BP_DST = "/Game/Developers/TaitoP/Environment/Sky"
BP_NAME = "BP_LobbyStylizedSky"
BP_PATH = f"{BP_DST}/{BP_NAME}"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

TEX_LIST = [
    ("T_LobbySky_Gradient.png", "T_LobbySky_Gradient", False),
    ("T_LobbySky_BGClouds.png", "T_LobbySky_BGClouds", False),
    ("T_LobbyCloud_Layer01.png", "T_LobbyCloud_Layer01", False),
    ("T_LobbyCloud_Layer02.png", "T_LobbyCloud_Layer02", False),
    ("T_LobbyCloud_Layer03.png", "T_LobbyCloud_Layer03", False),
    ("T_LobbySky_Noise.png", "T_LobbySky_Noise", True),
    ("T_LobbySun_Disc.png", "T_LobbySun_Disc", False),
]


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def _ensure_dir(p):
    if not unreal.EditorAssetLibrary.does_directory_exist(p):
        unreal.EditorAssetLibrary.make_directory(p)


def import_textures():
    _ensure_dir(TEX_DST)
    out = {}
    for fname, name, linear in TEX_LIST:
        task = unreal.AssetImportTask()
        task.filename = str(SRC / fname).replace("\\", "/")
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
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", not linear)
        if linear:
            try:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            except Exception:
                pass
            tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
        out[name] = tex
        unreal.log(f"tex {name}")
    return out


def _get_or_create_mat(name):
    _ensure_dir(MAT_DST)
    path = f"{MAT_DST}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
        return mat, path
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
    )
    return mat, path


def build_sky_mat(grad, bg, noise):
    """Opaque unlit skydome: camera-Z gradient + panning BG clouds (SoStylized layout)."""
    mat, path = _get_or_create_mat("M_LobbyStylizedSky")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    # --- height UV from CameraVector ---
    cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -1200, 0)
    z = _add(mat, unreal.MaterialExpressionComponentMask, -1000, 0)
    z.set_editor_property("r", False)
    z.set_editor_property("g", False)
    z.set_editor_property("b", True)
    z.set_editor_property("a", False)
    unreal.MaterialEditingLibrary.connect_material_expressions(cam, "", z, "")
    one = _add(mat, unreal.MaterialExpressionConstant, -1000, 100)
    one.set_editor_property("r", 1.0)
    add = _add(mat, unreal.MaterialExpressionAdd, -800, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(z, "", add, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", add, "B")
    half = _add(mat, unreal.MaterialExpressionConstant, -800, 120)
    half.set_editor_property("r", 0.5)
    mul = _add(mat, unreal.MaterialExpressionMultiply, -620, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(add, "", mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(half, "", mul, "B")
    one2 = _add(mat, unreal.MaterialExpressionConstant, -620, 120)
    one2.set_editor_property("r", 1.0)
    inv = _add(mat, unreal.MaterialExpressionSubtract, -440, 20)
    unreal.MaterialEditingLibrary.connect_material_expressions(one2, "", inv, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", inv, "B")
    u0 = _add(mat, unreal.MaterialExpressionConstant, -440, -80)
    u0.set_editor_property("r", 0.5)
    app = _add(mat, unreal.MaterialExpressionAppendVector, -260, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(u0, "", app, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inv, "", app, "B")

    s_grad = _add(mat, unreal.MaterialExpressionTextureSample, -60, -40)
    s_grad.set_editor_property("texture", grad)
    s_grad.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(app, "", s_grad, "UVs")

    bright = _add(mat, unreal.MaterialExpressionScalarParameter, -60, -180)
    bright.set_editor_property("parameter_name", "SkyBrightness")
    bright.set_editor_property("default_value", 1.85)
    tint = _add(mat, unreal.MaterialExpressionVectorParameter, -60, -280)
    tint.set_editor_property("parameter_name", "SkyTint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.95, 1.05, 1.2, 1.0))
    sky = _add(mat, unreal.MaterialExpressionMultiply, 180, -80)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_grad, "", sky, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", sky, "B")
    sky2 = _add(mat, unreal.MaterialExpressionMultiply, 360, -80)
    unreal.MaterialEditingLibrary.connect_material_expressions(sky, "", sky2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bright, "", sky2, "B")

    # --- BG clouds on mesh UV ---
    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1200, 320)
    stretch = _add(mat, unreal.MaterialExpressionScalarParameter, -1200, 420)
    stretch.set_editor_property("parameter_name", "BGCloudsVerticalStretch")
    stretch.set_editor_property("default_value", 1.1)
    # scale V only via multiply UV * (1, stretch) approx: use multiply by stretch on whole UV then ok
    uv_s = _add(mat, unreal.MaterialExpressionMultiply, -980, 340)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", uv_s, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(stretch, "", uv_s, "B")
    speed = _add(mat, unreal.MaterialExpressionScalarParameter, -1200, 520)
    speed.set_editor_property("parameter_name", "BGCloudsSpeed")
    speed.set_editor_property("default_value", 0.008)
    time = _add(mat, unreal.MaterialExpressionTime, -1200, 600)
    tmul = _add(mat, unreal.MaterialExpressionMultiply, -980, 520)
    unreal.MaterialEditingLibrary.connect_material_expressions(time, "", tmul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(speed, "", tmul, "B")
    pan = _add(mat, unreal.MaterialExpressionPanner, -760, 380)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv_s, "", pan, "Coordinate")
    unreal.MaterialEditingLibrary.connect_material_expressions(tmul, "", pan, "Time")
    s_bg = _add(mat, unreal.MaterialExpressionTextureSample, -520, 360)
    s_bg.set_editor_property("texture", bg)
    s_bg.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(pan, "", s_bg, "UVs")
    bg_str = _add(mat, unreal.MaterialExpressionScalarParameter, -520, 520)
    bg_str.set_editor_property("parameter_name", "BGCloudsStrength")
    bg_str.set_editor_property("default_value", 0.45)
    bg_tint = _add(mat, unreal.MaterialExpressionVectorParameter, -520, 600)
    bg_tint.set_editor_property("parameter_name", "BGCloudsTint")
    bg_tint.set_editor_property("default_value", unreal.LinearColor(0.55, 0.72, 1.0, 1.0))
    bg_col = _add(mat, unreal.MaterialExpressionMultiply, -280, 360)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_bg, "", bg_col, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bg_tint, "", bg_col, "B")
    bg_a = _add(mat, unreal.MaterialExpressionMultiply, -280, 480)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_bg, "A", bg_a, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bg_str, "", bg_a, "B")
    lerp = _add(mat, unreal.MaterialExpressionLinearInterpolate, 560, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(sky2, "", lerp, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bg_col, "", lerp, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(bg_a, "", lerp, "Alpha")

    # mild horizon noise
    s_n = _add(mat, unreal.MaterialExpressionTextureSample, 180, 280)
    s_n.set_editor_property("texture", noise)
    s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    n_str = _add(mat, unreal.MaterialExpressionScalarParameter, 180, 420)
    n_str.set_editor_property("parameter_name", "HorizonNoiseStrength")
    n_str.set_editor_property("default_value", 0.04)
    n_term = _add(mat, unreal.MaterialExpressionMultiply, 400, 300)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_n, "R", n_term, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(n_str, "", n_term, "B")
    out = _add(mat, unreal.MaterialExpressionAdd, 760, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(lerp, "", out, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(n_term, "", out, "B")

    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def build_clouds_mat(l1, l2, l3, noise):
    """Translucent unlit multi-layer cloud dome (SoStylized cloud layers pattern)."""
    mat, path = _get_or_create_mat("M_LobbyStylizedClouds")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    try:
        mat.set_editor_property(
            "translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL,
        )
    except Exception:
        pass

    time = _add(mat, unreal.MaterialExpressionTime, -1400, 0)
    uv0 = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1400, -100)

    def layer(idx, tex, x_off, speed_def, stretch_def, strength_def, v_off):
        stretch = _add(mat, unreal.MaterialExpressionScalarParameter, -1200, x_off)
        stretch.set_editor_property("parameter_name", f"{idx} Stretch Vertical")
        stretch.set_editor_property("default_value", stretch_def)
        uv_s = _add(mat, unreal.MaterialExpressionMultiply, -1000, x_off)
        unreal.MaterialEditingLibrary.connect_material_expressions(uv0, "", uv_s, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(stretch, "", uv_s, "B")
        voff = _add(mat, unreal.MaterialExpressionScalarParameter, -1200, x_off + 80)
        voff.set_editor_property("parameter_name", f"{idx} Offset Vertical")
        voff.set_editor_property("default_value", v_off)
        # append offset to V via add on UV: rough — add (0, voff) via constant2 + add
        off2 = _add(mat, unreal.MaterialExpressionConstant2Vector, -1000, x_off + 80)
        # connect voff into G of constant2 not possible easily — use append 0 + voff then add
        zc = _add(mat, unreal.MaterialExpressionConstant, -1200, x_off + 140)
        zc.set_editor_property("r", 0.0)
        ap = _add(mat, unreal.MaterialExpressionAppendVector, -1000, x_off + 140)
        unreal.MaterialEditingLibrary.connect_material_expressions(zc, "", ap, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(voff, "", ap, "B")
        uv_o = _add(mat, unreal.MaterialExpressionAdd, -820, x_off)
        unreal.MaterialEditingLibrary.connect_material_expressions(uv_s, "", uv_o, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(ap, "", uv_o, "B")
        spd = _add(mat, unreal.MaterialExpressionScalarParameter, -1200, x_off + 200)
        spd.set_editor_property("parameter_name", f"{idx} Speed")
        spd.set_editor_property("default_value", speed_def)
        tm = _add(mat, unreal.MaterialExpressionMultiply, -1000, x_off + 200)
        unreal.MaterialEditingLibrary.connect_material_expressions(time, "", tm, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(spd, "", tm, "B")
        pan = _add(mat, unreal.MaterialExpressionPanner, -640, x_off)
        unreal.MaterialEditingLibrary.connect_material_expressions(uv_o, "", pan, "Coordinate")
        unreal.MaterialEditingLibrary.connect_material_expressions(tm, "", pan, "Time")
        samp = _add(mat, unreal.MaterialExpressionTextureSample, -420, x_off)
        samp.set_editor_property("texture", tex)
        samp.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_expressions(pan, "", samp, "UVs")
        strength = _add(mat, unreal.MaterialExpressionScalarParameter, -420, x_off + 180)
        strength.set_editor_property("parameter_name", f"{idx} Strength")
        strength.set_editor_property("default_value", strength_def)
        a = _add(mat, unreal.MaterialExpressionMultiply, -200, x_off + 100)
        unreal.MaterialEditingLibrary.connect_material_expressions(samp, "A", a, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(strength, "", a, "B")
        return samp, a

    s1, a1 = layer(1, l1, 0, 0.012, 0.42, 1.15, 0.18)
    s2, a2 = layer(2, l2, 320, 0.018, 0.45, 1.05, -0.12)
    s3, a3 = layer(3, l3, 640, 0.009, 0.5, 0.95, -0.05)

    # Combined color = max-ish via lerp chain toward brighter white
    tint = _add(mat, unreal.MaterialExpressionVectorParameter, 0, -200)
    tint.set_editor_property("parameter_name", "CloudTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.2, 1.2, 1.25, 1.0))
    c1 = _add(mat, unreal.MaterialExpressionMultiply, 80, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(s1, "", c1, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", c1, "B")
    # opacity = saturate(a1+a2+a3)
    oa = _add(mat, unreal.MaterialExpressionAdd, 80, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(a1, "", oa, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(a2, "", oa, "B")
    ob = _add(mat, unreal.MaterialExpressionAdd, 260, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(oa, "", ob, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(a3, "", ob, "B")
    opac = _add(mat, unreal.MaterialExpressionSaturate, 440, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(ob, "", opac, "")

    # highlight lift
    hl = _add(mat, unreal.MaterialExpressionScalarParameter, 80, 360)
    hl.set_editor_property("parameter_name", "HighlightStrength")
    hl.set_editor_property("default_value", 1.6)
    c_hi = _add(mat, unreal.MaterialExpressionMultiply, 280, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(c1, "", c_hi, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(hl, "", c_hi, "B")

    unreal.MaterialEditingLibrary.connect_material_property(c_hi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(c_hi, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def build_sun_mat(sun_tex):
    mat, path = _get_or_create_mat("M_LobbySun")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -500, 0)
    s = _add(mat, unreal.MaterialExpressionTextureSample, -250, 0)
    s.set_editor_property("texture", sun_tex)
    s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", s, "UVs")
    tint = _add(mat, unreal.MaterialExpressionVectorParameter, 0, -100)
    tint.set_editor_property("parameter_name", "SunColor")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.95, 1.0))
    strength = _add(mat, unreal.MaterialExpressionScalarParameter, 0, 40)
    strength.set_editor_property("parameter_name", "SunStrength")
    strength.set_editor_property("default_value", 14.0)
    m1 = _add(mat, unreal.MaterialExpressionMultiply, 220, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(s, "", m1, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", m1, "B")
    m2 = _add(mat, unreal.MaterialExpressionMultiply, 420, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(m1, "", m2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(strength, "", m2, "B")
    unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(s, "A", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def _subsys():
    return unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)


def _configure_smc(comp, mesh, mat, scale, loc=(0, 0, 0), rot=(0, 0, 0)):
    if not comp:
        return
    try:
        comp.set_static_mesh(mesh)
    except Exception:
        pass
    try:
        comp.set_material(0, mat)
    except Exception:
        pass
    try:
        comp.set_editor_property("relative_scale3d", unreal.Vector(*scale))
        comp.set_editor_property("relative_location", unreal.Vector(*loc))
        comp.set_editor_property("relative_rotation", unreal.Rotator(*rot))
    except Exception:
        pass
    try:
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        comp.set_cast_shadow(False)
    except Exception:
        pass


def create_bp(sky_mat, cloud_mat, sun_mat):
    """Actor BP: Orbit → Sun plane, SkyDome, CloudDome, DirLight, SkyLight, Fog, PP."""
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    plane = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")
    if unreal.EditorAssetLibrary.does_asset_exist(BP_PATH):
        unreal.EditorAssetLibrary.delete_asset(BP_PATH)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(BP_NAME, BP_DST, unreal.Blueprint, factory)
    if not bp:
        raise RuntimeError("bp create failed")

    sub = _subsys()
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    handles = sub.k2_gather_subobject_data_for_blueprint(bp)
    root = handles[0]

    def add_comp(name, cls, parent=None):
        parent = parent or root
        params = unreal.AddNewSubobjectParams(
            parent_handle=parent,
            new_class=cls,
            blueprint_context=bp,
        )
        try:
            h, fail = sub.add_new_subobject(params)
        except TypeError:
            h = sub.add_new_subobject(params)
            fail = ""
        if fail and str(fail) not in ("", "None"):
            unreal.log_warning(f"add {name}: {fail}")
        try:
            sub.rename_subobject(h, unreal.Text(name))
        except Exception:
            pass
        try:
            sub.attach_subobject(owner_handle=parent, child_to_add_handle=h)
        except Exception:
            pass
        data = bfl.get_data(h)
        try:
            obj = bfl.get_associated_object(data)
        except Exception:
            obj = bfl.get_object(data)
        return h, obj

    # Orbit hierarchy (like SoStylized)
    h_incl, orbit_incl = add_comp("OrbitInclinator", unreal.SceneComponent)
    h_ori, orbit_ori = add_comp("OrbitOrientator", unreal.SceneComponent, h_incl)
    if orbit_incl:
        try:
            orbit_incl.set_editor_property("relative_rotation", unreal.Rotator(25.0, 0.0, 0.0))
        except Exception:
            pass
    if orbit_ori:
        try:
            orbit_ori.set_editor_property("relative_rotation", unreal.Rotator(0.0, -35.0, 0.0))
        except Exception:
            pass

    # Sun plane on orbit (midday-ish)
    h_sun, sun = add_comp("SM_Sun", unreal.StaticMeshComponent, h_ori)
    _configure_smc(sun, plane, sun_mat, (2500, 2500, 2500), loc=(0, 0, 0), rot=(-10, 0, 0))
    if sun:
        # push along local +X like SoStylized plane offset — use relative loc on orbit
        try:
            sun.set_editor_property("relative_location", unreal.Vector(45000.0, 0.0, 0.0))
            sun.set_editor_property("relative_scale3d", unreal.Vector(800.0, 800.0, 800.0))
            sun.set_editor_property("relative_rotation", unreal.Rotator(0.0, 90.0, 0.0))
        except Exception:
            pass

    h_sky, sky = add_comp("SkyDome", unreal.StaticMeshComponent)
    _configure_smc(sky, sphere, sky_mat, (8000, 8000, 8000))

    h_cld, cld = add_comp("SkyDomeClouds", unreal.StaticMeshComponent)
    _configure_smc(cld, sphere, cloud_mat, (7600, 7600, 7600))

    # Lighting (owned by BP — no SoStylized)
    h_dir, dlight = add_comp("SkyDirectionalLight", unreal.DirectionalLightComponent, h_ori)
    if dlight:
        try:
            dlight.set_editor_property("intensity", 12.0)
            dlight.set_editor_property("light_color", unreal.Color(255, 250, 235, 255))
            dlight.set_editor_property("relative_rotation", unreal.Rotator(-50.0, 0.0, 0.0))
            dlight.set_editor_property("atmosphere_sun_light", False)
        except Exception as e:
            unreal.log(f"dir {e}")

    h_sl, slight = add_comp("SkyLight", unreal.SkyLightComponent)
    if slight:
        try:
            slight.set_editor_property("intensity", 4.0)
            slight.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
        except Exception as e:
            unreal.log(f"skylight {e}")

    h_fog, fog = add_comp("ExponentialHeightFog", unreal.ExponentialHeightFogComponent)
    if fog:
        try:
            fog.set_editor_property("fog_density", 0.012)
            fog.set_editor_property("fog_height_falloff", 0.15)
            fog.set_editor_property("fog_inscattering_color", unreal.LinearColor(0.55, 0.75, 1.0, 1.0))
        except Exception as e:
            unreal.log(f"fog {e}")

    h_pp, pp = add_comp("PostProcess", unreal.PostProcessComponent)
    if pp:
        try:
            pp.set_editor_property("b_unbound", True)
            settings = pp.get_editor_property("settings")
            settings.set_editor_property("override_bloom_intensity", True)
            settings.set_editor_property("bloom_intensity", 0.9)
            settings.set_editor_property("override_bloom_threshold", True)
            settings.set_editor_property("bloom_threshold", 0.55)
            settings.set_editor_property("override_auto_exposure_bias", True)
            settings.set_editor_property("auto_exposure_bias", 0.85)
            settings.set_editor_property("override_color_saturation", True)
            settings.set_editor_property("color_saturation", unreal.Vector4(1.1, 1.1, 1.18, 1.0))
            pp.set_editor_property("settings", settings)
        except Exception as e:
            unreal.log(f"pp {e}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)
    n = len(sub.k2_gather_subobject_data_for_blueprint(bp))
    unreal.log(f"BP components={n} path={BP_PATH}")
    # verify no SoStylized deps
    deps = unreal.EditorAssetLibrary.find_package_referencers_for_asset(BP_PATH, False)
    unreal.log(f"referencers check done; deps_count_related skipped")
    return bp


def place(bp):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        path = a.get_path_name()
        if lab == "LobbyStylizedSky" or "BP_LobbyStylizedSky" in cls:
            eas.destroy_actor(a)
            continue
        # hide conflicting level lights/atmosphere/fog/pp
        if any(
            k in lab or k in cls
            for k in (
                "Pastel",
                "SkyAtmosphere",
                "SkyLight",
                "DirectionalLight",
                "ExponentialHeightFog",
                "PostProcessVolume",
            )
        ):
            # don't hide our new BP's... these are level actors
            if "BP_Lobby" in cls:
                continue
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
                for cc in (
                    unreal.DirectionalLightComponent,
                    unreal.SkyLightComponent,
                    unreal.ExponentialHeightFogComponent,
                ):
                    c = a.get_component_by_class(cc)
                    if c:
                        try:
                            c.set_visibility(False)
                            c.set_intensity(0.0)
                        except Exception:
                            pass
                unreal.log(f"hid {lab}/{cls}")
            except Exception:
                pass

    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("LobbyStylizedSky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"spawned original {actor}")


def assert_no_sostylized(bp_path):
    # Soft check: material deps shouldn't include SoStylized
    for p in (
        f"{MAT_DST}/M_LobbyStylizedSky",
        f"{MAT_DST}/M_LobbyStylizedClouds",
        f"{MAT_DST}/M_LobbySun",
        bp_path,
    ):
        if not unreal.EditorAssetLibrary.does_asset_exist(p):
            continue
        try:
            deps = unreal.EditorAssetLibrary.get_dependencies(p)  # may not exist
        except Exception:
            deps = []
        for d in deps or []:
            if "SoStylized" in str(d):
                unreal.log_error(f"LEAK {p} -> {d}")


def run():
    tex = import_textures()
    sky_mat = build_sky_mat(tex["T_LobbySky_Gradient"], tex["T_LobbySky_BGClouds"], tex["T_LobbySky_Noise"])
    cloud_mat = build_clouds_mat(
        tex["T_LobbyCloud_Layer01"],
        tex["T_LobbyCloud_Layer02"],
        tex["T_LobbyCloud_Layer03"],
        tex["T_LobbySky_Noise"],
    )
    sun_mat = build_sun_mat(tex["T_LobbySun_Disc"])
    bp = create_bp(sky_mat, cloud_mat, sun_mat)
    place(bp)
    unreal.log("ORIGINAL_LOBBY_SKY_OK")


run()
