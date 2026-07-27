"""Bisect M_LobbyOriginalSky / Sun SM6 failures by building progressive variants."""
import unreal

MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials"
TEX = "/Game/Developers/TaitoP/Environment/Sky/Textures"
MPC = unreal.EditorAssetLibrary.load_asset("/Game/Developers/TaitoP/Environment/Sky/MPC_LobbySky")
atlas = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbySky_CurveAtlas")
height = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbySky_HeightGradient")
bg = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyBGClouds")
l1 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L1")
sun = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbySun_Body")


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def _c(a, ap, b, bp=""):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, ap, b, bp)


def _mpc(mat, name, x, y):
    n = _add(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    n.set_editor_property("collection", MPC)
    n.set_editor_property("parameter_name", unreal.Name(name))
    return n


def make(name):
    path = f"{MAT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, MAT, unreal.Material, unreal.MaterialFactoryNew()
        )
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    return mat, path


# --- A: stylized-like atlas+height only (known good pattern) ---
mat, path = make("M_Bisect_A_AtlasHeight")
time = _add(mat, unreal.MaterialExpressionTime, -1400, -200)
day_len = _mpc(mat, "DayLengthSeconds", -1400, -100)
div = _add(mat, unreal.MaterialExpressionDivide, -1180, -160)
_c(time, "", div, "A"); _c(day_len, "", div, "B")
fr = _add(mat, unreal.MaterialExpressionFrac, -1000, -160)
_c(div, "", fr, "")
frozen = _mpc(mat, "Frozen", -1400, 0)
prog = _mpc(mat, "DayCycleProgress", -1400, 80)
day_u = _add(mat, unreal.MaterialExpressionLinearInterpolate, -820, -100)
_c(fr, "", day_u, "A"); _c(prog, "", day_u, "B"); _c(frozen, "", day_u, "Alpha")
row = _mpc(mat, "PhaseRow", -1400, 180)
rows = _add(mat, unreal.MaterialExpressionConstant, -1000, 200); rows.set_editor_property("r", 5.0)
half = _add(mat, unreal.MaterialExpressionConstant, -1000, 260); half.set_editor_property("r", 0.5)
row_a = _add(mat, unreal.MaterialExpressionAdd, -820, 200)
_c(row, "", row_a, "A"); _c(half, "", row_a, "B")
row_v = _add(mat, unreal.MaterialExpressionDivide, -640, 200)
_c(row_a, "", row_v, "A"); _c(rows, "", row_v, "B")
atlas_uv = _add(mat, unreal.MaterialExpressionAppendVector, -460, -40)
_c(day_u, "", atlas_uv, "A"); _c(row_v, "", atlas_uv, "B")
s_atlas = _add(mat, unreal.MaterialExpressionTextureSample, -240, -60)
s_atlas.set_editor_property("texture", atlas)
_c(atlas_uv, "", s_atlas, "UVs")
cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -1400, 360)
z = _add(mat, unreal.MaterialExpressionComponentMask, -1200, 360)
z.set_editor_property("r", False); z.set_editor_property("g", False)
z.set_editor_property("b", True); z.set_editor_property("a", False)
_c(cam, "", z, "")
one = _add(mat, unreal.MaterialExpressionConstant, -1400, 460); one.set_editor_property("r", 1.0)
add = _add(mat, unreal.MaterialExpressionAdd, -1200, 380)
_c(z, "", add, "A"); _c(one, "", add, "B")
half2 = _add(mat, unreal.MaterialExpressionConstant, -1200, 460); half2.set_editor_property("r", 0.5)
mul = _add(mat, unreal.MaterialExpressionMultiply, -1000, 380)
_c(add, "", mul, "A"); _c(half2, "", mul, "B")
inv = _add(mat, unreal.MaterialExpressionOneMinus, -820, 380)
_c(mul, "", inv, "")
u0 = _add(mat, unreal.MaterialExpressionConstant, -820, 300); u0.set_editor_property("r", 0.5)
h_uv = _add(mat, unreal.MaterialExpressionAppendVector, -640, 360)
_c(u0, "", h_uv, "A"); _c(inv, "", h_uv, "B")
s_h = _add(mat, unreal.MaterialExpressionTextureSample, -420, 340)
s_h.set_editor_property("texture", height)
_c(h_uv, "", s_h, "UVs")
atlas_w = _add(mat, unreal.MaterialExpressionConstant, -200, 40); atlas_w.set_editor_property("r", 0.35)
atlas_mix = _add(mat, unreal.MaterialExpressionLinearInterpolate, -40, 80)
_c(s_h, "", atlas_mix, "A"); _c(s_atlas, "", atlas_mix, "B"); _c(atlas_w, "", atlas_mix, "Alpha")
bright = _mpc(mat, "SkyBrightness", -200, -40)
sky = _add(mat, unreal.MaterialExpressionMultiply, 140, 60)
_c(atlas_mix, "", sky, "A"); _c(bright, "", sky, "B")
tint = _add(mat, unreal.MaterialExpressionConstant3Vector, 140, -40)
tint.set_editor_property("constant", unreal.LinearColor(0.95, 1.06, 1.18, 1.0))
sky_t = _add(mat, unreal.MaterialExpressionMultiply, 320, 40)
_c(sky, "", sky_t, "A"); _c(tint, "", sky_t, "B")
unreal.MaterialEditingLibrary.connect_material_property(sky_t, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(path)
unreal.log("BISECT_A_DONE")

# --- B: A + one panner cloud ---
mat, path = make("M_Bisect_B_OnePanner")
# reuse simplified: just emissive = panned bg * strength
time = _add(mat, unreal.MaterialExpressionTime, -800, 0)
uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -800, 80)
sp = _add(mat, unreal.MaterialExpressionConstant, -600, 0); sp.set_editor_property("r", 0.01)
tm = _add(mat, unreal.MaterialExpressionMultiply, -400, 0)
_c(time, "", tm, "A"); _c(sp, "", tm, "B")
pan = _add(mat, unreal.MaterialExpressionPanner, -200, 40)
_c(uv, "", pan, "Coordinate"); _c(tm, "", pan, "Time")
s = _add(mat, unreal.MaterialExpressionTextureSample, 0, 40)
s.set_editor_property("texture", bg)
_c(pan, "", s, "UVs")
unreal.MaterialEditingLibrary.connect_material_property(s, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(path)
unreal.log("BISECT_B_DONE")

# --- C: sun emissive-only like working test ---
path = f"{MAT}/M_Bisect_C_SunEmissive"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    mat = unreal.EditorAssetLibrary.load_asset(path); _clear(mat)
else:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Bisect_C_SunEmissive", MAT, unreal.Material, unreal.MaterialFactoryNew()
    )
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -400, 0)
s = _add(mat, unreal.MaterialExpressionTextureSample, -200, 0)
s.set_editor_property("texture", sun)
_c(uv, "", s, "UVs")
col = _add(mat, unreal.MaterialExpressionConstant3Vector, 0, -80)
col.set_editor_property("constant", unreal.LinearColor(1, 0.98, 0.92, 1))
stren = _mpc(mat, "SunStrength", 0, 40)
m1 = _add(mat, unreal.MaterialExpressionMultiply, 200, 0)
_c(s, "", m1, "A"); _c(col, "", m1, "B")
m2 = _add(mat, unreal.MaterialExpressionMultiply, 400, 0)
_c(m1, "", m2, "A"); _c(stren, "", m2, "B")
unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(path)
unreal.log("BISECT_C_DONE")

# --- D: sun with opacity like OriginalSun ---
path = f"{MAT}/M_Bisect_D_SunOpacity"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    mat = unreal.EditorAssetLibrary.load_asset(path); _clear(mat)
else:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Bisect_D_SunOpacity", MAT, unreal.Material, unreal.MaterialFactoryNew()
    )
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -400, 0)
s = _add(mat, unreal.MaterialExpressionTextureSample, -200, 0)
s.set_editor_property("texture", sun)
_c(uv, "", s, "UVs")
col = _add(mat, unreal.MaterialExpressionConstant3Vector, 0, -80)
col.set_editor_property("constant", unreal.LinearColor(1, 0.98, 0.92, 1))
stren = _mpc(mat, "SunStrength", 0, 40)
m1 = _add(mat, unreal.MaterialExpressionMultiply, 200, 0)
_c(s, "", m1, "A"); _c(col, "", m1, "B")
m2 = _add(mat, unreal.MaterialExpressionMultiply, 400, 0)
_c(m1, "", m2, "A"); _c(stren, "", m2, "B")
unreal.MaterialEditingLibrary.connect_material_property(m2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
a = _add(mat, unreal.MaterialExpressionComponentMask, 400, 80)
a.set_editor_property("r", False); a.set_editor_property("g", False)
a.set_editor_property("b", False); a.set_editor_property("a", True)
_c(s, "", a, "")
r = _add(mat, unreal.MaterialExpressionComponentMask, 400, 160)
r.set_editor_property("r", True); r.set_editor_property("g", False)
r.set_editor_property("b", False); r.set_editor_property("a", False)
_c(s, "", r, "")
op = _add(mat, unreal.MaterialExpressionAdd, 600, 120)
_c(a, "", op, "A"); _c(r, "", op, "B")
sat = _add(mat, unreal.MaterialExpressionSaturate, 780, 120)
_c(op, "", sat, "")
unreal.MaterialEditingLibrary.connect_material_property(sat, "", unreal.MaterialProperty.MP_OPACITY)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(path)
unreal.log("BISECT_D_DONE")

# --- E: A + BASE_COLOR also connected ---
mat, path = make("M_Bisect_E_BaseAndEmissive")
c = _add(mat, unreal.MaterialExpressionConstant3Vector, 0, 0)
c.set_editor_property("constant", unreal.LinearColor(0.4, 0.7, 1.0, 1.0))
unreal.MaterialEditingLibrary.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(path)
unreal.log("BISECT_E_DONE")

unreal.log("BISECT_ALL_DONE")
