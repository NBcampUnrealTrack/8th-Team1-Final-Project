"""Force solid cyan emissive on sky dome to verify mesh visibility; then restore full mat via rebuild flag."""
import unreal

MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSky"
HEIGHT = "/Game/Developers/TaitoP/Environment/Sky/Textures/T_LobbySky_HeightGradient"


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


mat = unreal.EditorAssetLibrary.load_asset(MAT)
height = unreal.EditorAssetLibrary.load_asset(HEIGHT)
_clear(mat)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)

# Camera-Z height sample of gradient only — simplest proven path
cam = _add(mat, unreal.MaterialExpressionCameraVectorWS, -800, 0)
z = _add(mat, unreal.MaterialExpressionComponentMask, -600, 0)
z.set_editor_property("r", False)
z.set_editor_property("g", False)
z.set_editor_property("b", True)
z.set_editor_property("a", False)
unreal.MaterialEditingLibrary.connect_material_expressions(cam, "", z, "")
one = _add(mat, unreal.MaterialExpressionConstant, -600, 80)
one.set_editor_property("r", 1.0)
add = _add(mat, unreal.MaterialExpressionAdd, -400, 20)
unreal.MaterialEditingLibrary.connect_material_expressions(z, "", add, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(one, "", add, "B")
half = _add(mat, unreal.MaterialExpressionConstant, -400, 100)
half.set_editor_property("r", 0.5)
mul = _add(mat, unreal.MaterialExpressionMultiply, -220, 20)
unreal.MaterialEditingLibrary.connect_material_expressions(add, "", mul, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(half, "", mul, "B")
inv = _add(mat, unreal.MaterialExpressionOneMinus, -40, 20)
unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", inv, "")
u0 = _add(mat, unreal.MaterialExpressionConstant, -40, -60)
u0.set_editor_property("r", 0.5)
uv = _add(mat, unreal.MaterialExpressionAppendVector, 140, 0)
unreal.MaterialEditingLibrary.connect_material_expressions(u0, "", uv, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(inv, "", uv, "B")
s = _add(mat, unreal.MaterialExpressionTextureSample, 340, 0)
s.set_editor_property("texture", height)
s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", s, "UVs")
boost = _add(mat, unreal.MaterialExpressionConstant, 340, 120)
boost.set_editor_property("r", 1.4)
out = _add(mat, unreal.MaterialExpressionMultiply, 540, 40)
unreal.MaterialEditingLibrary.connect_material_expressions(s, "", out, "A")
unreal.MaterialEditingLibrary.connect_material_expressions(boost, "", out, "B")

unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.connect_material_property(out, "", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(MAT)

# hide translucent cloud shell temporarily so it can't black-out
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") != "LobbyOriginalSky":
        continue
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if "Cloud" in c.get_name():
            c.set_visibility(False)
            unreal.log(f"hid {c.get_name()}")
    fog = a.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if fog:
        fog.set_editor_property("fog_density", 0.0)

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ues.set_level_viewport_camera_info(
    unreal.Vector(0.0, -12000.0, 5000.0),
    unreal.Rotator(pitch=10.0, yaw=90.0, roll=0.0),
)
unreal.log("SIMPLE_GRADIENT_SKY_OK")
