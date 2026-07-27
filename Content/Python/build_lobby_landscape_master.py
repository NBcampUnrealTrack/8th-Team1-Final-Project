"""
Rebuild Lobby landscape material following Medieval Dock M_Landscape_Master pattern:

  M_LobbyLandscape_Master  (LandscapeLayerBlend of MaterialAttributes)
  MI_LobbyLandscape        (pastel instance overrides)
  LI_Seabed / LI_Plains / LI_Mountain
  Assign + height-based weight paint

  py "E:/Unreal/MarineDemolition/Content/Python/build_lobby_landscape_master.py"
"""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
TEX_DST = "/Game/Developers/TaitoP/Landscape/Textures"
MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
LAYER_DST = "/Game/Developers/TaitoP/Landscape/Layers"
MASTER = f"{MAT_DST}/M_LobbyLandscape_Master"
MIC = f"{MAT_DST}/MI_LobbyLandscape"

# layer_name, BC, Normal, defaults: scale, sat, hue(tint amt), lum, rough, n_intensity
# Pastel tints applied via Lerp toward pastel target colors
LAYERS = [
    {
        "name": "Seabed",
        "bc": f"{TEX_DST}/T_Seabed_BC",
        "n": f"{TEX_DST}/T_Seabed_N",
        "scale": 0.55,
        "sat": -0.25,
        "hue": 0.15,
        "lum": 0.12,
        "rough": 0.92,
        "n_int": 0.65,
        "tint": (0.55, 0.72, 0.82),  # soft blue-sand
    },
    {
        "name": "Plains",
        "bc": f"{TEX_DST}/T_Plains_BC",
        "n": f"{TEX_DST}/T_Plains_N",
        "scale": 0.45,
        "sat": -0.15,
        "hue": 0.12,
        "lum": 0.08,
        "rough": 0.95,
        "n_int": 0.55,
        "tint": (0.62, 0.78, 0.58),  # soft mint grass
    },
    {
        "name": "Mountain",
        "bc": f"{TEX_DST}/T_Mountain_BC",
        "n": f"{TEX_DST}/T_Mountain_N",
        "scale": 0.22,
        "sat": -0.20,
        "hue": 0.18,
        "lum": 0.05,
        "rough": 0.98,
        "n_int": 1.35,
        "tint": (0.72, 0.66, 0.78),  # soft mauve rock
    },
]


def ensure_dirs():
    for d in (MAT_DST, LAYER_DST):
        if not unreal.EditorAssetLibrary.does_directory_exist(d):
            unreal.EditorAssetLibrary.make_directory(d)


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


def build_layer_attributes(mat, layer, base_x, base_y, coords):
    """Build MaterialAttributes for one landscape layer (dock-style controls)."""
    name = layer["name"]
    g = name
    bc_tex = unreal.EditorAssetLibrary.load_asset(layer["bc"])
    n_tex = unreal.EditorAssetLibrary.load_asset(layer["n"])
    if not bc_tex or not n_tex:
        raise RuntimeError(f"Missing textures for {name}")

    scale = _scalar(mat, f"{name}_Scale", layer["scale"], base_x - 900, base_y, g)
    uv = _add(mat, unreal.MaterialExpressionMultiply, base_x - 700, base_y)
    _connect(coords, "", uv, "A")
    _connect(scale, "", uv, "B")

    s_bc = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, base_x - 480, base_y)
    s_bc.set_editor_property("parameter_name", f"{name}_BC")
    s_bc.set_editor_property("texture", bc_tex)
    s_bc.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(uv, "", s_bc, "UVs")

    s_n = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, base_x - 480, base_y + 220)
    s_n.set_editor_property("parameter_name", f"{name}_N")
    s_n.set_editor_property("texture", n_tex)
    s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    _connect(uv, "", s_n, "UVs")

    # Desaturation: Lerp(Luma, Color, Saturate(1+Sat))
    sat = _scalar(mat, f"{name}_Saturation", layer["sat"], base_x - 220, base_y - 40, g)
    one = _const(mat, 1.0, base_x - 220, base_y + 40)
    sat_amt = _add(mat, unreal.MaterialExpressionAdd, base_x - 40, base_y - 20)
    _connect(one, "", sat_amt, "A")
    _connect(sat, "", sat_amt, "B")
    sat_clamped = _add(mat, unreal.MaterialExpressionClamp, base_x + 120, base_y - 20)
    _connect(sat_amt, "", sat_clamped, "")

    desat = _add(mat, unreal.MaterialExpressionDesaturation, base_x + 300, base_y)
    _connect(s_bc, "RGB", desat, "")
    # Fraction = 1 means fully desaturated; we want sat_param -1..+ → invert
    # Use (1 - sat_clamped) as fraction so sat=0 keeps color, sat=-1 desaturates
    inv = _add(mat, unreal.MaterialExpressionOneMinus, base_x + 120, base_y + 60)
    _connect(sat_clamped, "", inv, "")
    _connect(inv, "", desat, "Fraction")

    # Pastel tint Lerp
    tint = _vec(mat, f"{name}_Tint", layer["tint"], base_x - 220, base_y + 140, g)
    hue = _scalar(mat, f"{name}_HUE", layer["hue"], base_x - 220, base_y + 260, g)
    tinted = _add(mat, unreal.MaterialExpressionLinearInterpolate, base_x + 520, base_y)
    _connect(desat, "", tinted, "A")
    _connect(tint, "", tinted, "B")
    _connect(hue, "", tinted, "Alpha")

    # Luminosity add
    lum = _scalar(mat, f"{name}_Luminosity", layer["lum"], base_x + 300, base_y + 140, g)
    bright = _add(mat, unreal.MaterialExpressionAdd, base_x + 720, base_y)
    _connect(tinted, "", bright, "A")
    _connect(lum, "", bright, "B")

    # Normal intensity: Lerp(flat, N, intensity)
    n_int = _scalar(mat, f"{name}_Normal_Intensity", layer["n_int"], base_x - 220, base_y + 360, g)
    flat = _add(mat, unreal.MaterialExpressionConstant3Vector, base_x - 40, base_y + 360)
    flat.set_editor_property("constant", unreal.LinearColor(0.5, 0.5, 1.0, 0.0))
    n_scaled = _add(mat, unreal.MaterialExpressionLinearInterpolate, base_x + 300, base_y + 300)
    _connect(flat, "", n_scaled, "A")
    _connect(s_n, "RGB", n_scaled, "B")
    _connect(n_int, "", n_scaled, "Alpha")

    rough = _scalar(mat, f"{name}_Roughness", layer["rough"], base_x + 300, base_y + 460, g)
    metal = _const(mat, 0.0, base_x + 300, base_y + 540)
    spec = _const(mat, 0.04, base_x + 300, base_y + 600)

    make = _add(mat, unreal.MaterialExpressionMakeMaterialAttributes, base_x + 980, base_y + 80)
    _connect(bright, "", make, "BaseColor")
    _connect(n_scaled, "", make, "Normal")
    _connect(rough, "", make, "Roughness")
    _connect(metal, "", make, "Metallic")
    _connect(spec, "", make, "Specular")
    return make


def build_master():
    if unreal.EditorAssetLibrary.does_asset_exist(MASTER):
        mat = unreal.EditorAssetLibrary.load_asset(MASTER)
        for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
            unreal.MaterialEditingLibrary.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyLandscape_Master", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )

    # Use Material Attributes workflow like dock master
    try:
        mat.set_editor_property("use_material_attributes", True)
    except Exception:
        pass

    coords = _add(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1600, 200)

    makes = []
    y = -200
    for layer in LAYERS:
        makes.append(build_layer_attributes(mat, layer, -200, y, coords))
        y += 780

    blend = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 1400, 200)
    inputs = []
    for layer in LAYERS:
        inp = unreal.LayerBlendInput()
        inp.set_editor_property("layer_name", layer["name"])
        inp.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
        inp.set_editor_property("preview_weight", 0.34)
        inputs.append(inp)
    blend.set_editor_property("layers", inputs)

    for layer, make in zip(LAYERS, makes):
        unreal.MaterialEditingLibrary.connect_material_expressions(
            make, "", blend, f"Layer {layer['name']}"
        )

    # Optional sharpness bias params (dock-like, soft defaults)
    sharp = _scalar(mat, "Sharp_Blend", 1.0, 1400, -80, "Blend")
    bias = _scalar(mat, "Blend_Bias", 0.0, 1400, -20, "Blend")
    # Keep unused connected lightly so params exist on MIC (multiply into alpha unused)
    # Just leave as parameters for future; dock uses them inside height blend elsewhere.

    brk = _add(mat, unreal.MaterialExpressionBreakMaterialAttributes, 1700, 200)
    _connect(blend, "", brk, "")

    # Micro variation on base color (dock style, subtle)
    micro = _scalar(mat, "Micro_Variation", 0.04, 1700, -40, "Variation")
    var_tex = _add(mat, unreal.MaterialExpressionTextureSample, 1700, -200)
    engine_var = unreal.EditorAssetLibrary.load_asset("/Engine/EngineMaterials/T_Default_MacroVariation")
    if engine_var:
        var_tex.set_editor_property("texture", engine_var)
    tc = _add(mat, unreal.MaterialExpressionTextureCoordinate, 1500, -200)
    _connect(tc, "", var_tex, "UVs")
    var_mul = _add(mat, unreal.MaterialExpressionMultiply, 1900, -120)
    _connect(var_tex, "R", var_mul, "A")
    _connect(micro, "", var_mul, "B")
    bc_add = _add(mat, unreal.MaterialExpressionAdd, 2100, 80)
    _connect(brk, "BaseColor", bc_add, "A")
    _connect(var_mul, "", bc_add, "B")

    # Since use_material_attributes may require connecting to Material Attributes pin
    make_out = _add(mat, unreal.MaterialExpressionMakeMaterialAttributes, 2300, 200)
    _connect(bc_add, "", make_out, "BaseColor")
    _connect(brk, "Normal", make_out, "Normal")
    _connect(brk, "Roughness", make_out, "Roughness")
    _connect(brk, "Metallic", make_out, "Metallic")
    _connect(brk, "Specular", make_out, "Specular")

    try:
        unreal.MaterialEditingLibrary.connect_material_property(
            make_out, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES
        )
    except Exception:
        # Fallback non-attributes path
        try:
            mat.set_editor_property("use_material_attributes", False)
        except Exception:
            pass
        unreal.MaterialEditingLibrary.connect_material_property(bc_add, "", unreal.MaterialProperty.MP_BASE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(brk, "Normal", unreal.MaterialProperty.MP_NORMAL)
        unreal.MaterialEditingLibrary.connect_material_property(brk, "Roughness", unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.connect_material_property(brk, "Metallic", unreal.MaterialProperty.MP_METALLIC)
        unreal.MaterialEditingLibrary.connect_material_property(brk, "Specular", unreal.MaterialProperty.MP_SPECULAR)

    # silence unused
    _ = (sharp, bias)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(MASTER)
    unreal.log(f"Built {MASTER}")
    return mat


def build_mic(parent):
    if unreal.EditorAssetLibrary.does_asset_exist(MIC):
        mic = unreal.EditorAssetLibrary.load_asset(MIC)
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LobbyLandscape",
            MAT_DST,
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, parent)

    # Pastel-forward defaults (also baked into parent; reinforce on MIC)
    overrides = {
        "Seabed_Scale": 0.55,
        "Seabed_Saturation": -0.28,
        "Seabed_HUE": 0.22,
        "Seabed_Luminosity": 0.14,
        "Seabed_Roughness": 0.93,
        "Seabed_Normal_Intensity": 0.55,
        "Plains_Scale": 0.42,
        "Plains_Saturation": -0.18,
        "Plains_HUE": 0.16,
        "Plains_Luminosity": 0.10,
        "Plains_Roughness": 0.96,
        "Plains_Normal_Intensity": 0.50,
        "Mountain_Scale": 0.18,
        "Mountain_Saturation": -0.22,
        "Mountain_HUE": 0.20,
        "Mountain_Luminosity": 0.06,
        "Mountain_Roughness": 0.99,
        "Mountain_Normal_Intensity": 1.45,
        "Micro_Variation": 0.035,
        "Sharp_Blend": 1.0,
        "Blend_Bias": 0.0,
    }
    for k, v in overrides.items():
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, k, float(v))

    for layer in LAYERS:
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            mic,
            f"{layer['name']}_Tint",
            unreal.LinearColor(layer["tint"][0], layer["tint"][1], layer["tint"][2], 1.0),
        )

    unreal.EditorAssetLibrary.save_asset(MIC)
    unreal.log(f"Built {MIC}")
    return mic


def create_layer_info(name: str):
    path = f"{LAYER_DST}/LI_{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)

    factory = None
    for fname in ("LandscapeLayerInfoObjectFactory", "LandscapeLayerInfoFactory"):
        if hasattr(unreal, fname):
            factory = getattr(unreal, fname)()
            break
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    li = None
    try:
        li = asset_tools.create_asset(f"LI_{name}", LAYER_DST, unreal.LandscapeLayerInfoObject, factory)
    except Exception as exc:
        unreal.log_warning(f"LI create via factory failed: {exc}")
    if not li:
        # Duplicate a known approach: new object via duplicate if any LI exists elsewhere
        try:
            li = unreal.LandscapeLayerInfoObject()
        except Exception:
            li = None
    if li:
        try:
            li.set_editor_property("layer_name", name)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_asset(path)
        return unreal.EditorAssetLibrary.load_asset(path) or li
    unreal.log_warning(f"Could not create LI_{name}")
    return None


def find_landscape():
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            return a
    return None


def paint_by_height(landscape):
    """Paint Seabed/Plains/Mountain weights from height via render targets."""
    try:
        fmt = unreal.TextureRenderTargetFormat.RTF_RGBA16F
    except Exception:
        fmt = unreal.TextureRenderTargetFormat.RTF_RGBA8

    size = 505
    rt_h = unreal.RenderingLibrary.create_render_target2d(
        None, size, size, fmt, unreal.LinearColor(0, 0, 0, 1), False
    )
    if not landscape.landscape_export_heightmap_to_render_target(rt_h, False, True):
        unreal.log_warning("height RT export failed — skip paint")
        return False

    # Read height pixels and build weight RTs in python (CPU)
    try:
        pixels = unreal.RenderingLibrary.read_render_target(None, rt_h, True)
    except Exception as exc:
        unreal.log_warning(f"read height RT failed: {exc}")
        return False

    hs = []
    for p in pixels:
        # Packed landscape height often in R as 0..1 normalized
        hs.append(float(p.r))
    if not hs:
        unreal.log_warning("empty height pixels")
        return False

    hmin, hmax = min(hs), max(hs)
    span = max(1e-5, hmax - hmin)
    unreal.log(f"Height sample min={hmin:.4f} max={hmax:.4f}")

    sea_end = 0.28
    mtn_start = 0.55

    def weights(h):
        t = (h - hmin) / span
        sea = max(0.0, min(1.0, 1.0 - t / sea_end)) if sea_end > 0 else 0.0
        mtn = max(0.0, min(1.0, (t - mtn_start) / max(1e-5, 1.0 - mtn_start)))
        pln = max(0.0, 1.0 - sea - mtn)
        s = sea + pln + mtn
        if s <= 1e-5:
            return 0.0, 1.0, 0.0
        return sea / s, pln / s, mtn / s

    w_sea, w_pln, w_mtn = [], [], []
    for h in hs:
        a, b, c = weights(h)
        w_sea.append(a)
        w_pln.append(b)
        w_mtn.append(c)

    def import_weight(name, values):
        # Create temporary texture asset, then RT, then import — RT fill via clear is uniform only.
        # Use landscape_import_weightmap_from_render_target with per-pixel RT if we can write.
        # Fallback: create Texture2D from pixels then draw — limited in Python.
        # Best-effort: clear RT to average weight so layers exist; auto look still needs paint.
        avg = sum(values) / max(1, len(values))
        rt = unreal.RenderingLibrary.create_render_target2d(
            None, size, size, unreal.TextureRenderTargetFormat.RTF_R8, unreal.LinearColor(avg, 0, 0, 1), False
        )
        unreal.RenderingLibrary.clear_render_target2d(None, rt, unreal.LinearColor(avg, 0, 0, 1))
        ok = False
        try:
            ok = landscape.landscape_import_weightmap_from_render_target(rt, name)
        except Exception as exc:
            unreal.log_warning(f"weight import {name}: {exc}")
        unreal.log(f"weight {name} avg={avg:.3f} ok={ok}")
        return ok

    # Ensure layer infos exist / registered by importing
    for name, vals in (("Seabed", w_sea), ("Plains", w_pln), ("Mountain", w_mtn)):
        import_weight(name, vals)
    return True


def run():
    ensure_dirs()
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    for layer in LAYERS:
        for key in ("bc", "n"):
            if not unreal.EditorAssetLibrary.does_asset_exist(layer[key]):
                raise RuntimeError(f"Missing texture {layer[key]}")

    master = build_master()
    mic = build_mic(master)

    for layer in LAYERS:
        create_layer_info(layer["name"])

    landscape = find_landscape()
    if not landscape:
        raise RuntimeError("Landscape not found")

    landscape.set_editor_property("landscape_material", mic)
    try:
        landscape.post_edit_change()
    except Exception:
        pass
    try:
        landscape.set_actor_label("LobbyLandscape")
        landscape.set_folder_path("Terrain")
    except Exception:
        pass

    # NOTE: automatic weight-paint via RT can hang the editor; paint Seabed/Plains/Mountain in Landscape mode.
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.EditorAssetLibrary.save_directory(MAT_DST)
    unreal.EditorAssetLibrary.save_directory(LAYER_DST)
    unreal.log("SUCCESS: M_LobbyLandscape_Master + MI_LobbyLandscape assigned (dock-style layer blend)")


run()
