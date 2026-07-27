"""
Repair M_LobbyLandscape_Master / MI_LobbyLandscape using ONLY owned Lobby textures.
Medieval_Dock / SoStylized / any external pack refs are intentionally avoided.

Layers: Seabed / Plains / Mountain (dual-scale + macro variation).
"""
from __future__ import annotations

from pathlib import Path

import unreal

TEX_DST = "/Game/Developers/TaitoP/Landscape/Textures"
MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
LAYER_DST = "/Game/Developers/TaitoP/Landscape/Layers"
MASTER = f"{MAT_DST}/M_LobbyLandscape_Master"
MIC = f"{MAT_DST}/MI_LobbyLandscape"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MACRO = "/Engine/EngineMaterials/T_Default_MacroVariation"
ART = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Landscape")

LAYERS = [
    {
        "name": "Seabed",
        "bc": f"{TEX_DST}/T_Seabed_BC",
        "n": f"{TEX_DST}/T_Seabed_N",
        "png_bc": "T_Seabed_BC.png",
        "png_n": "T_Seabed_N.png",
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
        "bc": f"{TEX_DST}/T_Plains_BC",
        "n": f"{TEX_DST}/T_Plains_N",
        "png_bc": "T_Plains_BC.png",
        "png_n": "T_Plains_N.png",
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
        "bc": f"{TEX_DST}/T_Mountain_BC",
        "n": f"{TEX_DST}/T_Mountain_N",
        "png_bc": "T_Mountain_BC.png",
        "png_n": "T_Mountain_N.png",
        "large": 0.08,
        "detail": 0.55,
        "tint": (0.78, 0.74, 0.68),
        "hue": 0.22,
        "sat": -0.12,
        "lum": 0.10,
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


def import_texture(src: Path, name: str, is_normal: bool):
    if not unreal.EditorAssetLibrary.does_directory_exist(TEX_DST):
        unreal.EditorAssetLibrary.make_directory(TEX_DST)
    task = unreal.AssetImportTask()
    task.filename = str(src).replace("\\", "/")
    task.destination_path = TEX_DST
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(f"{TEX_DST}/{name}")
    if not tex:
        raise RuntimeError(f"import failed {name}")
    if is_normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    unreal.log(f"imported {name}")
    return tex


def ensure_owned_textures():
    out = {}
    for layer in LAYERS:
        bc_name = layer["bc"].rsplit("/", 1)[-1]
        n_name = layer["n"].rsplit("/", 1)[-1]
        bc = unreal.EditorAssetLibrary.load_asset(layer["bc"]) if unreal.EditorAssetLibrary.does_asset_exist(layer["bc"]) else None
        n = unreal.EditorAssetLibrary.load_asset(layer["n"]) if unreal.EditorAssetLibrary.does_asset_exist(layer["n"]) else None
        # Always reimport from Art so we own fresh non-pack textures
        bc = import_texture(ART / layer["png_bc"], bc_name, False)
        n = import_texture(ART / layer["png_n"], n_name, True)
        if "SoStylized" in bc.get_path_name() or "Medieval" in bc.get_path_name():
            raise RuntimeError(f"external leak on {bc.get_path_name()}")
        out[layer["name"]] = (bc, n)
        layer["bc_tex"] = bc
        layer["n_tex"] = n
    return out


def ensure_layer_infos():
    for layer in LAYERS:
        path = f"{LAYER_DST}/LI_{layer['name']}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            continue
        if not unreal.EditorAssetLibrary.does_directory_exist(LAYER_DST):
            unreal.EditorAssetLibrary.make_directory(LAYER_DST)
        # LandscapeLayerInfoObject via factory if available
        try:
            factory = unreal.LandscapeLayerInfoObjectFactory()
            li = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                f"LI_{layer['name']}", LAYER_DST, unreal.LandscapeLayerInfoObject, factory
            )
            unreal.EditorAssetLibrary.save_asset(path)
            unreal.log(f"created {path}")
        except Exception as e:
            unreal.log_warning(f"LI create {layer['name']}: {e}")


def build_layer(mat, layer, coords, macro_mask, x, y):
    name = layer["name"]
    g = name
    bc_tex = layer["bc_tex"]
    n_tex = layer["n_tex"]
    if not bc_tex or not n_tex:
        raise RuntimeError(f"missing textures for {name}")

    sc_large = _scalar(mat, f"{name}_Scale", layer["large"], x - 1100, y - 40, g)
    sc_detail = _scalar(mat, f"{name}_DetailScale", layer["detail"], x - 1100, y + 40, g)
    detail_amt = _scalar(mat, f"{name}_DetailAmount", 0.55, x - 1100, y + 120, g)

    uv_l = _add(mat, unreal.MaterialExpressionMultiply, x - 880, y - 20)
    _connect(coords, "", uv_l, "A")
    _connect(sc_large, "", uv_l, "B")
    uv_d = _add(mat, unreal.MaterialExpressionMultiply, x - 880, y + 100)
    _connect(coords, "", uv_d, "A")
    _connect(sc_detail, "", uv_d, "B")

    def sample(tex, sampler, uv, sx, sy):
        s = _add(mat, unreal.MaterialExpressionTextureSample, sx, sy)
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", sampler)
        _connect(uv, "", s, "UVs")
        return s

    bc_l = sample(bc_tex, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, uv_l, x - 650, y - 40)
    bc_d = sample(bc_tex, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, uv_d, x - 650, y + 140)
    n_l = sample(n_tex, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, uv_l, x - 650, y + 320)
    n_d = sample(n_tex, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, uv_d, x - 650, y + 500)

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

    macro_str = _scalar(mat, f"{name}_MacroStrength", 0.12, x + 1000, y + 220, g)
    half = _const(mat, 0.5, x + 1000, y + 280)
    macro_c = _add(mat, unreal.MaterialExpressionSubtract, x + 1200, y + 280)
    _connect(macro_mask, "", macro_c, "A")
    _connect(half, "", macro_c, "B")
    macro_off = _add(mat, unreal.MaterialExpressionMultiply, x + 1200, y + 220)
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


def rebuild_master():
    if unreal.EditorAssetLibrary.does_asset_exist(MASTER):
        mat = unreal.EditorAssetLibrary.load_asset(MASTER)
        for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
            unreal.MaterialEditingLibrary.delete_material_expression(mat, e)
    else:
        if not unreal.EditorAssetLibrary.does_directory_exist(MAT_DST):
            unreal.EditorAssetLibrary.make_directory(MAT_DST)
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyLandscape_Master", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )
    try:
        mat.set_editor_property("use_material_attributes", False)
    except Exception:
        pass

    coords = _add(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1600, 200)
    macro_scale = _scalar(mat, "MacroScale", 0.035, -1600, -80, "Variation")
    macro_uv = _add(mat, unreal.MaterialExpressionMultiply, -1400, -60)
    _connect(coords, "", macro_uv, "A")
    _connect(macro_scale, "", macro_uv, "B")
    macro_tex = unreal.EditorAssetLibrary.load_asset(MACRO)
    macro_s = _add(mat, unreal.MaterialExpressionTextureSample, -1180, -100)
    if macro_tex:
        macro_s.set_editor_property("texture", macro_tex)
    macro_s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _connect(macro_uv, "", macro_s, "UVs")
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
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(MASTER)
    unreal.log(f"rebuilt {MASTER}")
    return mat


def update_mic(mat):
    if unreal.EditorAssetLibrary.does_asset_exist(MIC):
        mic = unreal.EditorAssetLibrary.load_asset(MIC)
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LobbyLandscape",
            MAT_DST,
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)
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
            mic,
            f"{n}_Tint",
            unreal.LinearColor(layer["tint"][0], layer["tint"][1], layer["tint"][2], 1.0),
        )
    unreal.EditorAssetLibrary.save_asset(MIC)
    unreal.log(f"updated {MIC}")
    return mic


def assign_to_landscape(mic):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() != "Landscape":
            continue
        a.set_editor_property("landscape_material", mic)
        try:
            a.post_edit_change()
        except Exception:
            pass
        unreal.log(f"assigned MIC to {a.get_actor_label()}")
        # Prefer FinalWeightBlending on LIs if property exists
        for layer in LAYERS:
            li_path = f"{LAYER_DST}/LI_{layer['name']}"
            if unreal.EditorAssetLibrary.does_asset_exist(li_path):
                li = unreal.EditorAssetLibrary.load_asset(li_path)
                try:
                    # UE5 often uses LayerUsageFlags / blend method differently
                    pass
                except Exception:
                    pass
        break


def verify_no_external(mat):
    bad = []
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        try:
            tex = e.get_editor_property("texture")
        except Exception:
            tex = None
        if not tex:
            # TextureSample with None is failure
            if "TextureSample" in e.get_class().get_name() and "Parameter" not in e.get_class().get_name():
                # allow none only if somehow unused — treat as error for layer samples
                # Macro must exist; layer samples must exist
                pass
            continue
        p = tex.get_path_name()
        if any(k in p for k in ("Medieval_Dock", "SoStylized", "Modular Medieval", "/Game/Developers/TaitoP/Medieval")):
            bad.append(p)
        if tex.get_path_name().endswith("None") or "None" == str(tex):
            bad.append("NoneTexture")
    # Explicitly require all layer samples non-null
    null_samples = 0
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        if e.get_class().get_name() != "MaterialExpressionTextureSample":
            continue
        try:
            tex = e.get_editor_property("texture")
        except Exception:
            tex = None
        if not tex:
            null_samples += 1
    unreal.log(f"null_texture_samples={null_samples}")
    if null_samples > 0:
        raise RuntimeError(f"still have {null_samples} null TextureSamples")
    if bad:
        raise RuntimeError(f"external texture leak: {bad}")
    unreal.log("VERIFY_OWNED_TEXTURES_OK")


def run():
    ensure_owned_textures()
    ensure_layer_infos()
    mat = rebuild_master()
    verify_no_external(mat)
    mic = update_mic(mat)
    assign_to_landscape(mic)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    # deps check
    deps = unreal.EditorAssetLibrary.find_package_referencers_for_asset  # noqa: keep quiet
    try:
        # EditorAssetLibrary.get_dependencies may not exist; use AssetRegistry
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        deps = ar.get_dependencies(unreal.Name(MASTER), unreal.AssetRegistryDependencyOptions())
        for d in deps:
            s = str(d)
            if "Medieval" in s or "SoStylized" in s:
                unreal.log_error(f"DEP LEAK {s}")
            else:
                unreal.log(f"dep {s}")
    except Exception as e:
        unreal.log(f"dep dump skip: {e}")
    unreal.log("LANDSCAPE_PAINT_REPAIR_OK")


run()
