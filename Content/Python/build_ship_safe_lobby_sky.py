"""
Ship-safe ORIGINAL Lobby sky — zero SoStylized / Owned-migrated assets.
Uses only Art/Textures/Sky procedural PNGs + engine basic shapes.

Removes LobbyStylizedSky that references SoStylized/Owned BP_StylizedSky.
"""
from __future__ import annotations

from pathlib import Path

import unreal

ROOT = "/Game/Developers/TaitoP/Environment/Sky"
TEX_DST = f"{ROOT}/Textures"
MAT_DST = f"{ROOT}/Materials"
BP_NAME = "BP_LobbyOriginalSky"
BP_PATH = f"{ROOT}/{BP_NAME}"
MPC_PATH = f"{ROOT}/MPC_LobbySky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
ART = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky")
FORBIDDEN = ("SoStylized", "/Owned/")


def ensure_dir(p):
    if not unreal.EditorAssetLibrary.does_directory_exist(p):
        unreal.EditorAssetLibrary.make_directory(p)


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def _connect(a, ap, b, bp=""):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, ap, b, bp)


def _mpc_scalar(mat, mpc, name, x, y):
    """Scalar CollectionParameter (safe on SM6)."""
    node = _add(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", mpc)
    node.set_editor_property("parameter_name", unreal.Name(name))
    return node


def _const_color(mat, rgba, x, y):
    """Constant RGB — use instead of vector MPC (vector CollectionParameter breaks SM6 via Python)."""
    node = _add(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    node.set_editor_property("constant", unreal.LinearColor(*rgba))
    return node


def import_tex(src: Path, name: str, srgb=True):
    ensure_dir(TEX_DST)
    task = unreal.AssetImportTask()
    task.filename = str(src).replace("\\", "/")
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
    tex.set_editor_property("srgb", srgb)
    if "Atlas" in name or "Gradient" in name:
        try:
            tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
            tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        except Exception:
            pass
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    unreal.log(f"tex {name}")
    return tex


def import_all():
    mapping = [
        ("T_LobbySky_CurveAtlas.png", "T_LobbySky_CurveAtlas", True),
        ("T_LobbySky_HeightGradient.png", "T_LobbySky_HeightGradient", True),
        ("T_LobbySky_BGClouds.png", "T_LobbyBGClouds", True),
        ("T_LobbyCloud_Layer01.png", "T_LobbyCloud_L1", True),
        ("T_LobbyCloud_Layer02.png", "T_LobbyCloud_L2", True),
        ("T_LobbyCloud_Layer03.png", "T_LobbyCloud_L3", True),
        ("T_LobbySun_Disc.png", "T_LobbySun_Body", True),
        ("T_LobbySky_Noise.png", "T_LobbySky_Noise", False),
    ]
    out = {}
    for src_name, dst, srgb in mapping:
        src = ART / src_name
        if not src.exists():
            raise RuntimeError(f"missing {src} — run Art/Textures/Sky/build_lobby_sky_textures.py")
        out[dst] = import_tex(src, dst, srgb)
    return out


def create_mpc():
    ensure_dir(ROOT)
    if unreal.EditorAssetLibrary.does_asset_exist(MPC_PATH):
        unreal.EditorAssetLibrary.delete_asset(MPC_PATH)
    mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MPC_LobbySky", ROOT, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew()
    )
    scalars = [
        ("DayCycleProgress", 0.48),
        ("DayLengthSeconds", 600.0),
        ("SkyBrightness", 2.1),
        ("BGCloudsStrength", 0.48),
        ("CloudStrength", 1.15),
        ("SunStrength", 18.0),
        ("Frozen", 1.0),  # lobby: freeze midday
        ("PhaseRow", 0.0),
    ]
    arr = []
    for name, val in scalars:
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", float(val))
        arr.append(p)
    mpc.set_editor_property("scalar_parameters", arr)
    vecs = []
    for name, rgba in (
        ("SunColor", (1.0, 0.98, 0.92, 1.0)),
        ("CloudTint", (1.12, 1.14, 1.18, 1.0)),
        ("SkyTint", (0.95, 1.06, 1.18, 1.0)),
    ):
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(*rgba))
        vecs.append(p)
    mpc.set_editor_property("vector_parameters", vecs)
    unreal.EditorAssetLibrary.save_asset(MPC_PATH)
    return mpc


def build_sky_mat(mpc, atlas, height, bg, l1, l2, l3):
    """Opaque unlit skydome: height gradient + soft cloud masks (ref-varied textures)."""
    ensure_dir(MAT_DST)
    path = f"{MAT_DST}/M_LobbyOriginalSky"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyOriginalSky", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    time = _add(mat, unreal.MaterialExpressionTime, -1600, -200)
    day_len = _add(mat, unreal.MaterialExpressionCollectionParameter, -1600, -100)
    day_len.set_editor_property("collection", mpc)
    day_len.set_editor_property("parameter_name", "DayLengthSeconds")
    div = _add(mat, unreal.MaterialExpressionDivide, -1380, -160)
    _connect(time, "", div, "A")
    _connect(day_len, "", div, "B")
    fr = _add(mat, unreal.MaterialExpressionFrac, -1200, -160)
    _connect(div, "", fr, "")

    frozen = _add(mat, unreal.MaterialExpressionCollectionParameter, -1600, 0)
    frozen.set_editor_property("collection", mpc)
    frozen.set_editor_property("parameter_name", "Frozen")
    prog = _add(mat, unreal.MaterialExpressionCollectionParameter, -1600, 80)
    prog.set_editor_property("collection", mpc)
    prog.set_editor_property("parameter_name", "DayCycleProgress")
    day_u = _add(mat, unreal.MaterialExpressionLinearInterpolate, -1000, -100)
    _connect(fr, "", day_u, "A")
    _connect(prog, "", day_u, "B")
    _connect(frozen, "", day_u, "Alpha")

    row = _add(mat, unreal.MaterialExpressionCollectionParameter, -1600, 180)
    row.set_editor_property("collection", mpc)
    row.set_editor_property("parameter_name", "PhaseRow")
    rows = _add(mat, unreal.MaterialExpressionConstant, -1200, 200)
    rows.set_editor_property("r", 5.0)
    half = _add(mat, unreal.MaterialExpressionConstant, -1200, 260)
    half.set_editor_property("r", 0.5)
    row_a = _add(mat, unreal.MaterialExpressionAdd, -1000, 200)
    _connect(row, "", row_a, "A")
    _connect(half, "", row_a, "B")
    row_v = _add(mat, unreal.MaterialExpressionDivide, -820, 200)
    _connect(row_a, "", row_v, "A")
    _connect(rows, "", row_v, "B")
    atlas_uv = _add(mat, unreal.MaterialExpressionAppendVector, -640, -40)
    _connect(day_u, "", atlas_uv, "A")
    _connect(row_v, "", atlas_uv, "B")
    s_atlas = _add(mat, unreal.MaterialExpressionTextureSample, -420, -60)
    s_atlas.set_editor_property("texture", atlas)
    s_atlas.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(atlas_uv, "", s_atlas, "UVs")

    cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -1600, 360)
    z = _add(mat, unreal.MaterialExpressionComponentMask, -1400, 360)
    z.set_editor_property("r", False)
    z.set_editor_property("g", False)
    z.set_editor_property("b", True)
    z.set_editor_property("a", False)
    _connect(cam, "", z, "")
    one = _add(mat, unreal.MaterialExpressionConstant, -1400, 460)
    one.set_editor_property("r", 1.0)
    add = _add(mat, unreal.MaterialExpressionAdd, -1200, 380)
    _connect(z, "", add, "A")
    _connect(one, "", add, "B")
    half2 = _add(mat, unreal.MaterialExpressionConstant, -1200, 460)
    half2.set_editor_property("r", 0.5)
    mul = _add(mat, unreal.MaterialExpressionMultiply, -1000, 380)
    _connect(add, "", mul, "A")
    _connect(half2, "", mul, "B")
    inv = _add(mat, unreal.MaterialExpressionOneMinus, -820, 380)
    _connect(mul, "", inv, "")
    # horizon-weighted cloud band (zenith stays clearer blue)
    zb = _add(mat, unreal.MaterialExpressionSaturate, -640, 500)
    _connect(inv, "", zb, "")
    zb2 = _add(mat, unreal.MaterialExpressionMultiply, -460, 520)
    _connect(zb, "", zb2, "A")
    _connect(zb, "", zb2, "B")

    u0 = _add(mat, unreal.MaterialExpressionConstant, -820, 300)
    u0.set_editor_property("r", 0.5)
    h_uv = _add(mat, unreal.MaterialExpressionAppendVector, -640, 360)
    _connect(u0, "", h_uv, "A")
    _connect(inv, "", h_uv, "B")
    s_h = _add(mat, unreal.MaterialExpressionTextureSample, -420, 340)
    s_h.set_editor_property("texture", height)
    s_h.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(h_uv, "", s_h, "UVs")

    # Soft blend atlas day tint onto height gradient (not hard multiply washout)
    atlas_w = _add(mat, unreal.MaterialExpressionConstant, -200, 40)
    atlas_w.set_editor_property("r", 0.35)
    atlas_mix = _add(mat, unreal.MaterialExpressionLinearInterpolate, -40, 80)
    _connect(s_h, "", atlas_mix, "A")
    _connect(s_atlas, "", atlas_mix, "B")
    _connect(atlas_w, "", atlas_mix, "Alpha")
    bright = _add(mat, unreal.MaterialExpressionCollectionParameter, -200, -40)
    bright.set_editor_property("collection", mpc)
    bright.set_editor_property("parameter_name", "SkyBrightness")
    sky = _add(mat, unreal.MaterialExpressionMultiply, 140, 60)
    _connect(atlas_mix, "", sky, "A")
    _connect(bright, "", sky, "B")
    tint = _const_color(mat, (0.95, 1.06, 1.18, 1.0), 140, -40)
    sky_t = _add(mat, unreal.MaterialExpressionMultiply, 320, 40)
    _connect(sky, "", sky_t, "A")
    _connect(tint, "", sky_t, "B")

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1600, 600)

    def pan_sample(tex, y, spd, u_scale):
        st = _add(mat, unreal.MaterialExpressionConstant, -1400, y)
        st.set_editor_property("r", u_scale)
        uv_s = _add(mat, unreal.MaterialExpressionMultiply, -1200, y)
        _connect(uv, "", uv_s, "A")
        _connect(st, "", uv_s, "B")
        sp = _add(mat, unreal.MaterialExpressionConstant, -1400, y + 80)
        sp.set_editor_property("r", spd)
        tm = _add(mat, unreal.MaterialExpressionMultiply, -1200, y + 80)
        _connect(time, "", tm, "A")
        _connect(sp, "", tm, "B")
        pan = _add(mat, unreal.MaterialExpressionPanner, -1000, y)
        _connect(uv_s, "", pan, "Coordinate")
        _connect(tm, "", pan, "Time")
        s = _add(mat, unreal.MaterialExpressionTextureSample, -780, y)
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        _connect(pan, "", s, "UVs")
        return s

    s_bg = pan_sample(bg, 600, 0.005, 0.65)
    s1 = pan_sample(l1, 820, 0.009, 0.50)
    s2 = pan_sample(l2, 1040, 0.014, 0.55)
    s3 = pan_sample(l3, 1260, 0.006, 0.60)

    cloud_col = _const_color(mat, (1.12, 1.15, 1.22, 1.0), -200, 700)

    def alpha_of(sample, y):
        # density in R/A — avoid Max node (SM6 type issues in some builds)
        r = _add(mat, unreal.MaterialExpressionComponentMask, -560, y)
        r.set_editor_property("r", True)
        r.set_editor_property("g", False)
        r.set_editor_property("b", False)
        r.set_editor_property("a", False)
        _connect(sample, "", r, "")
        a = _add(mat, unreal.MaterialExpressionComponentMask, -560, y + 60)
        a.set_editor_property("r", False)
        a.set_editor_property("g", False)
        a.set_editor_property("b", False)
        a.set_editor_property("a", True)
        _connect(sample, "", a, "")
        combined = _add(mat, unreal.MaterialExpressionAdd, -380, y + 30)
        _connect(r, "", combined, "A")
        _connect(a, "", combined, "B")
        sat = _add(mat, unreal.MaterialExpressionSaturate, -200, y + 30)
        _connect(combined, "", sat, "")
        return sat

    a_bg_raw = alpha_of(s_bg, 620)
    a1 = alpha_of(s1, 840)
    a2 = alpha_of(s2, 1060)
    a3 = alpha_of(s3, 1280)

    bg_s = _add(mat, unreal.MaterialExpressionCollectionParameter, -200, 860)
    bg_s.set_editor_property("collection", mpc)
    bg_s.set_editor_property("parameter_name", "BGCloudsStrength")
    a_bg = _add(mat, unreal.MaterialExpressionMultiply, 0, 860)
    _connect(a_bg_raw, "", a_bg, "A")
    _connect(bg_s, "", a_bg, "B")
    a_bg_z = _add(mat, unreal.MaterialExpressionMultiply, 200, 880)
    _connect(a_bg, "", a_bg_z, "A")
    _connect(zb2, "", a_bg_z, "B")

    a12 = _add(mat, unreal.MaterialExpressionAdd, 0, 940)
    _connect(a1, "", a12, "A")
    _connect(a2, "", a12, "B")
    a123 = _add(mat, unreal.MaterialExpressionAdd, 200, 960)
    _connect(a12, "", a123, "A")
    _connect(a3, "", a123, "B")
    cstr = _add(mat, unreal.MaterialExpressionCollectionParameter, 0, 1040)
    cstr.set_editor_property("collection", mpc)
    cstr.set_editor_property("parameter_name", "CloudStrength")
    a_cloud = _add(mat, unreal.MaterialExpressionMultiply, 200, 1040)
    _connect(a123, "", a_cloud, "A")
    _connect(cstr, "", a_cloud, "B")
    a_cloud_z = _add(mat, unreal.MaterialExpressionMultiply, 400, 1060)
    _connect(a_cloud, "", a_cloud_z, "A")
    _connect(zb2, "", a_cloud_z, "B")
    a_cloud_s = _add(mat, unreal.MaterialExpressionSaturate, 600, 1060)
    _connect(a_cloud_z, "", a_cloud_s, "")

    lerp_bg = _add(mat, unreal.MaterialExpressionLinearInterpolate, 500, 120)
    _connect(sky_t, "", lerp_bg, "A")
    _connect(cloud_col, "", lerp_bg, "B")
    _connect(a_bg_z, "", lerp_bg, "Alpha")
    lerp = _add(mat, unreal.MaterialExpressionLinearInterpolate, 720, 140)
    _connect(lerp_bg, "", lerp, "A")
    _connect(cloud_col, "", lerp, "B")
    _connect(a_cloud_s, "", lerp, "Alpha")

    unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return mat


def build_clouds_mat(mpc, l1, l2, l3):
    """Extra translucent cloud shell (disable depth test so it always draws)."""
    path = f"{MAT_DST}/M_LobbyOriginalClouds"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyOriginalClouds", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    try:
        mat.set_editor_property("disable_depth_test", True)
    except Exception:
        pass

    time = _add(mat, unreal.MaterialExpressionTime, -1200, 0)
    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1200, -80)

    def layer(tex, y, spd, stretch, voff):
        st = _add(mat, unreal.MaterialExpressionConstant, -1000, y)
        st.set_editor_property("r", stretch)
        uv_s = _add(mat, unreal.MaterialExpressionMultiply, -820, y)
        _connect(uv, "", uv_s, "A")
        _connect(st, "", uv_s, "B")
        zc = _add(mat, unreal.MaterialExpressionConstant, -1000, y + 60)
        zc.set_editor_property("r", 0.0)
        vo = _add(mat, unreal.MaterialExpressionConstant, -1000, y + 120)
        vo.set_editor_property("r", voff)
        ap = _add(mat, unreal.MaterialExpressionAppendVector, -820, y + 80)
        _connect(zc, "", ap, "A")
        _connect(vo, "", ap, "B")
        uv_o = _add(mat, unreal.MaterialExpressionAdd, -640, y)
        _connect(uv_s, "", uv_o, "A")
        _connect(ap, "", uv_o, "B")
        sp = _add(mat, unreal.MaterialExpressionConstant, -1000, y + 180)
        sp.set_editor_property("r", spd)
        tm = _add(mat, unreal.MaterialExpressionMultiply, -820, y + 180)
        _connect(time, "", tm, "A")
        _connect(sp, "", tm, "B")
        pan = _add(mat, unreal.MaterialExpressionPanner, -460, y)
        _connect(uv_o, "", pan, "Coordinate")
        _connect(tm, "", pan, "Time")
        s = _add(mat, unreal.MaterialExpressionTextureSample, -240, y)
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        _connect(pan, "", s, "UVs")
        return s

    s1 = layer(l1, 0, 0.011, 0.40, 0.16)
    s2 = layer(l2, 280, 0.017, 0.46, -0.10)
    s3 = layer(l3, 560, 0.008, 0.52, -0.04)

    tint = _const_color(mat, (1.12, 1.14, 1.18, 1.0), 40, -120)
    c1 = _add(mat, unreal.MaterialExpressionMultiply, 200, 40)
    _connect(s1, "", c1, "A")
    _connect(tint, "", c1, "B")
    a = _add(mat, unreal.MaterialExpressionAdd, 200, 200)
    _connect(s1, "R", a, "A")
    _connect(s2, "R", a, "B")
    a2 = _add(mat, unreal.MaterialExpressionAdd, 380, 220)
    _connect(a, "", a2, "A")
    _connect(s3, "R", a2, "B")
    stren = _add(mat, unreal.MaterialExpressionCollectionParameter, 200, 320)
    stren.set_editor_property("collection", mpc)
    stren.set_editor_property("parameter_name", "CloudStrength")
    opac = _add(mat, unreal.MaterialExpressionMultiply, 560, 240)
    _connect(a2, "", opac, "A")
    _connect(stren, "", opac, "B")
    half = _add(mat, unreal.MaterialExpressionConstant, 560, 320)
    half.set_editor_property("r", 0.55)
    opac2 = _add(mat, unreal.MaterialExpressionMultiply, 720, 260)
    _connect(opac, "", opac2, "A")
    _connect(half, "", opac2, "B")
    opac_s = _add(mat, unreal.MaterialExpressionSaturate, 900, 260)
    _connect(opac2, "", opac_s, "")
    hl = _add(mat, unreal.MaterialExpressionConstant, 200, 400)
    hl.set_editor_property("r", 1.55)
    col = _add(mat, unreal.MaterialExpressionMultiply, 420, 40)
    _connect(c1, "", col, "A")
    _connect(hl, "", col, "B")

    unreal.MaterialEditingLibrary.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opac_s, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return mat


def build_sun_mat(mpc, sun_tex):
    path = f"{MAT_DST}/M_LobbyOriginalSun"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyOriginalSun", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -500, 0)
    s = _add(mat, unreal.MaterialExpressionTextureSample, -250, 0)
    s.set_editor_property("texture", sun_tex)
    s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(uv, "", s, "UVs")
    col = _const_color(mat, (1.0, 0.98, 0.92, 1.0), 0, -80)
    strength = _mpc_scalar(mat, mpc, "SunStrength", 0, 40)
    m1 = _add(mat, unreal.MaterialExpressionMultiply, 220, 0)
    _connect(s, "", m1, "A")
    _connect(col, "", m1, "B")
    m2 = _add(mat, unreal.MaterialExpressionMultiply, 420, 0)
    _connect(m1, "", m2, "A")
    _connect(strength, "", m2, "B")
    # Emissive only — BASE_COLOR + opacity on additive caused SM6 issues with broken MPC vectors
    unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    a_mask = _add(mat, unreal.MaterialExpressionComponentMask, 420, 80)
    a_mask.set_editor_property("r", False)
    a_mask.set_editor_property("g", False)
    a_mask.set_editor_property("b", False)
    a_mask.set_editor_property("a", True)
    _connect(s, "", a_mask, "")
    r_mask = _add(mat, unreal.MaterialExpressionComponentMask, 420, 160)
    r_mask.set_editor_property("r", True)
    r_mask.set_editor_property("g", False)
    r_mask.set_editor_property("b", False)
    r_mask.set_editor_property("a", False)
    _connect(s, "", r_mask, "")
    opac = _add(mat, unreal.MaterialExpressionAdd, 600, 120)
    _connect(a_mask, "", opac, "A")
    _connect(r_mask, "", opac, "B")
    opac_s = _add(mat, unreal.MaterialExpressionSaturate, 780, 120)
    _connect(opac, "", opac_s, "")
    unreal.MaterialEditingLibrary.connect_material_property(opac_s, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return mat


def _subsys():
    return unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)


def create_bp(sky_mat, cloud_mat, sun_mat):
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    plane = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")
    if unreal.EditorAssetLibrary.does_asset_exist(BP_PATH):
        unreal.EditorAssetLibrary.delete_asset(BP_PATH)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(BP_NAME, ROOT, unreal.Blueprint, factory)
    sub = _subsys()
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    root = sub.k2_gather_subobject_data_for_blueprint(bp)[0]

    def add(name, cls, parent=None):
        parent = parent or root
        params = unreal.AddNewSubobjectParams(parent_handle=parent, new_class=cls, blueprint_context=bp)
        try:
            h, fail = sub.add_new_subobject(params)
        except TypeError:
            h = sub.add_new_subobject(params)
            fail = ""
        if fail and str(fail) not in ("", "None"):
            unreal.log_warning(f"{name}: {fail}")
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

    h_incl, incl = add("OrbitInclinator", unreal.SceneComponent)
    h_ori, ori = add("OrbitOrientator", unreal.SceneComponent, h_incl)
    if incl:
        try:
            incl.set_editor_property("relative_rotation", unreal.Rotator(28.0, 0.0, 0.0))
        except Exception:
            pass
    h_rot, rot = add("DayCycleRotation", unreal.RotatingMovementComponent, h_ori)
    if rot:
        try:
            rot.set_editor_property("rotation_rate", unreal.Rotator(0.0, 0.0, 0.0))  # frozen midday
            try:
                rot.set_editor_property("rotation_in_local_space", True)
            except Exception:
                pass
            if ori:
                try:
                    rot.set_editor_property("updated_component", ori)
                except Exception:
                    pass
        except Exception as e:
            unreal.log(f"rot: {e}")

    _, sun = add("SM_Sun", unreal.StaticMeshComponent, h_ori)
    if sun:
        sun.set_static_mesh(sphere)
        sun.set_material(0, sun_mat)
        sun.set_editor_property("relative_location", unreal.Vector(42000.0, -8000.0, 12000.0))
        sun.set_editor_property("relative_scale3d", unreal.Vector(900.0, 900.0, 900.0))
        sun.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        sun.set_cast_shadow(False)

    _, dlight = add("SkyDirectionalLight", unreal.DirectionalLightComponent, h_ori)
    if dlight:
        dlight.set_editor_property("intensity", 10.0)
        dlight.set_editor_property("light_color", unreal.Color(255, 250, 243, 255))
        dlight.set_editor_property("relative_rotation", unreal.Rotator(-48.0, 35.0, 0.0))
        dlight.set_editor_property("atmosphere_sun_light", True)
        dlight.set_editor_property("specular_scale", 0.35)
        dlight.set_editor_property("indirect_lighting_intensity", 1.2)
        dlight.set_editor_property("light_source_angle", 1.5)
        dlight.set_editor_property("cast_shadows", True)
        dlight.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        dlight.set_editor_property("dynamic_shadow_distance_movable_light", 40000.0)

    _, sky = add("SkyDome", unreal.StaticMeshComponent)
    if sky:
        sky.set_static_mesh(sphere)
        sky.set_material(0, sky_mat)
        sky.set_editor_property("relative_scale3d", unreal.Vector(10000, 10000, 10000))
        sky.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        sky.set_cast_shadow(False)

    _, cld = add("SkyDomeClouds", unreal.StaticMeshComponent)
    if cld:
        cld.set_static_mesh(sphere)
        cld.set_material(0, cloud_mat)
        cld.set_editor_property("relative_scale3d", unreal.Vector(9500, 9500, 9500))
        cld.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        cld.set_cast_shadow(False)

    _, slight = add("SkyLight", unreal.SkyLightComponent)
    if slight:
        hdr_paths = [
            "/Game/Developers/TaitoP/Environment/Sky/Owned/Environment/Sky/Textures/T_Skylight_HDR",
            "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Textures/T_Skylight_HDR",
        ]
        hdr = None
        for p in hdr_paths:
            if unreal.EditorAssetLibrary.does_asset_exist(p):
                hdr = unreal.EditorAssetLibrary.load_asset(p)
                break
        slight.set_editor_property("intensity", 1.15)
        slight.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        if hdr:
            try:
                slight.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
                slight.set_editor_property("cubemap", hdr)
            except Exception:
                pass
        else:
            try:
                slight.set_editor_property("real_time_capture", True)
                slight.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
            except Exception:
                pass
        slight.set_editor_property("lower_hemisphere_is_black", False)
        slight.set_editor_property(
            "lower_hemisphere_color", unreal.LinearColor(0.35, 0.55, 0.85, 1.0)
        )

    _, fog = add("ExponentialHeightFog", unreal.ExponentialHeightFogComponent)
    if fog:
        fog.set_editor_property("fog_density", 0.012)
        fog.set_editor_property("fog_height_falloff", 0.28)
        fog.set_editor_property("fog_max_opacity", 0.08)
        try:
            fog.set_editor_property("enable_volumetric_fog", True)
            fog.set_editor_property("volumetric_fog_scattering_distribution", 0.2)
        except Exception:
            pass

    _, pp = add("PostProcess", unreal.PostProcessComponent)
    if pp:
        try:
            s = pp.get_editor_property("settings")
            s.set_editor_property("override_bloom_intensity", True)
            s.set_editor_property("bloom_intensity", 5.5)
            s.set_editor_property("override_bloom_threshold", True)
            s.set_editor_property("bloom_threshold", 0.35)
            s.set_editor_property("override_auto_exposure_bias", True)
            s.set_editor_property("auto_exposure_bias", 1.0)
            s.set_editor_property("override_auto_exposure_min_brightness", True)
            s.set_editor_property("auto_exposure_min_brightness", 0.85)
            s.set_editor_property("override_auto_exposure_max_brightness", True)
            s.set_editor_property("auto_exposure_max_brightness", 2.0)
            pp.set_editor_property("settings", s)
        except Exception as e:
            unreal.log(f"pp: {e}")

    # Orient sun to midday-ish: rotate orientator
    if ori:
        try:
            ori.set_editor_property("relative_rotation", unreal.Rotator(0.0, 40.0, 0.0))
        except Exception:
            pass

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)
    return bp


def place(bp):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        path = a.get_path_name()
        if (
            lab in ("LobbyStylizedSky", "LobbyOriginalSky")
            or "BP_StylizedSky" in cls
            or "BP_LobbyStylizedSky" in cls
            or "BP_LobbyOriginalSky" in cls
            or "SoStylized" in path
            or "/Owned/" in path
        ):
            eas.destroy_actor(a)
            unreal.log(f"removed {lab}|{cls}")
            continue
        if any(k in lab for k in ("PastelSun", "PastelFill", "PastelSkyLight", "PastelFog", "PastelSkyAtmosphere", "PastelPPV")) or cls == "SkyAtmosphere":
            eas.destroy_actor(a)
            unreal.log(f"removed pastel {lab}|{cls}")
            continue

    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("LobbyOriginalSky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    try:
        sl = actor.get_component_by_class(unreal.SkyLightComponent)
        if sl:
            sl.recapture_sky()
    except Exception:
        pass
    for c in actor.get_components_by_class(unreal.PrimitiveComponent):
        try:
            c.set_visibility(True)
            c.set_hidden_in_game(False)
        except Exception:
            pass
    try:
        unreal.SystemLibrary.execute_console_command(None, "r.BloomQuality 5")
    except Exception:
        pass
    # straighten viewport
    try:
        ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        loc, rot = ues.get_level_viewport_camera_info()
        ues.set_level_viewport_camera_info(loc, unreal.Rotator(pitch=-8.0, yaw=float(rot.yaw), roll=0.0))
    except Exception:
        pass
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"placed {actor.get_path_name()}")


def assert_clean(paths):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    for prop, val in (
        ("include_soft_package_references", True),
        ("include_hard_package_references", True),
    ):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    leaks = []
    for p in paths:
        try:
            deps = ar.get_dependencies(unreal.Name(p), opts) or []
        except Exception:
            continue
        for d in deps:
            s = str(d)
            if any(f in s for f in FORBIDDEN):
                leaks.append(f"{p} -> {s}")
            else:
                unreal.log(f"DEP {p} -> {s}")
    if leaks:
        for L in leaks:
            unreal.log_warning(L)
        raise RuntimeError(f"forbidden deps: {len(leaks)}")
    unreal.log("SHIP_SAFE_DEPS_OK")


def run():
    ensure_dir(ROOT)
    ensure_dir(TEX_DST)
    ensure_dir(MAT_DST)
    tex = import_all()
    mpc = create_mpc()
    sky_mat = build_sky_mat(
        mpc,
        tex["T_LobbySky_CurveAtlas"],
        tex["T_LobbySky_HeightGradient"],
        tex["T_LobbyBGClouds"],
        tex["T_LobbyCloud_L1"],
        tex["T_LobbyCloud_L2"],
        tex["T_LobbyCloud_L3"],
    )
    cloud_mat = build_clouds_mat(mpc, tex["T_LobbyCloud_L1"], tex["T_LobbyCloud_L2"], tex["T_LobbyCloud_L3"])
    sun_mat = build_sun_mat(mpc, tex["T_LobbySun_Body"])
    bp = create_bp(sky_mat, cloud_mat, sun_mat)
    place(bp)
    assert_clean(
        [
            BP_PATH,
            f"{MAT_DST}/M_LobbyOriginalSky",
            f"{MAT_DST}/M_LobbyOriginalClouds",
            f"{MAT_DST}/M_LobbyOriginalSun",
            MPC_PATH,
        ]
    )
    unreal.log("SHIP_SAFE_ORIGINAL_SKY_OK")


if __name__ == "__main__":
    run()
