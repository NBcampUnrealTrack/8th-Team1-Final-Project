"""
Enrich Lobby landscape material variation (still paintable Seabed/Plains/Mountain):
- Dual-scale texture blend (large + detail)
- Macro variation mask (engine T_Default_MacroVariation)
- Per-layer tiling / tint / luminosity / normal intensity
- Soft micro color noise
"""
from __future__ import annotations

import unreal

# Owned Lobby textures only — never Medieval_Dock / SoStylized / external packs
TEX = "/Game/Developers/TaitoP/Landscape/Textures"
MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
MASTER = f"{MAT_DST}/M_LobbyLandscape_Master"
MIC = f"{MAT_DST}/MI_LobbyLandscape"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MACRO = "/Engine/EngineMaterials/T_Default_MacroVariation"

# name, BC, N, large_scale, detail_scale, tint, hue, sat, lum, n_int
LAYERS = [
    {
        "name": "Seabed",
        "bc": f"{TEX}/T_Seabed_BC",
        "n": f"{TEX}/T_Seabed_N",
        "large": 0.22,
        "detail": 1.35,
        "tint": (0.55, 0.82, 0.98),
        "hue": 0.42,
        "sat": -0.05,
        "lum": 0.18,
        "n_int": 0.9,
    },
    {
        "name": "Plains",
        "bc": f"{TEX}/T_Plains_BC",
        "n": f"{TEX}/T_Plains_N",
        "large": 0.18,
        "detail": 1.10,
        "tint": (0.55, 0.92, 0.48),
        "hue": 0.38,
        "sat": -0.02,
        "lum": 0.14,
        "n_int": 0.8,
    },
    {
        "name": "Mountain",
        "bc": f"{TEX}/T_Mountain_BC",
        "n": f"{TEX}/T_Mountain_N",
        "large": 0.08,
        "detail": 0.55,
        "tint": (0.78, 0.74, 0.68),
        "hue": 0.22,
        "lum": 0.10,
        "sat": -0.12,
        "n_int": 1.4,
    },
]


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _connect(a, ap, b, bp):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, ap, b, bp)


def _scalar(mat, name, default, x, y, group=""):
    n = _add(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", float(default))
    if group:
        try:
            n.set_editor_property("group", group)
        except Exception:
            pass
    return n


def _vec(mat, name, rgb, x, y, group=""):
    n = _add(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    if group:
        try:
            n.set_editor_property("group", group)
        except Exception:
            pass
    return n


def _const(mat, r, x, y):
    n = _add(mat, unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", float(r))
    return n


def _uv_scaled(mat, coords, scale_node, x, y):
    mul = _add(mat, unreal.MaterialExpressionMultiply, x, y)
    _connect(coords, "", mul, "A")
    _connect(scale_node, "", mul, "B")
    return mul


def _sample(mat, tex, sampler, uv, x, y):
    s = _add(mat, unreal.MaterialExpressionTextureSample, x, y)
    s.set_editor_property("texture", tex)
    s.set_editor_property("sampler_type", sampler)
    _connect(uv, "", s, "UVs")
    return s


def build_layer(mat, layer, coords, macro_mask, x, y):
    """Dual-scale BC/N + pastel tint + macro-driven mix."""
    name = layer["name"]
    g = name
    bc_tex = unreal.EditorAssetLibrary.load_asset(layer["bc"])
    n_tex = unreal.EditorAssetLibrary.load_asset(layer["n"])

    sc_large = _scalar(mat, f"{name}_Scale", layer["large"], x - 1100, y - 40, g)
    sc_detail = _scalar(mat, f"{name}_DetailScale", layer["detail"], x - 1100, y + 40, g)
    detail_amt = _scalar(mat, f"{name}_DetailAmount", 0.55, x - 1100, y + 120, g)

    uv_l = _uv_scaled(mat, coords, sc_large, x - 880, y - 20)
    uv_d = _uv_scaled(mat, coords, sc_detail, x - 880, y + 100)

    bc_l = _sample(mat, bc_tex, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, uv_l, x - 650, y - 40)
    bc_d = _sample(mat, bc_tex, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, uv_d, x - 650, y + 140)
    n_l = _sample(mat, n_tex, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, uv_l, x - 650, y + 320)
    n_d = _sample(mat, n_tex, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, uv_d, x - 650, y + 500)

    # Mix factor = saturate(macro * detail_amt + (1-detail_amt)*0.35)
    mix_mul = _add(mat, unreal.MaterialExpressionMultiply, x - 420, y + 80)
    _connect(macro_mask, "", mix_mul, "A")
    _connect(detail_amt, "", mix_mul, "B")
    base_mix = _const(mat, 0.35, x - 420, y + 160)
    one = _const(mat, 1.0, x - 420, y + 220)
    inv_amt = _add(mat, unreal.MaterialExpressionOneMinus, x - 250, y + 200)
    _connect(detail_amt, "", inv_amt, "")
    base_w = _add(mat, unreal.MaterialExpressionMultiply, x - 80, y + 180)
    _connect(base_mix, "", base_w, "A")
    _connect(inv_amt, "", base_w, "B")
    mix_raw = _add(mat, unreal.MaterialExpressionAdd, x + 80, y + 100)
    _connect(mix_mul, "", mix_raw, "A")
    _connect(base_w, "", mix_raw, "B")
    mix = _add(mat, unreal.MaterialExpressionClamp, x + 240, y + 100)
    _connect(mix_raw, "", mix, "")

    bc_mix = _add(mat, unreal.MaterialExpressionLinearInterpolate, x + 420, y)
    _connect(bc_l, "", bc_mix, "A")
    _connect(bc_d, "", bc_mix, "B")
    _connect(mix, "", bc_mix, "Alpha")

    n_mix = _add(mat, unreal.MaterialExpressionLinearInterpolate, x + 420, y + 360)
    _connect(n_l, "", n_mix, "A")
    _connect(n_d, "", n_mix, "B")
    _connect(mix, "", n_mix, "Alpha")

    # Desat + pastel tint
    sat = _scalar(mat, f"{name}_Saturation", layer["sat"], x + 420, y - 120, g)
    sat_amt = _add(mat, unreal.MaterialExpressionAdd, x + 620, y - 100)
    _connect(one, "", sat_amt, "A")
    _connect(sat, "", sat_amt, "B")
    sat_cl = _add(mat, unreal.MaterialExpressionClamp, x + 800, y - 100)
    _connect(sat_amt, "", sat_cl, "")
    inv_sat = _add(mat, unreal.MaterialExpressionOneMinus, x + 800, y - 20)
    _connect(sat_cl, "", inv_sat, "")
    desat = _add(mat, unreal.MaterialExpressionDesaturation, x + 1000, y)
    _connect(bc_mix, "", desat, "")
    _connect(inv_sat, "", desat, "Fraction")

    tint = _vec(mat, f"{name}_Tint", layer["tint"], x + 620, y + 40, g)
    hue = _scalar(mat, f"{name}_HUE", layer["hue"], x + 620, y + 160, g)
    tinted = _add(mat, unreal.MaterialExpressionLinearInterpolate, x + 1200, y)
    _connect(desat, "", tinted, "A")
    _connect(tint, "", tinted, "B")
    _connect(hue, "", tinted, "Alpha")

    lum = _scalar(mat, f"{name}_Luminosity", layer["lum"], x + 1000, y + 140, g)
    bright = _add(mat, unreal.MaterialExpressionAdd, x + 1400, y)
    _connect(tinted, "", bright, "A")
    _connect(lum, "", bright, "B")

    # Macro luminance variation
    macro_str = _scalar(mat, f"{name}_MacroStrength", 0.12, x + 1000, y + 220, g)
    macro_off = _add(mat, unreal.MaterialExpressionMultiply, x + 1200, y + 220)
    # (macro-0.5)*strength
    half = _const(mat, 0.5, x + 1000, y + 280)
    macro_c = _add(mat, unreal.MaterialExpressionSubtract, x + 1200, y + 280)
    _connect(macro_mask, "", macro_c, "A")
    _connect(half, "", macro_c, "B")
    _connect(macro_c, "", macro_off, "A")
    _connect(macro_str, "", macro_off, "B")
    bc_out = _add(mat, unreal.MaterialExpressionAdd, x + 1600, y)
    _connect(bright, "", bc_out, "A")
    _connect(macro_off, "", bc_out, "B")

    n_int = _scalar(mat, f"{name}_Normal_Intensity", layer["n_int"], x + 620, y + 420, g)
    flat = _add(mat, unreal.MaterialExpressionConstant3Vector, x + 800, y + 420)
    flat.set_editor_property("constant", unreal.LinearColor(0.5, 0.5, 1.0, 0.0))
    n_out = _add(mat, unreal.MaterialExpressionLinearInterpolate, x + 1400, y + 360)
    _connect(flat, "", n_out, "A")
    _connect(n_mix, "", n_out, "B")
    _connect(n_int, "", n_out, "Alpha")

    return bc_out, n_out


def run():
    if unreal.EditorAssetLibrary.does_asset_exist(MASTER):
        mat = unreal.EditorAssetLibrary.load_asset(MASTER)
        for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
            unreal.MaterialEditingLibrary.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyLandscape_Master", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )
    try:
        mat.set_editor_property("use_material_attributes", False)
    except Exception:
        pass

    coords = _add(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1600, 200)

    # Shared macro variation (large world UV)
    macro_scale = _scalar(mat, "MacroScale", 0.035, -1600, -80, "Variation")
    macro_uv = _add(mat, unreal.MaterialExpressionMultiply, -1400, -60)
    _connect(coords, "", macro_uv, "A")
    _connect(macro_scale, "", macro_uv, "B")
    macro_tex = unreal.EditorAssetLibrary.load_asset(MACRO)
    macro_s = _add(mat, unreal.MaterialExpressionTextureSample, -1180, -100)
    if macro_tex:
        macro_s.set_editor_property("texture", macro_tex)
    # Must be Color (not LinearColor) or landscape MIC fails to compile
    macro_s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(macro_uv, "", macro_s, "UVs")
    # Use R as mask
    # ComponentMask R
    macro_r = _add(mat, unreal.MaterialExpressionComponentMask, -960, -80)
    macro_r.set_editor_property("r", True)
    macro_r.set_editor_property("g", False)
    macro_r.set_editor_property("b", False)
    macro_r.set_editor_property("a", False)
    _connect(macro_s, "", macro_r, "")

    def make_inputs():
        arr = []
        for layer in LAYERS:
            inp = unreal.LayerBlendInput()
            inp.set_editor_property("layer_name", layer["name"])
            inp.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
            inp.set_editor_property("preview_weight", 0.34)
            arr.append(inp)
        return arr

    blend_bc = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 2200, 0)
    blend_n = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 2200, 400)
    blend_bc.set_editor_property("layers", make_inputs())
    blend_n.set_editor_property("layers", make_inputs())

    y = -200
    for layer in LAYERS:
        bc, nrm = build_layer(mat, layer, coords, macro_r, -200, y)
        _connect(bc, "", blend_bc, f"Layer {layer['name']}")
        _connect(nrm, "", blend_n, f"Layer {layer['name']}")
        y += 780

    rough = _const(mat, 0.88, 2500, 200)
    metal = _const(mat, 0.0, 2500, 280)
    spec = _const(mat, 0.04, 2500, 360)

    unreal.MaterialEditingLibrary.connect_material_property(blend_bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(blend_n, "", unreal.MaterialProperty.MP_NORMAL)
    unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    unreal.MaterialEditingLibrary.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MASTER)

    if unreal.EditorAssetLibrary.does_asset_exist(MIC):
        mic = unreal.EditorAssetLibrary.load_asset(MIC)
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LobbyLandscape", MAT_DST, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)

    # Reinforce variation defaults on MIC
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "MacroScale", 0.035)
    for layer in LAYERS:
        n = layer["name"]
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_Scale", float(layer["large"]))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_DetailScale", float(layer["detail"]))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_DetailAmount", 0.55)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_MacroStrength", 0.12)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_Saturation", float(layer["sat"]))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_HUE", float(layer["hue"]))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_Luminosity", float(layer["lum"]))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{n}_Normal_Intensity", float(layer["n_int"]))
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            mic, f"{n}_Tint",
            unreal.LinearColor(layer["tint"][0], layer["tint"][1], layer["tint"][2], 1.0),
        )
    unreal.EditorAssetLibrary.save_asset(MIC)

    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            a.set_editor_property("landscape_material", mic)
            try:
                a.post_edit_change()
            except Exception:
                pass
            break

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("VARIATION_OK")


run()
