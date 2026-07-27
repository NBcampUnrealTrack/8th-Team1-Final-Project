"""
Pastel Lobby landscape material with paintable layers (Seabed / Plains / Mountain)
plus height+slope auto-weights so sculpted terrain paints itself.

  Remote: setup_lobby_landscape_material.py
"""
from __future__ import annotations

import unreal
from pathlib import Path

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
SRC = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Landscape")
TEX_DST = "/Game/Developers/TaitoP/Landscape/Textures"
MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
LAYER_DST = "/Game/Developers/TaitoP/Landscape/Layers"
MAT_PATH = f"{MAT_DST}/M_LobbyLandscape"

# name, bc file stem, normal stem, UV tiling
LAYERS = [
    ("Seabed", "T_Seabed_BC", "T_Seabed_N", 14.0),
    ("Plains", "T_Plains_BC", "T_Plains_N", 10.0),
    ("Mountain", "T_Mountain_BC", "T_Mountain_N", 7.0),
]


def ensure_dirs():
    for d in (TEX_DST, MAT_DST, LAYER_DST):
        if not unreal.EditorAssetLibrary.does_directory_exist(d):
            unreal.EditorAssetLibrary.make_directory(d)


def import_texture(filename: str, asset_name: str, is_normal: bool):
    src = SRC / filename
    asset_path = f"{TEX_DST}/{asset_name}"
    task = unreal.AssetImportTask()
    task.filename = str(src).replace("\\", "/")
    task.destination_path = TEX_DST
    task.destination_name = asset_name
    task.replace_existing = True
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not tex:
        raise RuntimeError(f"texture import failed: {asset_path}")
    if is_normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def create_layer_info(name: str):
    path = f"{LAYER_DST}/LI_{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        li = unreal.EditorAssetLibrary.load_asset(path)
    else:
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        li = None
        # Try known factory class names
        for factory_name in (
            "LandscapeLayerInfoObjectFactory",
            "LandscapeLayerInfoFactory",
        ):
            if hasattr(unreal, factory_name):
                li = asset_tools.create_asset(
                    f"LI_{name}", LAYER_DST, unreal.LandscapeLayerInfoObject, getattr(unreal, factory_name)()
                )
                break
        if li is None:
            # UE sometimes allows None/empty factory for UObject assets
            try:
                li = asset_tools.create_asset(
                    f"LI_{name}", LAYER_DST, unreal.LandscapeLayerInfoObject, None
                )
            except Exception:
                li = None
        if not li:
            unreal.log_warning(f"Skip LI_{name} creation (no factory) — auto material still works")
            return None

    try:
        li.set_editor_property("layer_name", name)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_asset(path)
    return unreal.EditorAssetLibrary.load_asset(path)


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _connect(a, a_pin, b, b_pin):
    unreal.MaterialEditingLibrary.connect_material_expressions(a, a_pin, b, b_pin)


def build_auto_height_material(textures: dict):
    """
    Height + slope driven pastel blend (no weight paint required).
    Still uses LandscapeLayerCoords for tiling. Sculpt updates look immediately.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(MAT_PATH):
        mat = unreal.EditorAssetLibrary.load_asset(MAT_PATH)
        for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
            unreal.MaterialEditingLibrary.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyLandscape", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )

    coords = _add(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1200, 200)
    world_pos = _add(mat, unreal.MaterialExpressionWorldPosition, -1200, -200)
    try:
        world_pos.set_editor_property(
            "world_position_shader_offset",
            unreal.WorldPositionIncludedOffsets.WPT_DEFAULT,
        )
    except Exception:
        pass

    # Z component
    mask_z = _add(mat, unreal.MaterialExpressionComponentMask, -980, -200)
    mask_z.set_editor_property("r", False)
    mask_z.set_editor_property("g", False)
    mask_z.set_editor_property("b", True)
    mask_z.set_editor_property("a", False)
    _connect(world_pos, "", mask_z, "")

    # Normalize height roughly using ScalarParameters so user can tweak
    z_min = _add(mat, unreal.MaterialExpressionScalarParameter, -980, -40)
    z_min.set_editor_property("parameter_name", "HeightMin")
    z_min.set_editor_property("default_value", -200.0)
    z_max = _add(mat, unreal.MaterialExpressionScalarParameter, -980, 40)
    z_max.set_editor_property("parameter_name", "HeightMax")
    z_max.set_editor_property("default_value", 6000.0)

    sub = _add(mat, unreal.MaterialExpressionSubtract, -760, -120)
    _connect(mask_z, "", sub, "A")
    _connect(z_min, "", sub, "B")
    denom = _add(mat, unreal.MaterialExpressionSubtract, -760, 0)
    _connect(z_max, "", denom, "A")
    _connect(z_min, "", denom, "B")
    div = _add(mat, unreal.MaterialExpressionDivide, -560, -80)
    _connect(sub, "", div, "A")
    _connect(denom, "", div, "B")
    sat = _add(mat, unreal.MaterialExpressionClamp, -400, -80)
    _connect(div, "", sat, "")

    # Soft masks: seabed low, mountain high, plains mid
    sea_end = _add(mat, unreal.MaterialExpressionScalarParameter, -560, 120)
    sea_end.set_editor_property("parameter_name", "SeabedEnd")
    sea_end.set_editor_property("default_value", 0.30)
    mtn_start = _add(mat, unreal.MaterialExpressionScalarParameter, -560, 200)
    mtn_start.set_editor_property("parameter_name", "MountainStart")
    mtn_start.set_editor_property("default_value", 0.58)

    # Mountain weight ~= smoothstep(mtn_start, 1, h)
    # Seabed weight ~= 1 - smoothstep(0, sea_end, h)
    # Use cheap Power approximations with Saturate
    # seabed = saturate(1 - h/sea_end)
    sea_div = _add(mat, unreal.MaterialExpressionDivide, -220, -200)
    _connect(sat, "", sea_div, "A")
    _connect(sea_end, "", sea_div, "B")
    one = _add(mat, unreal.MaterialExpressionConstant, -220, -40)
    one.set_editor_property("r", 1.0)
    sea_w = _add(mat, unreal.MaterialExpressionSubtract, -40, -160)
    _connect(one, "", sea_w, "A")
    _connect(sea_div, "", sea_w, "B")
    sea_w = _add(mat, unreal.MaterialExpressionClamp, 120, -160)
    # reconnect properly - rebuild clamp
    # (recreate cleanly)
    sea_raw = _add(mat, unreal.MaterialExpressionSubtract, -40, -200)
    _connect(one, "", sea_raw, "A")
    _connect(sea_div, "", sea_raw, "B")
    sea_clamp = _add(mat, unreal.MaterialExpressionClamp, 140, -200)
    _connect(sea_raw, "", sea_clamp, "")

    # mountain = saturate((h - mtn_start)/(1-mtn_start))
    mtn_sub = _add(mat, unreal.MaterialExpressionSubtract, -220, 160)
    _connect(sat, "", mtn_sub, "A")
    _connect(mtn_start, "", mtn_sub, "B")
    one_m = _add(mat, unreal.MaterialExpressionConstant, -220, 240)
    one_m.set_editor_property("r", 1.0)
    mtn_den = _add(mat, unreal.MaterialExpressionSubtract, -40, 200)
    _connect(one_m, "", mtn_den, "A")
    _connect(mtn_start, "", mtn_den, "B")
    mtn_div = _add(mat, unreal.MaterialExpressionDivide, 140, 160)
    _connect(mtn_sub, "", mtn_div, "A")
    _connect(mtn_den, "", mtn_div, "B")
    mtn_clamp = _add(mat, unreal.MaterialExpressionClamp, 320, 160)
    _connect(mtn_div, "", mtn_clamp, "")

    # plains = saturate(1 - seabed - mountain)
    sum_sm = _add(mat, unreal.MaterialExpressionAdd, 320, -40)
    _connect(sea_clamp, "", sum_sm, "A")
    _connect(mtn_clamp, "", sum_sm, "B")
    pln_raw = _add(mat, unreal.MaterialExpressionSubtract, 500, -40)
    _connect(one, "", pln_raw, "A")
    _connect(sum_sm, "", pln_raw, "B")
    pln_clamp = _add(mat, unreal.MaterialExpressionClamp, 680, -40)
    _connect(pln_raw, "", pln_clamp, "")

    # Slope boost for mountain (steep faces)
    vnormal = _add(mat, unreal.MaterialExpressionVertexNormalWS, -1200, 480)
    nmask = _add(mat, unreal.MaterialExpressionComponentMask, -980, 480)
    nmask.set_editor_property("r", False)
    nmask.set_editor_property("g", False)
    nmask.set_editor_property("b", True)
    nmask.set_editor_property("a", False)
    _connect(vnormal, "", nmask, "")
    # slope = 1 - abs(Nz) approximated as 1 - Nz (up-facing)
    slope = _add(mat, unreal.MaterialExpressionSubtract, -760, 480)
    _connect(one, "", slope, "A")
    _connect(nmask, "", slope, "B")
    slope_c = _add(mat, unreal.MaterialExpressionClamp, -560, 480)
    _connect(slope, "", slope_c, "")
    slope_pow = _add(mat, unreal.MaterialExpressionPower, -360, 480)
    slope_amt = _add(mat, unreal.MaterialExpressionConstant, -560, 560)
    slope_amt.set_editor_property("r", 1.4)
    _connect(slope_c, "", slope_pow, "Base")
    _connect(slope_amt, "", slope_pow, "Exp")

    mtn_boost = _add(mat, unreal.MaterialExpressionAdd, 500, 200)
    _connect(mtn_clamp, "", mtn_boost, "A")
    _connect(slope_pow, "", mtn_boost, "B")
    mtn_final = _add(mat, unreal.MaterialExpressionClamp, 680, 200)
    _connect(mtn_boost, "", mtn_final, "")

    # Renormalize weights after boost
    sum2 = _add(mat, unreal.MaterialExpressionAdd, 860, 40)
    _connect(sea_clamp, "", sum2, "A")
    _connect(pln_clamp, "", sum2, "B")
    sum3 = _add(mat, unreal.MaterialExpressionAdd, 1040, 80)
    _connect(sum2, "", sum3, "A")
    _connect(mtn_final, "", sum3, "B")

    def norm_w(src_node, y):
        d = _add(mat, unreal.MaterialExpressionDivide, 1220, y)
        _connect(src_node, "", d, "A")
        _connect(sum3, "", d, "B")
        return d

    w_sea = norm_w(sea_clamp, -200)
    w_pln = norm_w(pln_clamp, -40)
    w_mtn = norm_w(mtn_final, 160)

    # Sample textures
    samples_bc = {}
    samples_n = {}
    y = -40
    for name, bc_name, n_name, tiling in LAYERS:
        bc, nrm, _ = textures[name]
        mul = _add(mat, unreal.MaterialExpressionMultiply, -780, y)
        tile = _add(mat, unreal.MaterialExpressionConstant, -980, y + 40)
        tile.set_editor_property("r", float(tiling))
        _connect(coords, "", mul, "A")
        _connect(tile, "", mul, "B")
        s_bc = _add(mat, unreal.MaterialExpressionTextureSample, -520, y)
        s_bc.set_editor_property("texture", bc)
        s_bc.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        _connect(mul, "", s_bc, "UVs")
        s_n = _add(mat, unreal.MaterialExpressionTextureSample, -520, y + 160)
        s_n.set_editor_property("texture", nrm)
        s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        _connect(mul, "", s_n, "UVs")
        samples_bc[name] = s_bc
        samples_n[name] = s_n
        y += 360

    def weighted_sum(samples, weights, base_y):
        # ((a*wa + b*wb) + c*wc)
        m0 = _add(mat, unreal.MaterialExpressionMultiply, 1400, base_y)
        _connect(samples["Seabed"], "", m0, "A")
        _connect(weights[0], "", m0, "B")
        m1 = _add(mat, unreal.MaterialExpressionMultiply, 1400, base_y + 80)
        _connect(samples["Plains"], "", m1, "A")
        _connect(weights[1], "", m1, "B")
        m2 = _add(mat, unreal.MaterialExpressionMultiply, 1400, base_y + 160)
        _connect(samples["Mountain"], "", m2, "A")
        _connect(weights[2], "", m2, "B")
        a01 = _add(mat, unreal.MaterialExpressionAdd, 1600, base_y + 40)
        _connect(m0, "", a01, "A")
        _connect(m1, "", a01, "B")
        a012 = _add(mat, unreal.MaterialExpressionAdd, 1800, base_y + 80)
        _connect(a01, "", a012, "A")
        _connect(m2, "", a012, "B")
        return a012

    bc_out = weighted_sum(samples_bc, (w_sea, w_pln, w_mtn), -120)
    n_out = weighted_sum(samples_n, (w_sea, w_pln, w_mtn), 280)

    metal = _add(mat, unreal.MaterialExpressionConstant, 1800, 400)
    metal.set_editor_property("r", 0.0)
    spec = _add(mat, unreal.MaterialExpressionConstant, 1800, 480)
    spec.set_editor_property("r", 0.04)
    rough = _add(mat, unreal.MaterialExpressionConstant, 1800, 560)
    rough.set_editor_property("r", 0.9)

    unreal.MaterialEditingLibrary.connect_material_property(bc_out, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(n_out, "", unreal.MaterialProperty.MP_NORMAL)
    unreal.MaterialEditingLibrary.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    unreal.MaterialEditingLibrary.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(MAT_PATH)
    return mat


def build_paintable_layer_material(textures: dict):
    """Traditional LandscapeLayerBlend material for manual paint mode."""
    mat_path = f"{MAT_DST}/M_LobbyLandscape_Paint"
    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
        mat = unreal.EditorAssetLibrary.load_asset(mat_path)
        for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
            unreal.MaterialEditingLibrary.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbyLandscape_Paint", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )

    coords = _add(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1000, 200)
    blend_bc = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 200, 0)
    blend_n = _add(mat, unreal.MaterialExpressionLandscapeLayerBlend, 200, 400)

    def make_inputs():
        arr = []
        for name, _, _, _ in LAYERS:
            inp = unreal.LayerBlendInput()
            inp.set_editor_property("layer_name", name)
            inp.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
            inp.set_editor_property("preview_weight", 0.34)
            arr.append(inp)
        return arr

    blend_bc.set_editor_property("layers", make_inputs())
    blend_n.set_editor_property("layers", make_inputs())

    y = 0
    for name, _, _, tiling in LAYERS:
        bc, nrm, _ = textures[name]
        mul = _add(mat, unreal.MaterialExpressionMultiply, -700, y)
        tile = _add(mat, unreal.MaterialExpressionConstant, -900, y)
        tile.set_editor_property("r", float(tiling))
        _connect(coords, "", mul, "A")
        _connect(tile, "", mul, "B")
        s_bc = _add(mat, unreal.MaterialExpressionTextureSample, -480, y)
        s_bc.set_editor_property("texture", bc)
        s_bc.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        _connect(mul, "", s_bc, "UVs")
        s_n = _add(mat, unreal.MaterialExpressionTextureSample, -480, y + 160)
        s_n.set_editor_property("texture", nrm)
        s_n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        _connect(mul, "", s_n, "UVs")
        unreal.MaterialEditingLibrary.connect_material_expressions(s_bc, "", blend_bc, f"Layer {name}")
        unreal.MaterialEditingLibrary.connect_material_expressions(s_n, "", blend_n, f"Layer {name}")
        y += 360

    metal = _add(mat, unreal.MaterialExpressionConstant, 480, 80)
    metal.set_editor_property("r", 0.0)
    spec = _add(mat, unreal.MaterialExpressionConstant, 480, 160)
    spec.set_editor_property("r", 0.04)
    rough = _add(mat, unreal.MaterialExpressionConstant, 480, 240)
    rough.set_editor_property("r", 0.9)

    unreal.MaterialEditingLibrary.connect_material_property(blend_bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(blend_n, "", unreal.MaterialProperty.MP_NORMAL)
    unreal.MaterialEditingLibrary.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    unreal.MaterialEditingLibrary.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(mat_path)
    return mat


def find_landscape():
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            return a
    return None


def paint_weights_via_render_targets(landscape, layer_infos):
    """Best-effort layer registration. Auto height material is primary."""
    try:
        fmt = unreal.TextureRenderTargetFormat.RTF_RGBA16F
    except Exception:
        try:
            fmt = unreal.TextureRenderTargetFormat.RTF_RGBA8
        except Exception:
            unreal.log_warning("No RT format for weight paint — skipping")
            return False

    try:
        rt_h = unreal.RenderingLibrary.create_render_target2d(
            None, 505, 505, fmt, unreal.LinearColor(0, 0, 0, 1), False
        )
        if not landscape.landscape_export_heightmap_to_render_target(rt_h, False, True):
            unreal.log_warning("Height export to RT failed — skip weight import")
            return False
    except Exception as exc:
        unreal.log_warning(f"Height export skipped: {exc}")
        return False

    for name, li in layer_infos.items():
        try:
            rt_w = unreal.RenderingLibrary.create_render_target2d(
                None, 505, 505, unreal.TextureRenderTargetFormat.RTF_R8, unreal.LinearColor(0.01, 0, 0, 1), False
            )
            unreal.RenderingLibrary.clear_render_target2d(None, rt_w, unreal.LinearColor(0.01, 0, 0, 1))
            ok = landscape.landscape_import_weightmap_from_render_target(rt_w, name)
            unreal.log(f"Import weight {name}: {ok}")
        except Exception as exc:
            unreal.log_warning(f"Weight import {name} failed: {exc}")
    return True


def run():
    ensure_dirs()
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    # Persist current sculpted level first
    unreal.EditorLevelLibrary.save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("Saved current L_Lobby (sculpted landscape)")

    textures = {}
    for name, bc, nrm, tiling in LAYERS:
        textures[name] = (
            import_texture(f"{bc}.png", bc, False),
            import_texture(f"{nrm}.png", nrm, True),
            tiling,
        )

    # Auto height/slope material (primary — paints itself from sculpt)
    mat_auto = build_auto_height_material(textures)
    # Paintable companion for Landscape Paint mode
    mat_paint = build_paintable_layer_material(textures)

    landscape = find_landscape()
    if not landscape:
        raise RuntimeError("Landscape not found in L_Lobby")

    landscape.set_editor_property("landscape_material", mat_auto)
    try:
        landscape.post_edit_change()
    except Exception:
        pass

    # Try create layer infos for paint material workflow (best-effort)
    layer_infos = {}
    for name, _, _, _ in LAYERS:
        try:
            layer_infos[name] = create_layer_info(name)
        except Exception as exc:
            unreal.log_warning(f"LayerInfo {name}: {exc}")

    if len(layer_infos) == 3:
        paint_weights_via_render_targets(landscape, layer_infos)

    try:
        landscape.set_actor_label("LobbyLandscape")
        landscape.set_folder_path("Terrain")
    except Exception:
        pass

    unreal.EditorLevelLibrary.save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.EditorAssetLibrary.save_directory("/Game/Developers/TaitoP/Landscape")
    unreal.log(
        "SUCCESS: M_LobbyLandscape (auto height/slope pastel) assigned; "
        "M_LobbyLandscape_Paint ready for manual Landscape paint mode"
    )


run()
