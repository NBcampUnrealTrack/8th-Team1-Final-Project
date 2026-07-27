"""
Rebuild Lobby landscape using Medieval Dock textures
(Sand / Grass / Rock) with dock-style MIC controls + height/slope auto blend.

  Seabed   <- T_SandWrocks_*
  Plains   <- T_Grass_02_*
  Mountain <- T_Rock_*

  Remote: rebuild_lobby_from_dock_textures.py
"""
from __future__ import annotations

import unreal

DOCK = "/Game/Developers/TaitoP/Medieval_Dock/Material"
MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
MASTER = f"{MAT_DST}/M_LobbyLandscape_Master"
MIC = f"{MAT_DST}/MI_LobbyLandscape"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

LAYERS = [
    {
        "name": "Seabed",
        "bc": f"{DOCK}/Material/Sand/With_Rocks/T_SandWrocks_C",
        "n": f"{DOCK}/Material/Sand/With_Rocks/T_SandWrocks_N",
        "r": f"{DOCK}/Material/Sand/With_Rocks/T_SandWrocks_R",
        "scale": 0.55,
        "sat": -0.35,
        "hue": 0.28,
        "lum": 0.18,
        "rough_mul": 1.0,
        "n_int": 0.7,
        "tint": (0.62, 0.78, 0.88),  # soft aqua sand
    },
    {
        "name": "Plains",
        "bc": f"{DOCK}/Grass/T_Grass_02_C",
        "n": f"{DOCK}/Grass/T_Grass_02_N",
        "r": f"{DOCK}/Grass/T_Grass_02_R",
        "scale": 0.40,
        "sat": -0.25,
        "hue": 0.22,
        "lum": 0.14,
        "rough_mul": 1.0,
        "n_int": 0.6,
        "tint": (0.70, 0.86, 0.68),  # soft mint grass
    },
    {
        "name": "Mountain",
        "bc": f"{DOCK}/Rock_Cliffs/T_Rock_C",
        "n": f"{DOCK}/Rock_Cliffs/T_Rock_N",
        "r": f"{DOCK}/Rock_Cliffs/T_Rock_R",
        "scale": 0.12,
        "sat": -0.30,
        "hue": 0.24,
        "lum": 0.10,
        "rough_mul": 1.15,
        "n_int": 1.6,
        "tint": (0.78, 0.72, 0.84),  # soft mauve rock
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


def build_layer(mat, layer, coords, x, y):
    name = layer["name"]
    g = name
    bc = unreal.EditorAssetLibrary.load_asset(layer["bc"])
    nrm = unreal.EditorAssetLibrary.load_asset(layer["n"])
    rough_tex = unreal.EditorAssetLibrary.load_asset(layer["r"])
    if not bc or not nrm:
        raise RuntimeError(f"missing textures for {name}: {layer['bc']} / {layer['n']}")

    scale = _scalar(mat, f"{name}_Scale", layer["scale"], x - 900, y, g)
    uv = _add(mat, unreal.MaterialExpressionMultiply, x - 700, y)
    _connect(coords, "", uv, "A")
    _connect(scale, "", uv, "B")

    s_bc = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, x - 480, y)
    s_bc.set_editor_property("parameter_name", f"{name}_BC")
    s_bc.set_editor_property("texture", bc)
    s_bc.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(uv, "", s_bc, "UVs")

    s_n = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, x - 480, y + 200)
    s_n.set_editor_property("parameter_name", f"{name}_N")
    s_n.set_editor_property("texture", nrm)
    s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    _connect(uv, "", s_n, "UVs")

    s_r = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, x - 480, y + 400)
    s_r.set_editor_property("parameter_name", f"{name}_R")
    if rough_tex:
        s_r.set_editor_property("texture", rough_tex)
        s_r.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        try:
            s_r.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        except Exception:
            pass
    _connect(uv, "", s_r, "UVs")

    # Desaturate then pastel tint (dock Saturation / HUE / Luminosity style)
    sat = _scalar(mat, f"{name}_Saturation", layer["sat"], x - 220, y - 40, g)
    one = _const(mat, 1.0, x - 220, y + 40)
    sat_amt = _add(mat, unreal.MaterialExpressionAdd, x - 40, y - 20)
    _connect(one, "", sat_amt, "A")
    _connect(sat, "", sat_amt, "B")
    sat_cl = _add(mat, unreal.MaterialExpressionClamp, x + 120, y - 20)
    _connect(sat_amt, "", sat_cl, "")
    inv = _add(mat, unreal.MaterialExpressionOneMinus, x + 120, y + 60)
    _connect(sat_cl, "", inv, "")
    desat = _add(mat, unreal.MaterialExpressionDesaturation, x + 300, y)
    _connect(s_bc, "RGB", desat, "")
    _connect(inv, "", desat, "Fraction")

    tint = _vec(mat, f"{name}_Tint", layer["tint"], x - 220, y + 140, g)
    hue = _scalar(mat, f"{name}_HUE", layer["hue"], x - 220, y + 260, g)
    tinted = _add(mat, unreal.MaterialExpressionLinearInterpolate, x + 520, y)
    _connect(desat, "", tinted, "A")
    _connect(tint, "", tinted, "B")
    _connect(hue, "", tinted, "Alpha")

    lum = _scalar(mat, f"{name}_Luminosity", layer["lum"], x + 300, y + 140, g)
    bright = _add(mat, unreal.MaterialExpressionAdd, x + 720, y)
    _connect(tinted, "", bright, "A")
    _connect(lum, "", bright, "B")

    n_int = _scalar(mat, f"{name}_Normal_Intensity", layer["n_int"], x - 220, y + 360, g)
    flat = _add(mat, unreal.MaterialExpressionConstant3Vector, x - 40, y + 360)
    flat.set_editor_property("constant", unreal.LinearColor(0.5, 0.5, 1.0, 0.0))
    n_scaled = _add(mat, unreal.MaterialExpressionLinearInterpolate, x + 300, y + 300)
    _connect(flat, "", n_scaled, "A")
    _connect(s_n, "RGB", n_scaled, "B")
    _connect(n_int, "", n_scaled, "Alpha")

    rough_mul = _scalar(mat, f"{name}_Roughness", layer["rough_mul"], x + 300, y + 480, g)
    rough = _add(mat, unreal.MaterialExpressionMultiply, x + 520, y + 480)
    _connect(s_r, "R", rough, "A")
    _connect(rough_mul, "", rough, "B")

    return bright, n_scaled, rough


def build_weights(mat):
    world_pos = _add(mat, unreal.MaterialExpressionWorldPosition, -1700, -420)
    mask_z = _add(mat, unreal.MaterialExpressionComponentMask, -1480, -420)
    mask_z.set_editor_property("r", False)
    mask_z.set_editor_property("g", False)
    mask_z.set_editor_property("b", True)
    mask_z.set_editor_property("a", False)
    _connect(world_pos, "", mask_z, "")

    z_min = _scalar(mat, "HeightMin", -952.0, -1480, -300, "Blend")
    z_max = _scalar(mat, "HeightMax", 7052.0, -1480, -220, "Blend")
    sea_end = _scalar(mat, "SeabedEnd", 0.26, -1480, -140, "Blend")
    mtn_start = _scalar(mat, "MountainStart", 0.52, -1480, -60, "Blend")
    slope_boost = _scalar(mat, "SlopeBoost", 0.65, -1480, 20, "Blend")

    sub = _add(mat, unreal.MaterialExpressionSubtract, -1260, -380)
    _connect(mask_z, "", sub, "A")
    _connect(z_min, "", sub, "B")
    denom = _add(mat, unreal.MaterialExpressionSubtract, -1260, -280)
    _connect(z_max, "", denom, "A")
    _connect(z_min, "", denom, "B")
    div = _add(mat, unreal.MaterialExpressionDivide, -1060, -340)
    _connect(sub, "", div, "A")
    _connect(denom, "", div, "B")
    h = _add(mat, unreal.MaterialExpressionClamp, -880, -340)
    _connect(div, "", h, "")

    one = _const(mat, 1.0, -1060, -200)

    sea_div = _add(mat, unreal.MaterialExpressionDivide, -680, -420)
    _connect(h, "", sea_div, "A")
    _connect(sea_end, "", sea_div, "B")
    sea_raw = _add(mat, unreal.MaterialExpressionSubtract, -500, -420)
    _connect(one, "", sea_raw, "A")
    _connect(sea_div, "", sea_raw, "B")
    sea = _add(mat, unreal.MaterialExpressionClamp, -320, -420)
    _connect(sea_raw, "", sea, "")

    mtn_sub = _add(mat, unreal.MaterialExpressionSubtract, -680, -220)
    _connect(h, "", mtn_sub, "A")
    _connect(mtn_start, "", mtn_sub, "B")
    mtn_den = _add(mat, unreal.MaterialExpressionSubtract, -500, -180)
    _connect(one, "", mtn_den, "A")
    _connect(mtn_start, "", mtn_den, "B")
    mtn_div = _add(mat, unreal.MaterialExpressionDivide, -320, -220)
    _connect(mtn_sub, "", mtn_div, "A")
    _connect(mtn_den, "", mtn_div, "B")
    mtn = _add(mat, unreal.MaterialExpressionClamp, -140, -220)
    _connect(mtn_div, "", mtn, "")

    vn = _add(mat, unreal.MaterialExpressionVertexNormalWS, -1700, -40)
    nz = _add(mat, unreal.MaterialExpressionComponentMask, -1480, -40)
    nz.set_editor_property("r", False)
    nz.set_editor_property("g", False)
    nz.set_editor_property("b", True)
    nz.set_editor_property("a", False)
    _connect(vn, "", nz, "")
    slope = _add(mat, unreal.MaterialExpressionSubtract, -1260, -40)
    _connect(one, "", slope, "A")
    _connect(nz, "", slope, "B")
    slope_c = _add(mat, unreal.MaterialExpressionClamp, -1060, -40)
    _connect(slope, "", slope_c, "")
    slope_w = _add(mat, unreal.MaterialExpressionMultiply, -880, -40)
    _connect(slope_c, "", slope_w, "A")
    _connect(slope_boost, "", slope_w, "B")
    mtn2 = _add(mat, unreal.MaterialExpressionAdd, -140, -60)
    _connect(mtn, "", mtn2, "A")
    _connect(slope_w, "", mtn2, "B")
    mtn_f = _add(mat, unreal.MaterialExpressionClamp, 40, -60)
    _connect(mtn2, "", mtn_f, "")

    sum_sm = _add(mat, unreal.MaterialExpressionAdd, 40, -300)
    _connect(sea, "", sum_sm, "A")
    _connect(mtn_f, "", sum_sm, "B")
    pln_raw = _add(mat, unreal.MaterialExpressionSubtract, 220, -300)
    _connect(one, "", pln_raw, "A")
    _connect(sum_sm, "", pln_raw, "B")
    pln = _add(mat, unreal.MaterialExpressionClamp, 400, -300)
    _connect(pln_raw, "", pln, "")

    s2 = _add(mat, unreal.MaterialExpressionAdd, 580, -260)
    _connect(sea, "", s2, "A")
    _connect(pln, "", s2, "B")
    s3 = _add(mat, unreal.MaterialExpressionAdd, 760, -220)
    _connect(s2, "", s3, "A")
    _connect(mtn_f, "", s3, "B")

    def norm(src, yy):
        d = _add(mat, unreal.MaterialExpressionDivide, 940, yy)
        _connect(src, "", d, "A")
        _connect(s3, "", d, "B")
        return d

    return norm(sea, -420), norm(pln, -300), norm(mtn_f, -60)


def weighted_sum(mat, samples, weights, base_y):
    m0 = _add(mat, unreal.MaterialExpressionMultiply, 1300, base_y)
    _connect(samples[0], "", m0, "A")
    _connect(weights[0], "", m0, "B")
    m1 = _add(mat, unreal.MaterialExpressionMultiply, 1300, base_y + 80)
    _connect(samples[1], "", m1, "A")
    _connect(weights[1], "", m1, "B")
    m2 = _add(mat, unreal.MaterialExpressionMultiply, 1300, base_y + 160)
    _connect(samples[2], "", m2, "A")
    _connect(weights[2], "", m2, "B")
    a01 = _add(mat, unreal.MaterialExpressionAdd, 1500, base_y + 40)
    _connect(m0, "", a01, "A")
    _connect(m1, "", a01, "B")
    a012 = _add(mat, unreal.MaterialExpressionAdd, 1700, base_y + 80)
    _connect(a01, "", a012, "A")
    _connect(m2, "", a012, "B")
    return a012


def run():
    for layer in LAYERS:
        for k in ("bc", "n", "r"):
            if not unreal.EditorAssetLibrary.does_asset_exist(layer[k]):
                raise RuntimeError(f"missing {layer[k]}")

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

    coords = _add(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1700, 200)
    w_sea, w_pln, w_mtn = build_weights(mat)
    weights = (w_sea, w_pln, w_mtn)

    bcs, ns, rs = [], [], []
    y = -80
    for layer in LAYERS:
        bc, nrm, rough = build_layer(mat, layer, coords, -200, y)
        bcs.append(bc)
        ns.append(nrm)
        rs.append(rough)
        y += 780

    bc_out = weighted_sum(mat, bcs, weights, -100)
    n_out = weighted_sum(mat, ns, weights, 300)
    r_out = weighted_sum(mat, rs, weights, 580)

    # dock-like micro variation
    micro = _scalar(mat, "Micro_Variation", 0.04, 1700, -100, "Variation")
    var_tex = _add(mat, unreal.MaterialExpressionTextureSample, 1700, -260)
    eng = unreal.EditorAssetLibrary.load_asset("/Engine/EngineMaterials/T_Default_MacroVariation")
    if eng:
        var_tex.set_editor_property("texture", eng)
    tc = _add(mat, unreal.MaterialExpressionTextureCoordinate, 1500, -260)
    _connect(tc, "", var_tex, "UVs")
    var_mul = _add(mat, unreal.MaterialExpressionMultiply, 1900, -180)
    _connect(var_tex, "R", var_mul, "A")
    _connect(micro, "", var_mul, "B")
    bc_final = _add(mat, unreal.MaterialExpressionAdd, 2100, -60)
    _connect(bc_out, "", bc_final, "A")
    _connect(var_mul, "", bc_final, "B")

    metal = _const(mat, 0.0, 2100, 200)
    spec = _const(mat, 0.04, 2100, 280)

    unreal.MaterialEditingLibrary.connect_material_property(bc_final, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(n_out, "", unreal.MaterialProperty.MP_NORMAL)
    unreal.MaterialEditingLibrary.connect_material_property(r_out, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    unreal.MaterialEditingLibrary.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MASTER)
    unreal.log(f"MASTER_OK {MASTER}")

    if unreal.EditorAssetLibrary.does_asset_exist(MIC):
        mic = unreal.EditorAssetLibrary.load_asset(MIC)
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LobbyLandscape", MAT_DST, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
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
            ("rough_mul", f"{layer['name']}_Roughness"),
            ("n_int", f"{layer['name']}_Normal_Intensity"),
        ):
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mic, pname, float(layer[key])
            )
        # lock textures to dock
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            mic, f"{layer['name']}_BC", unreal.EditorAssetLibrary.load_asset(layer["bc"])
        )
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            mic, f"{layer['name']}_N", unreal.EditorAssetLibrary.load_asset(layer["n"])
        )
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            mic, f"{layer['name']}_R", unreal.EditorAssetLibrary.load_asset(layer["r"])
        )

    for k, v in (
        ("HeightMin", -952.0),
        ("HeightMax", 7052.0),
        ("SeabedEnd", 0.26),
        ("MountainStart", 0.52),
        ("SlopeBoost", 0.65),
        ("Micro_Variation", 0.04),
    ):
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, k, v)

    unreal.EditorAssetLibrary.save_asset(MIC)

    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            a.set_editor_property("landscape_material", mic)
            try:
                a.post_edit_change()
            except Exception:
                pass
            unreal.log(f"assigned {mic.get_name()} -> {a.get_actor_label()}")
            break

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("SUCCESS_DOCK_TEXTURES")


run()
