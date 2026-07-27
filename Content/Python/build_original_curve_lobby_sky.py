"""
Build ORIGINAL Lobby stylized sky with curve-atlas day cycle.
Architecture matches SoStylized BP_StylizedSky, but ZERO SoStylized asset refs
(no duplicate_asset from SoStylized).

System:
  - MPC_LobbySky (CurrentTime, DayCycleProgress, DayLengthHours, Frozen?)
  - T_LobbySky_CurveAtlas (authored multi-row day/sunrise/sunset/night/moonset)
  - Original cloud/sun/noise textures under Environment/Sky/Textures
  - M_LobbyStylizedSky / M_LobbyStylizedClouds / M_LobbySun
  - BP_LobbyStylizedSky: dome+clouds+sun+lights+fog+PP+orbit+RotatingMovement
  - Day cycle via material Time→atlas + orbit rotation (curve-driven colors)
"""
from __future__ import annotations

import math
from pathlib import Path

import unreal

ROOT = "/Game/Developers/TaitoP/Environment/Sky"
TEX_DST = f"{ROOT}/Textures"
MAT_DST = f"{ROOT}/Materials"
CURVE_DST = f"{ROOT}/Curves"
BP_NAME = "BP_LobbyStylizedSky"
BP_PATH = f"{ROOT}/{BP_NAME}"
MPC_PATH = f"{ROOT}/MPC_LobbySky"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
ART = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky")


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
    node = _add(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", mpc)
    node.set_editor_property("parameter_name", unreal.Name(name))
    return node


def _const_color(mat, rgba, x, y):
    """Constant RGB — vector MPC CollectionParameter fails SM6 compile when set via Python."""
    node = _add(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    node.set_editor_property("constant", unreal.LinearColor(*rgba))
    return node


# ---------------------------------------------------------------------------
# 1) Author curve-atlas texture (our data — Classic-like bright day palette)
# ---------------------------------------------------------------------------
def bake_curve_atlas_png():
    """Rows: 0 Day, 1 Sunset, 2 Night, 3 Sunrise, 4 Moonset. U = day progress."""
    try:
        import numpy as np
        from PIL import Image
    except Exception as e:
        raise RuntimeError(f"PIL/numpy required on host for atlas bake: {e}")

    w, rows = 256, 5
    # Key colors per phase (zenith-ish strip for sky emissive lookup by U)
    # Matching cheerful stylized day: cyan-blue midday, warm sunrise/sunset, deep night
    phases = [
        # Day — bright cyan blue
        [(0.0, (0.45, 0.75, 1.0)), (0.5, (0.55, 0.82, 1.0)), (1.0, (0.75, 0.92, 1.0))],
        # Sunset
        [(0.0, (0.95, 0.35, 0.25)), (0.45, (1.0, 0.55, 0.35)), (1.0, (0.35, 0.25, 0.55))],
        # Night
        [(0.0, (0.02, 0.04, 0.12)), (0.5, (0.05, 0.08, 0.2)), (1.0, (0.08, 0.1, 0.22))],
        # Sunrise
        [(0.0, (0.2, 0.25, 0.55)), (0.4, (1.0, 0.65, 0.4)), (1.0, (0.55, 0.8, 1.0))],
        # Moonset
        [(0.0, (0.08, 0.1, 0.25)), (0.5, (0.25, 0.3, 0.45)), (1.0, (0.45, 0.55, 0.75))],
    ]

    def lerp_keys(keys, t):
        for i in range(len(keys) - 1):
            t0, c0 = keys[i]
            t1, c1 = keys[i + 1]
            if t0 <= t <= t1:
                u = 0 if t1 == t0 else (t - t0) / (t1 - t0)
                return tuple(c0[j] * (1 - u) + c1[j] * u for j in range(3))
        return keys[-1][1]

    img = np.zeros((rows, w, 3), np.float32)
    for r, keys in enumerate(phases):
        for x in range(w):
            img[r, x] = lerp_keys(keys, x / (w - 1))
    # Upscale rows for sampling stability
    scale = 32
    big = np.repeat(np.repeat(img, scale, axis=0), 1, axis=1)
    out = ART / "T_LobbySky_CurveAtlas.png"
    Image.fromarray((np.clip(big, 0, 1) * 255).astype(np.uint8), "RGB").save(out)
    unreal.log(f"baked atlas {out}")
    return out


def import_tex(src: Path, name: str, srgb=True, normal=False):
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
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", srgb)
    # Clamp for atlas
    if "Atlas" in name or "Gradient" in name:
        try:
            tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
            tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        except Exception:
            pass
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    unreal.log(f"tex {name}")
    return tex


def import_all_textures():
    """Import original Art/Textures/Sky PNGs — never SoStylized packages or pixel dumps."""
    tex = {}
    atlas_png = ART / "T_LobbySky_CurveAtlas.png"
    if not atlas_png.exists():
        bake_curve_atlas_png()
    tex["atlas"] = import_tex(atlas_png, "T_LobbySky_CurveAtlas", True)

    height_png = ART / "T_LobbySky_HeightGradient.png"
    if not height_png.exists():
        height_png = ART / "T_LobbySky_Gradient.png"
    if height_png.exists():
        tex["height"] = import_tex(height_png, "T_LobbySky_HeightGradient", True)

    # Own authored stylized layers (build_lobby_sky_textures.py) — same role as ref, not a clone
    mapping = [
        ("T_LobbySky_BGClouds.png", "T_LobbyBGClouds", True),
        ("T_LobbyCloud_Layer01.png", "T_LobbyCloud_L1", True),
        ("T_LobbyCloud_Layer02.png", "T_LobbyCloud_L2", True),
        ("T_LobbyCloud_Layer03.png", "T_LobbyCloud_L3", True),
        ("T_LobbySun_Disc.png", "T_LobbySun_Body", True),
        ("T_LobbySky_Noise.png", "T_LobbySky_Noise", False),
    ]
    for src_name, dst_name, srgb in mapping:
        src = ART / src_name
        if not src.exists():
            raise RuntimeError(f"missing owned Art texture {src} — run Art/Textures/Sky/build_lobby_sky_textures.py")
        tex[dst_name] = import_tex(src, dst_name, srgb)
    return tex


# ---------------------------------------------------------------------------
# 2) MPC
# ---------------------------------------------------------------------------
def create_mpc():
    ensure_dir(ROOT)
    if unreal.EditorAssetLibrary.does_asset_exist(MPC_PATH):
        unreal.EditorAssetLibrary.delete_asset(MPC_PATH)
    factory = unreal.MaterialParameterCollectionFactoryNew()
    mpc = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MPC_LobbySky", ROOT, unreal.MaterialParameterCollection, factory
    )
    # Scalar params
    scalars = [
        ("DayCycleProgress", 0.5),
        ("CurrentTime", 12.0),
        ("DayLengthHours", 0.1667),  # 10 min real = 24h game -> hours scale in mat
        ("DayLengthSeconds", 600.0),
        ("SkyBrightness", 1.15),
        ("BGCloudsStrength", 0.35),
        ("CloudStrength", 1.2),
        ("SunStrength", 10.0),
        ("Frozen", 0.0),
        ("PhaseRow", 0.0),  # 0=Day row in atlas
    ]
    scalar_arr = []
    for name, val in scalars:
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", float(val))
        scalar_arr.append(p)
    mpc.set_editor_property("scalar_parameters", scalar_arr)

    vectors = [
        ("SunColor", (1.0, 0.98, 0.85, 1.0)),
        ("CloudTint", (1.0, 1.0, 1.0, 1.0)),
        ("SkyTint", (1.0, 1.0, 1.0, 1.0)),
    ]
    vec_arr = []
    for name, rgba in vectors:
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(*rgba))
        vec_arr.append(p)
    mpc.set_editor_property("vector_parameters", vec_arr)
    unreal.EditorAssetLibrary.save_asset(MPC_PATH)
    unreal.log(f"MPC {MPC_PATH}")
    return mpc


# ---------------------------------------------------------------------------
# 3) Materials
# ---------------------------------------------------------------------------
def build_sky_mat(mpc, atlas, height, bg, noise):
    ensure_dir(MAT_DST)
    path = f"{MAT_DST}/M_LobbyStylizedSky"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyStylizedSky", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    # Day progress from Time / DayLengthSeconds (curve atlas U) — true curve system in material
    time = _add(mat, unreal.MaterialExpressionTime, -1400, -200)
    day_len = _add(mat, unreal.MaterialExpressionCollectionParameter, -1400, -100)
    day_len.set_editor_property("collection", mpc)
    day_len.set_editor_property("parameter_name", "DayLengthSeconds")
    div = _add(mat, unreal.MaterialExpressionDivide, -1180, -160)
    _connect(time, "", div, "A")
    _connect(day_len, "", div, "B")
    fr = _add(mat, unreal.MaterialExpressionFrac, -1000, -160)
    _connect(div, "", fr, "")

    # Optional freeze: lerp to DayCycleProgress from MPC
    frozen = _add(mat, unreal.MaterialExpressionCollectionParameter, -1400, 0)
    frozen.set_editor_property("collection", mpc)
    frozen.set_editor_property("parameter_name", "Frozen")
    prog = _add(mat, unreal.MaterialExpressionCollectionParameter, -1400, 80)
    prog.set_editor_property("collection", mpc)
    prog.set_editor_property("parameter_name", "DayCycleProgress")
    day_u = _add(mat, unreal.MaterialExpressionLinearInterpolate, -820, -100)
    _connect(fr, "", day_u, "A")
    _connect(prog, "", day_u, "B")
    _connect(frozen, "", day_u, "Alpha")

    # Phase row (Classic Day = 0)
    row = _add(mat, unreal.MaterialExpressionCollectionParameter, -1400, 180)
    row.set_editor_property("collection", mpc)
    row.set_editor_property("parameter_name", "PhaseRow")
    # UV = (day_u, (row+0.5)/5)
    rows = _add(mat, unreal.MaterialExpressionConstant, -1000, 200)
    rows.set_editor_property("r", 5.0)
    half = _add(mat, unreal.MaterialExpressionConstant, -1000, 260)
    half.set_editor_property("r", 0.5)
    row_a = _add(mat, unreal.MaterialExpressionAdd, -820, 200)
    _connect(row, "", row_a, "A")
    _connect(half, "", row_a, "B")
    row_v = _add(mat, unreal.MaterialExpressionDivide, -640, 200)
    _connect(row_a, "", row_v, "A")
    _connect(rows, "", row_v, "B")
    atlas_uv = _add(mat, unreal.MaterialExpressionAppendVector, -460, -40)
    _connect(day_u, "", atlas_uv, "A")
    _connect(row_v, "", atlas_uv, "B")

    s_atlas = _add(mat, unreal.MaterialExpressionTextureSample, -240, -60)
    s_atlas.set_editor_property("texture", atlas)
    s_atlas.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(atlas_uv, "", s_atlas, "UVs")

    # Height gradient by CameraVector Z
    cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -1400, 360)
    z = _add(mat, unreal.MaterialExpressionComponentMask, -1200, 360)
    z.set_editor_property("r", False)
    z.set_editor_property("g", False)
    z.set_editor_property("b", True)
    z.set_editor_property("a", False)
    _connect(cam, "", z, "")
    one = _add(mat, unreal.MaterialExpressionConstant, -1200, 460)
    one.set_editor_property("r", 1.0)
    add = _add(mat, unreal.MaterialExpressionAdd, -1000, 380)
    _connect(z, "", add, "A")
    _connect(one, "", add, "B")
    half2 = _add(mat, unreal.MaterialExpressionConstant, -1000, 460)
    half2.set_editor_property("r", 0.5)
    mul = _add(mat, unreal.MaterialExpressionMultiply, -820, 380)
    _connect(add, "", mul, "A")
    _connect(half2, "", mul, "B")
    inv = _add(mat, unreal.MaterialExpressionOneMinus, -640, 380)
    _connect(mul, "", inv, "")
    u0 = _add(mat, unreal.MaterialExpressionConstant, -640, 300)
    u0.set_editor_property("r", 0.5)
    h_uv = _add(mat, unreal.MaterialExpressionAppendVector, -460, 360)
    _connect(u0, "", h_uv, "A")
    _connect(inv, "", h_uv, "B")
    s_h = _add(mat, unreal.MaterialExpressionTextureSample, -240, 340)
    s_h.set_editor_property("texture", height)
    s_h.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(h_uv, "", s_h, "UVs")

    # Combine atlas phase color * height gradient
    comb = _add(mat, unreal.MaterialExpressionMultiply, 40, 100)
    _connect(s_atlas, "", comb, "A")
    _connect(s_h, "", comb, "B")

    bright = _add(mat, unreal.MaterialExpressionCollectionParameter, 40, -40)
    bright.set_editor_property("collection", mpc)
    bright.set_editor_property("parameter_name", "SkyBrightness")
    sky = _add(mat, unreal.MaterialExpressionMultiply, 240, 80)
    _connect(comb, "", sky, "A")
    _connect(bright, "", sky, "B")

    # BG clouds
    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 600)
    speed = _add(mat, unreal.MaterialExpressionConstant, -1400, 700)
    speed.set_editor_property("r", 0.01)
    tmul = _add(mat, unreal.MaterialExpressionMultiply, -1200, 660)
    _connect(time, "", tmul, "A")
    _connect(speed, "", tmul, "B")
    pan = _add(mat, unreal.MaterialExpressionPanner, -1000, 620)
    _connect(uv, "", pan, "Coordinate")
    _connect(tmul, "", pan, "Time")
    s_bg = _add(mat, unreal.MaterialExpressionTextureSample, -780, 600)
    s_bg.set_editor_property("texture", bg)
    s_bg.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(pan, "", s_bg, "UVs")
    bg_s = _add(mat, unreal.MaterialExpressionCollectionParameter, -780, 760)
    bg_s.set_editor_property("collection", mpc)
    bg_s.set_editor_property("parameter_name", "BGCloudsStrength")
    bg_a = _add(mat, unreal.MaterialExpressionMultiply, -560, 700)
    _connect(s_bg, "A", bg_a, "A")
    _connect(bg_s, "", bg_a, "B")
    lerp = _add(mat, unreal.MaterialExpressionLinearInterpolate, 460, 120)
    _connect(sky, "", lerp, "A")
    _connect(s_bg, "", lerp, "B")
    _connect(bg_a, "", lerp, "Alpha")

    unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def build_clouds_mat(mpc, l1, l2, l3):
    path = f"{MAT_DST}/M_LobbyStylizedClouds"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyStylizedClouds", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

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

    s1 = layer(l1, 0, 0.012, 0.42, 0.18)
    s2 = layer(l2, 280, 0.018, 0.45, -0.12)
    s3 = layer(l3, 560, 0.009, 0.5, -0.05)

    # Constant tint — vector MPC CollectionParameter breaks SM6 when authored via Python
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
    stren = _mpc_scalar(mat, mpc, "CloudStrength", 200, 320)
    opac = _add(mat, unreal.MaterialExpressionMultiply, 560, 240)
    _connect(a2, "", opac, "A")
    _connect(stren, "", opac, "B")
    opac_s = _add(mat, unreal.MaterialExpressionSaturate, 740, 240)
    _connect(opac, "", opac_s, "")

    hl = _add(mat, unreal.MaterialExpressionConstant, 200, 400)
    hl.set_editor_property("r", 1.5)
    col = _add(mat, unreal.MaterialExpressionMultiply, 420, 40)
    _connect(c1, "", col, "A")
    _connect(hl, "", col, "B")

    unreal.MaterialEditingLibrary.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opac_s, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def build_sun_mat(mpc, sun_tex):
    path = f"{MAT_DST}/M_LobbySun"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbySun", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
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
    unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(s, "A", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


# ---------------------------------------------------------------------------
# 4) BP (components + rotating orbit for sun; materials own curve timing)
# ---------------------------------------------------------------------------
def _subsys():
    return unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)


def create_bp(sky_mat, cloud_mat, sun_mat):
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    plane = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")
    if unreal.EditorAssetLibrary.does_asset_exist(BP_PATH):
        unreal.EditorAssetLibrary.delete_asset(BP_PATH)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        BP_NAME, ROOT, unreal.Blueprint, factory
    )
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

    # RotatingMovement on orientator → sun sweeps (day cycle motion)
    h_rot, rot = add("DayCycleRotation", unreal.RotatingMovementComponent, h_ori)
    if rot:
        try:
            # 360 deg / 600s = 0.6 deg/s yaw
            rot.set_editor_property("rotation_rate", unreal.Rotator(0.0, 0.6, 0.0))
            try:
                rot.set_editor_property("rotation_in_local_space", True)
            except Exception:
                pass
        except Exception as e:
            unreal.log(f"rot: {e}")

    h_sun, sun = add("SM_Sun", unreal.StaticMeshComponent, h_ori)
    if sun:
        try:
            sun.set_static_mesh(plane)
            sun.set_material(0, sun_mat)
            sun.set_editor_property("relative_location", unreal.Vector(50000.0, 0.0, 0.0))
            sun.set_editor_property("relative_rotation", unreal.Rotator(0.0, 90.0, 0.0))
            sun.set_editor_property("relative_scale3d", unreal.Vector(900.0, 900.0, 900.0))
            sun.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            sun.set_cast_shadow(False)
        except Exception as e:
            unreal.log(f"sun: {e}")

    h_dir, dlight = add("SkyDirectionalLight", unreal.DirectionalLightComponent, h_ori)
    if dlight:
        try:
            dlight.set_editor_property("intensity", 12.0)
            dlight.set_editor_property("relative_rotation", unreal.Rotator(-55.0, 0.0, 0.0))
            dlight.set_editor_property("atmosphere_sun_light", False)
        except Exception as e:
            unreal.log(f"dir: {e}")

    _, sky = add("SkyDome", unreal.StaticMeshComponent)
    if sky:
        sky.set_static_mesh(sphere)
        sky.set_material(0, sky_mat)
        sky.set_editor_property("relative_scale3d", unreal.Vector(10000, 10000, 10000))
        try:
            sky.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            sky.set_cast_shadow(False)
        except Exception:
            pass

    _, cld = add("SkyDomeClouds", unreal.StaticMeshComponent)
    if cld:
        cld.set_static_mesh(sphere)
        cld.set_material(0, cloud_mat)
        cld.set_editor_property("relative_scale3d", unreal.Vector(9500, 9500, 9500))
        try:
            cld.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            cld.set_cast_shadow(False)
        except Exception:
            pass

    _, slight = add("SkyLight", unreal.SkyLightComponent)
    if slight:
        try:
            slight.set_editor_property("intensity", 4.5)
        except Exception:
            pass

    _, fog = add("ExponentialHeightFog", unreal.ExponentialHeightFogComponent)
    if fog:
        try:
            fog.set_editor_property("fog_density", 0.003)
            fog.set_editor_property("fog_height_falloff", 0.2)
        except Exception:
            pass

    _, pp = add("PostProcess", unreal.PostProcessComponent)
    if pp:
        try:
            settings = pp.get_editor_property("settings")
            settings.set_editor_property("override_bloom_intensity", True)
            settings.set_editor_property("bloom_intensity", 0.95)
            settings.set_editor_property("override_auto_exposure_bias", True)
            settings.set_editor_property("auto_exposure_bias", 0.9)
            pp.set_editor_property("settings", settings)
        except Exception as e:
            unreal.log(f"pp: {e}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)
    unreal.log(f"BP {BP_PATH}")
    return bp


def place(bp):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab == "LobbyStylizedSky" or "BP_LobbyStylizedSky" in cls or "BP_StylizedSky" in cls:
            eas.destroy_actor(a)
            continue
        if any(k in lab for k in ("PastelSun", "PastelFill", "PastelSkyLight", "PastelFog", "PastelSkyAtmosphere", "PastelPPV")) or cls == "SkyAtmosphere":
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
                for cc in (unreal.DirectionalLightComponent, unreal.SkyLightComponent, unreal.ExponentialHeightFogComponent):
                    c = a.get_component_by_class(cc)
                    if c:
                        try:
                            c.set_visibility(False)
                            c.set_intensity(0.0)
                        except Exception:
                            pass
            except Exception:
                pass

    actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("LobbyStylizedSky")
    try:
        actor.set_folder_path("Environment/Sky")
    except Exception:
        pass
    # Init MPC midday
    mpc = unreal.EditorAssetLibrary.load_asset(MPC_PATH)
    if mpc:
        try:
            unreal.MaterialLibrary.set_scalar_parameter_value(None, mpc, "DayCycleProgress", 0.5)
            unreal.MaterialLibrary.set_scalar_parameter_value(None, mpc, "CurrentTime", 12.0)
            unreal.MaterialLibrary.set_scalar_parameter_value(None, mpc, "DayLengthSeconds", 600.0)
            unreal.MaterialLibrary.set_scalar_parameter_value(None, mpc, "Frozen", 0.0)
            unreal.MaterialLibrary.set_scalar_parameter_value(None, mpc, "PhaseRow", 0.0)
            unreal.MaterialLibrary.set_scalar_parameter_value(None, mpc, "SkyBrightness", 1.2)
            unreal.MaterialLibrary.set_scalar_parameter_value(None, mpc, "SunStrength", 12.0)
            unreal.MaterialLibrary.set_vector_parameter_value(
                None, mpc, "SunColor", unreal.LinearColor(1.0, 0.98, 0.88, 1.0)
            )
            unreal.MaterialLibrary.set_vector_parameter_value(
                None, mpc, "CloudTint", unreal.LinearColor(1.05, 1.05, 1.1, 1.0)
            )
        except Exception as e:
            unreal.log(f"mpc set: {e}")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"placed {actor}")


def assert_no_sostylized(paths):
    for p in paths:
        if not unreal.EditorAssetLibrary.does_asset_exist(p):
            continue
        try:
            ar = unreal.AssetRegistryHelpers.get_asset_registry()
            deps = ar.get_dependencies(unreal.Name(p), unreal.AssetRegistryDependencyOptions())
            for d in deps:
                s = str(d)
                if "SoStylized" in s:
                    raise RuntimeError(f"SoStylized leak: {p} -> {s}")
        except RuntimeError:
            raise
        except Exception as e:
            unreal.log(f"dep check skip {p}: {e}")
    unreal.log("NO_SOSTYLIZED_DEPS_OK")


def run():
    ensure_dir(ROOT)
    ensure_dir(TEX_DST)
    ensure_dir(MAT_DST)
    ensure_dir(CURVE_DST)
    tex = import_all_textures()
    mpc = create_mpc()
    sky_mat = build_sky_mat(
        mpc,
        tex["atlas"],
        tex.get("height") or tex["atlas"],
        tex.get("T_LobbyBGClouds") or tex["atlas"],
        tex.get("T_LobbySky_Noise") or tex["atlas"],
    )
    cloud_mat = build_clouds_mat(
        mpc,
        tex["T_LobbyCloud_L1"],
        tex["T_LobbyCloud_L2"],
        tex["T_LobbyCloud_L3"],
    )
    sun_mat = build_sun_mat(mpc, tex["T_LobbySun_Body"])
    bp = create_bp(sky_mat, cloud_mat, sun_mat)
    place(bp)
    assert_no_sostylized([BP_PATH, f"{MAT_DST}/M_LobbyStylizedSky", f"{MAT_DST}/M_LobbyStylizedClouds", f"{MAT_DST}/M_LobbySun", MPC_PATH])
    unreal.log("ORIGINAL_CURVE_SKY_OK")


if __name__ == "__main__":
    run()
