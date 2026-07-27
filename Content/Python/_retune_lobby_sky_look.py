"""Retune LobbyOriginalSky: less cloud washout, keep zenith blue."""
import unreal

MPC_PATH = "/Game/Developers/TaitoP/Environment/Sky/MPC_LobbySky"
MAT_SKY = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSky"
MAT_CLOUD = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalClouds"
ROOT = "/Game/Developers/TaitoP/Environment/Sky"
MAT_DST = f"{ROOT}/Materials"
TEX = f"{ROOT}/Textures"


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def _connect(a, ap, b, bp=""):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, ap, b, bp)


def retune_mpc():
    mpc = unreal.EditorAssetLibrary.load_asset(MPC_PATH)
    scalars = [
        ("DayCycleProgress", 0.48),
        ("DayLengthSeconds", 600.0),
        ("SkyBrightness", 1.2),
        ("BGCloudsStrength", 0.28),
        ("CloudStrength", 0.55),
        ("SunStrength", 12.0),
        ("Frozen", 1.0),
        ("PhaseRow", 0.0),
    ]
    arr = []
    for name, val in scalars:
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", float(val))
        arr.append(p)
    mpc.set_editor_property("scalar_parameters", arr)
    unreal.EditorAssetLibrary.save_loaded_asset(mpc)
    unreal.log("MPC retuned")
    return mpc


def rebuild_sky(mpc):
    atlas = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbySky_CurveAtlas")
    height = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbySky_HeightGradient")
    bg = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyBGClouds")
    l1 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L1")
    l2 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L2")
    l3 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L3")
    path = MAT_SKY
    mat = unreal.EditorAssetLibrary.load_asset(path)
    _clear(mat)
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
    # zenith_factor: 0 at zenith (look up), 1 near horizon — clouds fade at top
    zenith_band = _add(mat, unreal.MaterialExpressionSaturate, -640, 480)
    _connect(inv, "", zenith_band, "")
    # soft power via multiply twice ~ square
    zb2 = _add(mat, unreal.MaterialExpressionMultiply, -460, 500)
    _connect(zenith_band, "", zb2, "A")
    _connect(zenith_band, "", zb2, "B")

    u0 = _add(mat, unreal.MaterialExpressionConstant, -820, 300)
    u0.set_editor_property("r", 0.5)
    h_uv = _add(mat, unreal.MaterialExpressionAppendVector, -640, 360)
    _connect(u0, "", h_uv, "A")
    _connect(inv, "", h_uv, "B")
    s_h = _add(mat, unreal.MaterialExpressionTextureSample, -420, 340)
    s_h.set_editor_property("texture", height)
    s_h.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(h_uv, "", s_h, "UVs")

    comb = _add(mat, unreal.MaterialExpressionMultiply, -200, 100)
    _connect(s_atlas, "", comb, "A")
    _connect(s_h, "", comb, "B")
    bright = _add(mat, unreal.MaterialExpressionCollectionParameter, -200, -40)
    bright.set_editor_property("collection", mpc)
    bright.set_editor_property("parameter_name", "SkyBrightness")
    sky = _add(mat, unreal.MaterialExpressionMultiply, 0, 80)
    _connect(comb, "", sky, "A")
    _connect(bright, "", sky, "B")
    tint = _add(mat, unreal.MaterialExpressionCollectionParameter, 0, -40)
    tint.set_editor_property("collection", mpc)
    tint.set_editor_property("parameter_name", "SkyTint")
    sky_t = _add(mat, unreal.MaterialExpressionMultiply, 200, 60)
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

    s_bg = pan_sample(bg, 600, 0.006, 0.55)
    s1 = pan_sample(l1, 820, 0.010, 0.42)
    s2 = pan_sample(l2, 1040, 0.016, 0.48)
    s3 = pan_sample(l3, 1260, 0.007, 0.50)

    cloud_tint = _add(mat, unreal.MaterialExpressionCollectionParameter, -200, 700)
    cloud_tint.set_editor_property("collection", mpc)
    cloud_tint.set_editor_property("parameter_name", "CloudTint")
    white = _add(mat, unreal.MaterialExpressionConstant3Vector, -200, 780)
    white.set_editor_property("constant", unreal.LinearColor(1.02, 1.03, 1.06, 1.0))
    cloud_col = _add(mat, unreal.MaterialExpressionMultiply, 0, 740)
    _connect(white, "", cloud_col, "A")
    _connect(cloud_tint, "", cloud_col, "B")

    bg_s = _add(mat, unreal.MaterialExpressionCollectionParameter, -200, 860)
    bg_s.set_editor_property("collection", mpc)
    bg_s.set_editor_property("parameter_name", "BGCloudsStrength")
    a_bg = _add(mat, unreal.MaterialExpressionMultiply, 0, 860)
    _connect(s_bg, "A", a_bg, "A")
    _connect(bg_s, "", a_bg, "B")
    a_bg_z = _add(mat, unreal.MaterialExpressionMultiply, 200, 880)
    _connect(a_bg, "", a_bg_z, "A")
    _connect(zb2, "", a_bg_z, "B")

    a12 = _add(mat, unreal.MaterialExpressionAdd, 0, 940)
    _connect(s1, "A", a12, "A")
    _connect(s2, "A", a12, "B")
    a123 = _add(mat, unreal.MaterialExpressionAdd, 200, 960)
    _connect(a12, "", a123, "A")
    _connect(s3, "A", a123, "B")
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

    lerp_bg = _add(mat, unreal.MaterialExpressionLinearInterpolate, 420, 120)
    _connect(sky_t, "", lerp_bg, "A")
    _connect(cloud_col, "", lerp_bg, "B")
    _connect(a_bg_z, "", lerp_bg, "Alpha")
    lerp = _add(mat, unreal.MaterialExpressionLinearInterpolate, 640, 140)
    _connect(lerp_bg, "", lerp, "A")
    _connect(cloud_col, "", lerp, "B")
    _connect(a_cloud_s, "", lerp, "Alpha")

    unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log("sky mat rebuilt with zenith mask")


def soften_pp():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if (a.get_actor_label() or "") != "LobbyOriginalSky":
            continue
        pp = a.get_component_by_class(unreal.PostProcessComponent)
        if not pp:
            continue
        try:
            s = pp.get_editor_property("settings")
            s.set_editor_property("override_bloom_intensity", True)
            s.set_editor_property("bloom_intensity", 0.55)
            s.set_editor_property("override_bloom_threshold", True)
            s.set_editor_property("bloom_threshold", 1.0)
            s.set_editor_property("override_auto_exposure_bias", True)
            s.set_editor_property("auto_exposure_bias", 0.15)
            s.set_editor_property("override_auto_exposure_min_brightness", True)
            s.set_editor_property("auto_exposure_min_brightness", 0.6)
            s.set_editor_property("override_auto_exposure_max_brightness", True)
            s.set_editor_property("auto_exposure_max_brightness", 1.4)
            pp.set_editor_property("settings", s)
        except Exception as e:
            unreal.log(f"pp: {e}")
        sl = a.get_component_by_class(unreal.SkyLightComponent)
        if sl:
            try:
                sl.recapture_sky()
            except Exception:
                pass


mpc = retune_mpc()
rebuild_sky(mpc)
soften_pp()
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("RETUNE_OK")
