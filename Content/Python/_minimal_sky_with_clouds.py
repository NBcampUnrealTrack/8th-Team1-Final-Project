"""Minimal working sky: gradient + one cloud layer, no MPC brightness (constants)."""
import unreal

MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSky"
TEX = "/Game/Developers/TaitoP/Environment/Sky/Textures"


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def _connect(a, ap, b, bp=""):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, ap, b, bp)


mat = unreal.EditorAssetLibrary.load_asset(MAT)
height = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbySky_HeightGradient")
l1 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L1")
l2 = unreal.EditorAssetLibrary.load_asset(f"{TEX}/T_LobbyCloud_L2")
_clear(mat)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)

cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -1000, 0)
z = _add(mat, unreal.MaterialExpressionComponentMask, -800, 0)
z.set_editor_property("r", False)
z.set_editor_property("g", False)
z.set_editor_property("b", True)
z.set_editor_property("a", False)
_connect(cam, "", z, "")
one = _add(mat, unreal.MaterialExpressionConstant, -800, 80)
one.set_editor_property("r", 1.0)
add = _add(mat, unreal.MaterialExpressionAdd, -600, 20)
_connect(z, "", add, "A")
_connect(one, "", add, "B")
half = _add(mat, unreal.MaterialExpressionConstant, -600, 100)
half.set_editor_property("r", 0.5)
mul = _add(mat, unreal.MaterialExpressionMultiply, -420, 20)
_connect(add, "", mul, "A")
_connect(half, "", mul, "B")
inv = _add(mat, unreal.MaterialExpressionOneMinus, -240, 20)
_connect(mul, "", inv, "")
u0 = _add(mat, unreal.MaterialExpressionConstant, -240, -60)
u0.set_editor_property("r", 0.5)
huv = _add(mat, unreal.MaterialExpressionAppendVector, -60, 0)
_connect(u0, "", huv, "A")
_connect(inv, "", huv, "B")
s_h = _add(mat, unreal.MaterialExpressionTextureSample, 140, 0)
s_h.set_editor_property("texture", height)
s_h.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
_connect(huv, "", s_h, "UVs")
boost = _add(mat, unreal.MaterialExpressionConstant, 140, 120)
boost.set_editor_property("r", 1.35)
sky = _add(mat, unreal.MaterialExpressionMultiply, 340, 40)
_connect(s_h, "", sky, "A")
_connect(boost, "", sky, "B")

time = _add(mat, unreal.MaterialExpressionTime, -1000, 300)
uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -1000, 220)
st = _add(mat, unreal.MaterialExpressionConstant, -800, 220)
st.set_editor_property("r", 0.5)
uvs = _add(mat, unreal.MaterialExpressionMultiply, -600, 220)
_connect(uv, "", uvs, "A")
_connect(st, "", uvs, "B")
sp = _add(mat, unreal.MaterialExpressionConstant, -800, 300)
sp.set_editor_property("r", 0.01)
tm = _add(mat, unreal.MaterialExpressionMultiply, -600, 300)
_connect(time, "", tm, "A")
_connect(sp, "", tm, "B")
pan = _add(mat, unreal.MaterialExpressionPanner, -400, 240)
_connect(uvs, "", pan, "Coordinate")
_connect(tm, "", pan, "Time")
s1 = _add(mat, unreal.MaterialExpressionTextureSample, -180, 240)
s1.set_editor_property("texture", l1)
s1.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
_connect(pan, "", s1, "UVs")

# second layer slower
st2 = _add(mat, unreal.MaterialExpressionConstant, -800, 420)
st2.set_editor_property("r", 0.55)
uvs2 = _add(mat, unreal.MaterialExpressionMultiply, -600, 420)
_connect(uv, "", uvs2, "A")
_connect(st2, "", uvs2, "B")
sp2 = _add(mat, unreal.MaterialExpressionConstant, -800, 500)
sp2.set_editor_property("r", 0.006)
tm2 = _add(mat, unreal.MaterialExpressionMultiply, -600, 500)
_connect(time, "", tm2, "A")
_connect(sp2, "", tm2, "B")
pan2 = _add(mat, unreal.MaterialExpressionPanner, -400, 440)
_connect(uvs2, "", pan2, "Coordinate")
_connect(tm2, "", pan2, "Time")
s2 = _add(mat, unreal.MaterialExpressionTextureSample, -180, 440)
s2.set_editor_property("texture", l2)
s2.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
_connect(pan2, "", s2, "UVs")

a_sum = _add(mat, unreal.MaterialExpressionAdd, 40, 320)
_connect(s1, "R", a_sum, "A")
_connect(s2, "R", a_sum, "B")
astr = _add(mat, unreal.MaterialExpressionConstant, 40, 400)
astr.set_editor_property("r", 1.15)
a_m = _add(mat, unreal.MaterialExpressionMultiply, 220, 340)
_connect(a_sum, "", a_m, "A")
_connect(astr, "", a_m, "B")
# multiply by horizon weight inv
a_h = _add(mat, unreal.MaterialExpressionMultiply, 400, 300)
_connect(a_m, "", a_h, "A")
_connect(inv, "", a_h, "B")
a_s = _add(mat, unreal.MaterialExpressionSaturate, 560, 300)
_connect(a_h, "", a_s, "")

white = _add(mat, unreal.MaterialExpressionConstant3Vector, 340, 160)
white.set_editor_property("constant", unreal.LinearColor(1.0, 1.02, 1.05, 1.0))
lerp = _add(mat, unreal.MaterialExpressionLinearInterpolate, 720, 80)
_connect(sky, "", lerp, "A")
_connect(white, "", lerp, "B")
_connect(a_s, "", lerp, "Alpha")

unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(MAT)

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") != "LobbyOriginalSky":
        continue
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if "Cloud" in c.get_name():
            c.set_visibility(False)
    fog = a.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if fog:
        fog.set_editor_property("fog_density", 0.0)

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ues.set_level_viewport_camera_info(
    unreal.Vector(0.0, -12000.0, 5000.0),
    unreal.Rotator(pitch=10.0, yaw=90.0, roll=0.0),
)
unreal.log("MINIMAL_CLOUDS_OK")
