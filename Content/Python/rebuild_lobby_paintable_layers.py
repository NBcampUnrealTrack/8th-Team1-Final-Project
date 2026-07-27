"""Rebuild paintable Lobby landscape (LandscapeLayerBlend) + register target layers."""
from __future__ import annotations

import unreal

DOCK = "/Game/Developers/TaitoP/Medieval_Dock/Material"
MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
LAYER_DST = "/Game/Developers/TaitoP/Landscape/Layers"
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
        "rough": 1.0,
        "n_int": 0.7,
        "tint": (0.62, 0.78, 0.88),
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
        "rough": 1.0,
        "n_int": 0.6,
        "tint": (0.70, 0.86, 0.68),
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
        "rough": 1.15,
        "n_int": 1.6,
        "tint": (0.78, 0.72, 0.84),
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


def build_layer_sample(mat, layer, coords, x, y):
    name = layer["name"]
    g = name
    bc = unreal.EditorAssetLibrary.load_asset(layer["bc"])
    nrm = unreal.EditorAssetLibrary.load_asset(layer["n"])
    rough_tex = unreal.EditorAssetLibrary.load_asset(layer["r"])

    scale = _scalar(mat, f"{name}_Scale", layer["scale"], x - 900, y, g)
    uv = _add(mat, unreal.MaterialExpressionMultiply, x - 700, y)
    _connect(coords, "", uv, "A")
    _connect(scale, "", uv, "B")

    s_bc = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, x - 480, y)
    s_bc.set_editor_property("parameter_name", f"{name}_BC")
    s_bc.set_editor_property("texture", bc)
    s_bc.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(uv, "", s_bc, "UVs")

    s_n = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, x - 480, y + 180)
    s_n.set_editor_property("parameter_name", f"{name}_N")
    s_n.set_editor_property("texture", nrm)
    s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    _connect(uv, "", s_n, "UVs")

    s_r = _add(mat, unreal.MaterialExpressionTextureSampleParameter2D, x - 480, y + 360)
    s_r.set_editor_property("parameter_name", f"{name}_R")
    s_r.set_editor_property("texture", rough_tex)
    # Dock roughness maps are grayscale — Masks sampler fails compile.
    s_r.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    _connect(uv, "", s_r, "UVs")

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

    rough_mul = _scalar(mat, f"{name}_Roughness", layer["rough"], x + 300, y + 460, g)
    rough = _add(mat, unreal.MaterialExpressionMultiply, x + 520, y + 460)
    _connect(s_r, "R", rough, "A")
    _connect(rough_mul, "", rough, "B")
    return bright, n_scaled, rough


def make_blend_inputs():
    arr = []
    for layer in LAYERS:
        inp = unreal.LayerBlendInput()
        inp.set_editor_property("layer_name", layer["name"])
        inp.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
        inp.set_editor_property("preview_weight", 0.34)
        arr.append(inp)
    return arr


def ensure_layer_info(name: str):
    path = f"{LAYER_DST}/LI_{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        li = unreal.EditorAssetLibrary.load_asset(path)
    else:
        if not unreal.EditorAssetLibrary.does_directory_exist(LAYER_DST):
            unreal.EditorAssetLibrary.make_directory(LAYER_DST)
        factory = None
        if hasattr(unreal, "LandscapeLayerInfoObjectFactory"):
            factory = unreal.LandscapeLayerInfoObjectFactory()
        li = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            f"LI_{name}", LAYER_DST, unreal.LandscapeLayerInfoObject, factory
        )
    if li:
        try:
            li.set_editor_property("layer_name", name)
        except Exception:
            pass
        unreal.EditorAssetLibrary.save_asset(path)
        return unreal.EditorAssetLibrary.load_asset(path)
    return None


def register_target_layers(landscape):
    names = []
    try:
        names = list(landscape.get_target_layer_names())
    except Exception:
        names = []
    unreal.log(f"existing target layers: {names}")

    for layer in LAYERS:
        name = layer["name"]
        li = ensure_layer_info(name)
        if name in [str(n) for n in names]:
            unreal.log(f"already has {name}")
            continue
        settings = unreal.LandscapeTargetLayerSettings()
        if li:
            settings.set_editor_property("layer_info_obj", li)
        # Prefer typed add_target_layer(name, settings)
        ok = False
        for attempt in (
            lambda: landscape.add_target_layer(name, settings, True),
            lambda: landscape.add_target_layer(name, settings),
            lambda: landscape.add_target_layer(unreal.Name(name), settings, True),
        ):
            try:
                attempt()
                ok = True
                break
            except Exception as e:
                last = e
        if ok:
            unreal.log(f"AddTargetLayer OK {name}")
        else:
            unreal.log_warning(f"AddTargetLayer FAIL {name}: {last}")

    try:
        unreal.log(f"after: {landscape.get_target_layer_names()}")
    except Exception as e:
        unreal.log(f"after names err {e}")


def seed_plains(landscape):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, 256, 256, unreal.TextureRenderTargetFormat.RTF_R8,
        unreal.LinearColor(1, 0, 0, 1), False,
    )
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(1, 0, 0, 1))
    ok = landscape.landscape_import_weightmap_from_render_target(rt, "Plains")
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
    ok2 = landscape.landscape_import_weightmap_from_render_target(rt, "Seabed")
    ok3 = landscape.landscape_import_weightmap_from_render_target(rt, "Mountain")
    unreal.log(f"seed weights Plains={ok} Seabed={ok2} Mountain={ok3}")


def run():
    if not unreal.EditorAssetLibrary.does_directory_exist(MAT_DST):
        unreal.EditorAssetLibrary.make_directory(MAT_DST)

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

    coords = _add(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1400, 200)

    blend_bc = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 1100, -40)
    blend_n = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 1100, 280)
    blend_r = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 1100, 600)
    blend_bc.set_editor_property("layers", make_blend_inputs())
    blend_n.set_editor_property("layers", make_blend_inputs())
    blend_r.set_editor_property("layers", make_blend_inputs())

    y = -80
    for layer in LAYERS:
        bc, nrm, rough = build_layer_sample(mat, layer, coords, -200, y)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            bc, "", blend_bc, f"Layer {layer['name']}"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            nrm, "", blend_n, f"Layer {layer['name']}"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            rough, "", blend_r, f"Layer {layer['name']}"
        )
        y += 760

    metal = _const(mat, 0.0, 1400, 80)
    spec = _const(mat, 0.04, 1400, 160)

    unreal.MaterialEditingLibrary.connect_material_property(blend_bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(blend_n, "", unreal.MaterialProperty.MP_NORMAL)
    unreal.MaterialEditingLibrary.connect_material_property(blend_r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    unreal.MaterialEditingLibrary.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MASTER)
    unreal.log(f"MASTER_PAINTABLE_OK {MASTER}")

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
            ("rough", f"{layer['name']}_Roughness"),
            ("n_int", f"{layer['name']}_Normal_Intensity"),
        ):
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mic, pname, float(layer[key])
            )
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            mic, f"{layer['name']}_BC", unreal.EditorAssetLibrary.load_asset(layer["bc"])
        )
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            mic, f"{layer['name']}_N", unreal.EditorAssetLibrary.load_asset(layer["n"])
        )
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            mic, f"{layer['name']}_R", unreal.EditorAssetLibrary.load_asset(layer["r"])
        )
    unreal.EditorAssetLibrary.save_asset(MIC)

    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    landscape = None
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            landscape = a
            break
    if not landscape:
        raise RuntimeError("no landscape")

    landscape.set_editor_property("landscape_material", mic)
    try:
        landscape.post_edit_change()
    except Exception:
        pass

    register_target_layers(landscape)
    seed_plains(landscape)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.EditorAssetLibrary.save_directory(LAYER_DST)
    unreal.log("SUCCESS_PAINTABLE_LAYERS")


run()
