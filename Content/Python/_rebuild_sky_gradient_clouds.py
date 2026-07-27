"""Rebuild M_LobbyOriginalSky: proven gradient + soft white cloud composite from varied refs."""
import unreal

MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSky"
MPC = "/Game/Developers/TaitoP/Environment/Sky/MPC_LobbySky"
TEX = "/Game/Developers/TaitoP/Environment/Sky/Textures"


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def _connect(a, ap, b, bp=""):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, ap, b, bp)


mat = unreal.EditorAssetLibrary.load_asset(MAT)
mpc = unreal.EditorAssetLibrary.load_asset(MPC)
height = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbySky_HeightGradient")
bg = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyBGClouds")
l1 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L1")
l2 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L2")
l3 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L3")

_clear(mat)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)

# --- height gradient via CameraVector Z ---
cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -1400, 0)
z = _add(mat, unreal.MaterialExpressionComponentMask, -1200, 0)
z.set_editor_property("r", False)
z.set_editor_property("g", False)
z.set_editor_property("b", True)
z.set_editor_property("a", False)
_connect(cam, "", z, "")
one = _add(mat, unreal.MaterialExpressionConstant, -1200, 100)
one.set_editor_property("r", 1.0)
add = _add(mat, unreal.MaterialExpressionAdd, -1000, 20)
_connect(z, "", add, "A")
_connect(one, "", add, "B")
half = _add(mat, unreal.MaterialExpressionConstant, -1000, 100)
half.set_editor_property("r", 0.5)
mul = _add(mat, unreal.MaterialExpressionMultiply, -820, 20)
_connect(add, "", mul, "A")
_connect(half, "", mul, "B")
inv = _add(mat, unreal.MaterialExpressionOneMinus, -640, 20)
_connect(mul, "", inv, "")
u0 = _add(mat, unreal.MaterialExpressionConstant, -640, -60)
u0.set_editor_property("r", 0.5)
huv = _add(mat, unreal.MaterialExpressionAppendVector, -460, 0)
_connect(u0, "", huv, "A")
_connect(inv, "", huv, "B")
s_h = _add(mat, unreal.MaterialExpressionTextureSample, -260, 0)
s_h.set_editor_property("texture", height)
s_h.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
_connect(huv, "", s_h, "UVs")

bright = _add(mat, unreal.MaterialExpressionCollectionParameter, -260, -120)
bright.set_editor_property("collection", mpc)
bright.set_editor_property("parameter_name", "SkyBrightness")
sky = _add(mat, unreal.MaterialExpressionMultiply, -40, -40)
_connect(s_h, "", sky, "A")
_connect(bright, "", sky, "B")

# horizon cloud band weight = inv^2
zb = _add(mat, unreal.MaterialExpressionSaturate, -640, 160)
_connect(inv, "", zb, "")
zb2 = _add(mat, unreal.MaterialExpressionMultiply, -460, 180)
_connect(zb, "", zb2, "A")
_connect(zb, "", zb2, "B")

time = _add(mat, unreal.MaterialExpressionTime, -1400, 400)
uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 300)


def pan(tex, y, spd, scale):
    st = _add(mat, unreal.MaterialExpressionConstant, -1200, y)
    st.set_editor_property("r", scale)
    uvs = _add(mat, unreal.MaterialExpressionMultiply, -1000, y)
    _connect(uv, "", uvs, "A")
    _connect(st, "", uvs, "B")
    sp = _add(mat, unreal.MaterialExpressionConstant, -1200, y + 70)
    sp.set_editor_property("r", spd)
    tm = _add(mat, unreal.MaterialExpressionMultiply, -1000, y + 70)
    _connect(time, "", tm, "A")
    _connect(sp, "", tm, "B")
    pan_n = _add(mat, unreal.MaterialExpressionPanner, -800, y)
    _connect(uvs, "", pan_n, "Coordinate")
    _connect(tm, "", pan_n, "Time")
    s = _add(mat, unreal.MaterialExpressionTextureSample, -580, y)
    s.set_editor_property("texture", tex)
    s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(pan_n, "", s, "UVs")
    mx = _add(mat, unreal.MaterialExpressionMax, -360, y)
    _connect(s, "A", mx, "A")
    _connect(s, "R", mx, "B")
    return mx


a_bg = pan(bg, 300, 0.004, 0.55)
a1 = pan(l1, 480, 0.008, 0.48)
a2 = pan(l2, 660, 0.013, 0.52)
a3 = pan(l3, 840, 0.006, 0.58)

bg_s = _add(mat, unreal.MaterialExpressionCollectionParameter, -200, 300)
bg_s.set_editor_property("collection", mpc)
bg_s.set_editor_property("parameter_name", "BGCloudsStrength")
a_bg2 = _add(mat, unreal.MaterialExpressionMultiply, 0, 300)
_connect(a_bg, "", a_bg2, "A")
_connect(bg_s, "", a_bg2, "B")
a_bgz = _add(mat, unreal.MaterialExpressionMultiply, 200, 320)
_connect(a_bg2, "", a_bgz, "A")
_connect(zb2, "", a_bgz, "B")

a12 = _add(mat, unreal.MaterialExpressionAdd, 0, 500)
_connect(a1, "", a12, "A")
_connect(a2, "", a12, "B")
a123 = _add(mat, unreal.MaterialExpressionAdd, 200, 520)
_connect(a12, "", a123, "A")
_connect(a3, "", a123, "B")
cstr = _add(mat, unreal.MaterialExpressionCollectionParameter, 0, 620)
cstr.set_editor_property("collection", mpc)
cstr.set_editor_property("parameter_name", "CloudStrength")
a_c = _add(mat, unreal.MaterialExpressionMultiply, 200, 620)
_connect(a123, "", a_c, "A")
_connect(cstr, "", a_c, "B")
a_cz = _add(mat, unreal.MaterialExpressionMultiply, 400, 640)
_connect(a_c, "", a_cz, "A")
_connect(zb2, "", a_cz, "B")
a_cs = _add(mat, unreal.MaterialExpressionSaturate, 600, 640)
_connect(a_cz, "", a_cs, "")

cloud_col = _add(mat, unreal.MaterialExpressionConstant3Vector, 0, 200)
cloud_col.set_editor_property("constant", unreal.LinearColor(1.0, 1.015, 1.04, 1.0))
tint = _add(mat, unreal.MaterialExpressionCollectionParameter, 0, 120)
tint.set_editor_property("collection", mpc)
tint.set_editor_property("parameter_name", "CloudTint")
cc = _add(mat, unreal.MaterialExpressionMultiply, 220, 160)
_connect(cloud_col, "", cc, "A")
_connect(tint, "", cc, "B")

lerp1 = _add(mat, unreal.MaterialExpressionLinearInterpolate, 420, 0)
_connect(sky, "", lerp1, "A")
_connect(cc, "", lerp1, "B")
_connect(a_bgz, "", lerp1, "Alpha")
lerp2 = _add(mat, unreal.MaterialExpressionLinearInterpolate, 640, 20)
_connect(lerp1, "", lerp2, "A")
_connect(cc, "", lerp2, "B")
_connect(a_cs, "", lerp2, "Alpha")

unreal.MaterialEditingLibrary.connect_material_property(lerp2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(lerp2, "", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.MaterialEditingLibrary.layout_material_expressions(mat)
unreal.EditorAssetLibrary.save_asset(MAT)

# show cloud shell lightly again
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") != "LobbyOriginalSky":
        continue
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if "Cloud" in c.get_name():
            c.set_visibility(True)
    fog = a.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if fog:
        fog.set_editor_property("fog_density", 0.006)
        fog.set_editor_property("fog_max_opacity", 0.3)
    sl = a.get_component_by_class(unreal.SkyLightComponent)
    if sl:
        try:
            sl.recapture_sky()
        except Exception:
            pass

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ues.set_level_viewport_camera_info(
    unreal.Vector(0.0, -12000.0, 4500.0),
    unreal.Rotator(pitch=8.0, yaw=90.0, roll=0.0),
)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("GRADIENT_PLUS_CLOUDS_OK")
