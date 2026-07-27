"""
Rebuild M_LobbyLandscape_Master like Medieval Dock controls
(Scale / Saturation / HUE-Tint / Luminosity / Roughness / Normal_Intensity)
but height+slope auto-blend so it looks correct without weight paint.

  Remote: build_lobby_landscape_master_auto.py
"""
from __future__ import annotations

import unreal

MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
TEX_DST = "/Game/Developers/TaitoP/Landscape/Textures"
MASTER = f"{MAT_DST}/M_LobbyLandscape_Master"
MIC = f"{MAT_DST}/MI_LobbyLandscape"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

LAYERS = [
    {
        "name": "Seabed",
        "bc": f"{TEX_DST}/T_Seabed_BC",
        "n": f"{TEX_DST}/T_Seabed_N",
        "scale": 0.55,
        "sat": -0.28,
        "hue": 0.22,
        "lum": 0.14,
        "rough": 0.93,
        "n_int": 0.55,
        "tint": (0.55, 0.72, 0.82),
    },
    {
        "name": "Plains",
        "bc": f"{TEX_DST}/T_Plains_BC",
        "n": f"{TEX_DST}/T_Plains_N",
        "scale": 0.42,
        "sat": -0.18,
        "hue": 0.16,
        "lum": 0.10,
        "rough": 0.96,
        "n_int": 0.50,
        "tint": (0.62, 0.78, 0.58),
    },
    {
        "name": "Mountain",
        "bc": f"{TEX_DST}/T_Mountain_BC",
        "n": f"{TEX_DST}/T_Mountain_N",
        "scale": 0.18,
        "sat": -0.22,
        "hue": 0.20,
        "lum": 0.06,
        "rough": 0.99,
        "n_int": 1.45,
        "tint": (0.72, 0.66, 0.78),
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


def build_layer_sample(mat, layer, coords, base_x, base_y):
    name = layer["name"]
    g = name
    bc_tex = unreal.EditorAssetLibrary.load_asset(layer["bc"])
    n_tex = unreal.EditorAssetLibrary.load_asset(layer["n"])

    scale = _scalar(mat, f"{name}_Scale", layer["scale"], base_x - 900, base_y, g)
    uv = _add(mat, unreal.MaterialExpressionMultiply, base_x - 700, base_y)
    _connect(coords, "", uv, "A")
    _connect(scale, "", uv, "B")

    s_bc = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, base_x - 480, base_y)
    s_bc.set_editor_property("parameter_name", f"{name}_BC")
    s_bc.set_editor_property("texture", bc_tex)
    s_bc.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(uv, "", s_bc, "UVs")

    s_n = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, base_x - 480, base_y + 200)
    s_n.set_editor_property("parameter_name", f"{name}_N")
    s_n.set_editor_property("texture", n_tex)
    s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    _connect(uv, "", s_n, "UVs")

    sat = _scalar(mat, f"{name}_Saturation", layer["sat"], base_x - 220, base_y - 40, g)
    one = _const(mat, 1.0, base_x - 220, base_y + 40)
    sat_amt = _add(mat, unreal.MaterialExpressionAdd, base_x - 40, base_y - 20)
    _connect(one, "", sat_amt, "A")
    _connect(sat, "", sat_amt, "B")
    sat_cl = _add(mat, unreal.MaterialExpressionClamp, base_x + 120, base_y - 20)
    _connect(sat_amt, "", sat_cl, "")
    inv = _add(mat, unreal.MaterialExpressionOneMinus, base_x + 120, base_y + 60)
    _connect(sat_cl, "", inv, "")
    desat = _add(mat, unreal.MaterialExpressionDesaturation, base_x + 300, base_y)
    _connect(s_bc, "RGB", desat, "")
    _connect(inv, "", desat, "Fraction")

    tint = _vec(mat, f"{name}_Tint", layer["tint"], base_x - 220, base_y + 140, g)
    hue = _scalar(mat, f"{name}_HUE", layer["hue"], base_x - 220, base_y + 260, g)
    tinted = _add(mat, unreal.MaterialExpressionLinearInterpolate, base_x + 520, base_y)
    _connect(desat, "", tinted, "A")
    _connect(tint, "", tinted, "B")
    _connect(hue, "", tinted, "Alpha")

    lum = _scalar(mat, f"{name}_Luminosity", layer["lum"], base_x + 300, base_y + 140, g)
    bright = _add(mat, unreal.MaterialExpressionAdd, base_x + 720, base_y)
    _connect(tinted, "", bright, "A")
    _connect(lum, "", bright, "B")

    n_int = _scalar(mat, f"{name}_Normal_Intensity", layer["n_int"], base_x - 220, base_y + 360, g)
    flat = _add(mat, unreal.MaterialExpressionConstant3Vector, base_x - 40, base_y + 360)
    flat.set_editor_property("constant", unreal.LinearColor(0.5, 0.5, 1.0, 0.0))
    n_scaled = _add(mat, unreal.MaterialExpressionLinearInterpolate, base_x + 300, base_y + 300)
    _connect(flat, "", n_scaled, "A")
    _connect(s_n, "RGB", n_scaled, "B")
    _connect(n_int, "", n_scaled, "Alpha")

    rough = _scalar(mat, f"{name}_Roughness", layer["rough"], base_x + 300, base_y + 460, g)
    return bright, n_scaled, rough


def build_weights(mat):
    """Height + slope weights: Seabed / Plains / Mountain."""
    world_pos = _add(mat, unreal.MaterialExpressionWorldPosition, -1600, -400)
    mask_z = _add(mat, unreal.MaterialExpressionComponentMask, -1400, -400)
    mask_z.set_editor_property("r", False)
    mask_z.set_editor_property("g", False)
    mask_z.set_editor_property("b", True)
    mask_z.set_editor_property("a", False)
    _connect(world_pos, "", mask_z, "")

    z_min = _scalar(mat, "HeightMin", -952.0, -1400, -280, "Blend")
    z_max = _scalar(mat, "HeightMax", 7052.0, -1400, -200, "Blend")
    sea_end = _scalar(mat, "SeabedEnd", 0.26, -1400, -120, "Blend")
    mtn_start = _scalar(mat, "MountainStart", 0.52, -1400, -40, "Blend")

    sub = _add(mat, unreal.MaterialExpressionSubtract, -1180, -360)
    _connect(mask_z, "", sub, "A")
    _connect(z_min, "", sub, "B")
    denom = _add(mat, unreal.MaterialExpressionSubtract, -1180, -260)
    _connect(z_max, "", denom, "A")
    _connect(z_min, "", denom, "B")
    div = _add(mat, unreal.MaterialExpressionDivide, -980, -320)
    _connect(sub, "", div, "A")
    _connect(denom, "", div, "B")
    h = _add(mat, unreal.MaterialExpressionClamp, -800, -320)
    _connect(div, "", h, "")

    one = _const(mat, 1.0, -980, -180)

    sea_div = _add(mat, unreal.MaterialExpressionDivide, -600, -400)
    _connect(h, "", sea_div, "A")
    _connect(sea_end, "", sea_div, "B")
    sea_raw = _add(mat, unreal.MaterialExpressionSubtract, -420, -400)
    _connect(one, "", sea_raw, "A")
    _connect(sea_div, "", sea_raw, "B")
    sea = _add(mat, unreal.MaterialExpressionClamp, -240, -400)
    _connect(sea_raw, "", sea, "")

    mtn_sub = _add(mat, unreal.MaterialExpressionSubtract, -600, -200)
    _connect(h, "", mtn_sub, "A")
    _connect(mtn_start, "", mtn_sub, "B")
    mtn_den = _add(mat, unreal.MaterialExpressionSubtract, -420, -160)
    _connect(one, "", mtn_den, "A")
    _connect(mtn_start, "", mtn_den, "B")
    mtn_div = _add(mat, unreal.MaterialExpressionDivide, -240, -200)
    _connect(mtn_sub, "", mtn_div, "A")
    _connect(mtn_den, "", mtn_div, "B")
    mtn = _add(mat, unreal.MaterialExpressionClamp, -60, -200)
    _connect(mtn_div, "", mtn, "")

    # slope boost mountain
    vn = _add(mat, unreal.MaterialExpressionVertexNormalWS, -1600, -20)
    nz = _add(mat, unreal.MaterialExpressionComponentMask, -1400, -20)
    nz.set_editor_property("r", False)
    nz.set_editor_property("g", False)
    nz.set_editor_property("b", True)
    nz.set_editor_property("a", False)
    _connect(vn, "", nz, "")
    slope = _add(mat, unreal.MaterialExpressionSubtract, -1180, -20)
    _connect(one, "", slope, "A")
    _connect(nz, "", slope, "B")
    slope_c = _add(mat, unreal.MaterialExpressionClamp, -980, -20)
    _connect(slope, "", slope_c, "")
    slope_amt = _scalar(mat, "SlopeBoost", 0.55, -980, 60, "Blend")
    slope_w = _add(mat, unreal.MaterialExpressionMultiply, -800, -20)
    _connect(slope_c, "", slope_w, "A")
    _connect(slope_amt, "", slope_w, "B")
    mtn2 = _add(mat, unreal.MaterialExpressionAdd, -60, -40)
    _connect(mtn, "", mtn2, "A")
    _connect(slope_w, "", mtn2, "B")
    mtn_f = _add(mat, unreal.MaterialExpressionClamp, 120, -40)
    _connect(mtn2, "", mtn_f, "")

    sum_sm = _add(mat, unreal.MaterialExpressionAdd, 120, -280)
    _connect(sea, "", sum_sm, "A")
    _connect(mtn_f, "", sum_sm, "B")
    pln_raw = _add(mat, unreal.MaterialExpressionSubtract, 300, -280)
    _connect(one, "", pln_raw, "A")
    _connect(sum_sm, "", pln_raw, "B")
    pln = _add(mat, unreal.MaterialExpressionClamp, 480, -280)
    _connect(pln_raw, "", pln, "")

    # renormalize
    s2 = _add(mat, unreal.MaterialExpressionAdd, 660, -240)
    _connect(sea, "", s2, "A")
    _connect(pln, "", s2, "B")
    s3 = _add(mat, unreal.MaterialExpressionAdd, 840, -200)
    _connect(s2, "", s3, "A")
    _connect(mtn_f, "", s3, "B")

    def norm(src, y):
        d = _add(mat, unreal.MaterialExpressionDivide, 1020, y)
        _connect(src, "", d, "A")
        _connect(s3, "", d, "B")
        return d

    return norm(sea, -400), norm(pln, -280), norm(mtn_f, -40)


def weighted_sum(mat, samples, weights, base_y):
    m0 = _add(mat, unreal.MaterialExpressionMultiply, 1400, base_y)
    _connect(samples[0], "", m0, "A")
    _connect(weights[0], "", m0, "B")
    m1 = _add(mat, unreal.MaterialExpressionMultiply, 1400, base_y + 80)
    _connect(samples[1], "", m1, "A")
    _connect(weights[1], "", m1, "B")
    m2 = _add(mat, unreal.MaterialExpressionMultiply, 1400, base_y + 160)
    _connect(samples[2], "", m2, "A")
    _connect(weights[2], "", m2, "B")
    a01 = _add(mat, unreal.MaterialExpressionAdd, 1600, base_y + 40)
    _connect(m0, "", a01, "A")
    _connect(m1, "", a01, "B")
    a012 = _add(mat, unreal.MaterialExpressionAdd, 1800, base_y + 80)
    _connect(a01, "", a012, "A")
    _connect(m2, "", a012, "B")
    return a012


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
    w_sea, w_pln, w_mtn = build_weights(mat)
    weights = (w_sea, w_pln, w_mtn)

    bcs, ns, roughs = [], [], []
    y = -100
    for layer in LAYERS:
        bc, nrm, rough = build_layer_sample(mat, layer, coords, -200, y)
        bcs.append(bc)
        ns.append(nrm)
        roughs.append(rough)
        y += 720

    bc_out = weighted_sum(mat, bcs, weights, -120)
    n_out = weighted_sum(mat, ns, weights, 280)
    r_out = weighted_sum(mat, roughs, weights, 560)

    # micro variation
    micro = _scalar(mat, "Micro_Variation", 0.035, 1800, -80, "Variation")
    var_tex = _add(mat, unreal.MaterialExpressionTextureSample, 1800, -240)
    eng = unreal.EditorAssetLibrary.load_asset("/Engine/EngineMaterials/T_Default_MacroVariation")
    if eng:
        var_tex.set_editor_property("texture", eng)
    tc = _add(mat, unreal.MaterialExpressionTextureCoordinate, 1600, -240)
    _connect(tc, "", var_tex, "UVs")
    var_mul = _add(mat, unreal.MaterialExpressionMultiply, 2000, -160)
    _connect(var_tex, "R", var_mul, "A")
    _connect(micro, "", var_mul, "B")
    bc_final = _add(mat, unreal.MaterialExpressionAdd, 2200, -40)
    _connect(bc_out, "", bc_final, "A")
    _connect(var_mul, "", bc_final, "B")

    metal = _const(mat, 0.0, 2200, 200)
    spec = _const(mat, 0.04, 2200, 280)

    unreal.MaterialEditingLibrary.connect_material_property(bc_final, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(n_out, "", unreal.MaterialProperty.MP_NORMAL)
    unreal.MaterialEditingLibrary.connect_material_property(r_out, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    unreal.MaterialEditingLibrary.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    # skip layout — can be slow/hang
    unreal.EditorAssetLibrary.save_asset(MASTER)
    unreal.log(f"AUTO_MASTER_OK {MASTER}")

    # MIC
    if unreal.EditorAssetLibrary.does_asset_exist(MIC):
        mic = unreal.EditorAssetLibrary.load_asset(MIC)
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LobbyLandscape", MAT_DST, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew()
        )
    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)
    for layer in LAYERS:
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            mic, f"{layer['name']}_Tint",
            unreal.LinearColor(layer["tint"][0], layer["tint"][1], layer["tint"][2], 1.0),
        )
        for key, pname in (
            ("scale", f"{layer['name']}_Scale"),
            ("sat", f"{layer['name']}_Saturation"),
            ("hue", f"{layer['name']}_HUE"),
            ("lum", f"{layer['name']}_Luminosity"),
            ("rough", f"{layer['name']}_Roughness"),
            ("n_int", f"{layer['name']}_Normal_Intensity"),
        ):
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, pname, float(layer[key]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "HeightMin", -952.0)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "HeightMax", 7052.0)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "SeabedEnd", 0.26)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "MountainStart", 0.52)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "SlopeBoost", 0.55)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "Micro_Variation", 0.035)
    unreal.EditorAssetLibrary.save_asset(MIC)

    # Assign
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            a.set_editor_property("landscape_material", mic)
            try:
                a.post_edit_change()
            except Exception:
                pass
            unreal.log(f"assigned to {a.get_actor_label()}")
            break

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("SUCCESS_AUTO_MASTER")


run()
