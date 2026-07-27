"""
Create original Lobby stylized sky (SoStylized architecture as reference only):
  Textures -> Materials -> BP_LobbyStylizedSky -> spawn in L_Lobby
Does NOT use any SoStylized assets.
"""
from __future__ import annotations

from pathlib import Path

import unreal

SRC = Path(r"E:/Unreal/MarineDemolition/Art/Textures/Sky")
TEX_DST = "/Game/Developers/TaitoP/Environment/Sky/Textures"
MAT_DST = "/Game/Developers/TaitoP/Environment/Sky/Materials"
BP_DST = "/Game/Developers/TaitoP/Environment/Sky"
BP_NAME = "BP_LobbyStylizedSky"
BP_PATH = f"{BP_DST}/{BP_NAME}"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

TEX_FILES = [
    ("T_LobbySky_Gradient.png", "T_LobbySky_Gradient", False, False),
    ("T_LobbyClouds_Mask.png", "T_LobbyClouds_Mask", False, False),  # RGBA color+alpha
    ("T_LobbyClouds_Noise.png", "T_LobbyClouds_Noise", False, True),
    ("T_LobbySun_Disc.png", "T_LobbySun_Disc", False, False),
]


def ensure_dirs():
    for d in (TEX_DST, MAT_DST, BP_DST):
        if not unreal.EditorAssetLibrary.does_directory_exist(d):
            unreal.EditorAssetLibrary.make_directory(d)


def import_texture(src: Path, name: str, is_normal: bool, linear: bool):
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
        raise RuntimeError(f"failed import {name}")
    if is_normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif linear:
        try:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        except Exception:
            pass
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("srgb", True)
    # Soft sky maps — no mip sharpening issues
    try:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    unreal.log(f"tex {name}")
    return tex


def _add(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def _clear(mat):
    for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
        unreal.MaterialEditingLibrary.delete_material_expression(mat, e)


def _get_or_create_mat(name: str):
    path = f"{MAT_DST}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.EditorAssetLibrary.load_asset(path)
        _clear(mat)
        return mat, path
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
    )
    return mat, path


def build_sky_material(grad):
    mat, path = _get_or_create_mat("M_LobbyStylizedSky")
    # Unlit skydome
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    except Exception:
        pass
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    except Exception:
        pass
    for prop, val in (("two_sided", True),):
        try:
            mat.set_editor_property(prop, val)
        except Exception:
            pass

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    # Sample gradient by V (sky height). Use UV.y
    # ComponentMask G from UV via TextureCoordinate -> Break? Use Custom or Mask on UV
    # TextureSample with UVs; for vertical gradient map, U doesn't matter much.
    s = _add(mat, unreal.MaterialExpressionTextureSample, -400, 0)
    s.set_editor_property("texture", grad)
    s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", s, "UVs")

    tint = _add(mat, unreal.MaterialExpressionVectorParameter, -100, -120)
    tint.set_editor_property("parameter_name", "SkyTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    mul = _add(mat, unreal.MaterialExpressionMultiply, 120, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(s, "", mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", mul, "B")

    bright = _add(mat, unreal.MaterialExpressionScalarParameter, -100, 80)
    bright.set_editor_property("parameter_name", "Brightness")
    bright.set_editor_property("default_value", 1.15)
    mul2 = _add(mat, unreal.MaterialExpressionMultiply, 320, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", mul2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(bright, "", mul2, "B")

    # Emissive for unlit sky
    unreal.MaterialEditingLibrary.connect_material_property(mul2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Also base color for some paths
    unreal.MaterialEditingLibrary.connect_material_property(mul2, "", unreal.MaterialProperty.MP_BASE_COLOR)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def build_cloud_material(mask, noise):
    mat, path = _get_or_create_mat("M_LobbyStylizedClouds")
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    except Exception:
        pass
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    except Exception:
        pass
    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass
    try:
        mat.set_editor_property(
            "translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL,
        )
    except Exception:
        pass

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    speed = _add(mat, unreal.MaterialExpressionScalarParameter, -900, 120)
    speed.set_editor_property("parameter_name", "CloudSpeed")
    speed.set_editor_property("default_value", 0.008)
    time = _add(mat, unreal.MaterialExpressionTime, -900, 200)
    tmul = _add(mat, unreal.MaterialExpressionMultiply, -700, 140)
    unreal.MaterialEditingLibrary.connect_material_expressions(time, "", tmul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(speed, "", tmul, "B")
    # Panner
    pan = _add(mat, unreal.MaterialExpressionPanner, -480, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", pan, "Coordinate")
    unreal.MaterialEditingLibrary.connect_material_expressions(tmul, "", pan, "Time")

    tiling = _add(mat, unreal.MaterialExpressionScalarParameter, -900, 280)
    tiling.set_editor_property("parameter_name", "CloudTiling")
    tiling.set_editor_property("default_value", 1.5)
    uv_scaled = _add(mat, unreal.MaterialExpressionMultiply, -700, 40)
    # Panner output * tiling — connect pan to A, tiling to B via multiply on Coordinate path:
    # Better: multiply UV first then pan. Rebuild: UV*tiling -> pan coordinate
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", uv_scaled, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tiling, "", uv_scaled, "B")
    pan2 = _add(mat, unreal.MaterialExpressionPanner, -480, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv_scaled, "", pan2, "Coordinate")
    unreal.MaterialEditingLibrary.connect_material_expressions(tmul, "", pan2, "Time")

    s_mask = _add(mat, unreal.MaterialExpressionTextureSample, -220, 0)
    s_mask.set_editor_property("texture", mask)
    s_mask.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(pan2, "", s_mask, "UVs")

    s_noise = _add(mat, unreal.MaterialExpressionTextureSample, -220, 320)
    s_noise.set_editor_property("texture", noise)
    s_noise.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    # slower secondary pan
    speed2 = _add(mat, unreal.MaterialExpressionScalarParameter, -900, 400)
    speed2.set_editor_property("parameter_name", "NoiseSpeed")
    speed2.set_editor_property("default_value", 0.003)
    tmul2 = _add(mat, unreal.MaterialExpressionMultiply, -700, 380)
    unreal.MaterialEditingLibrary.connect_material_expressions(time, "", tmul2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(speed2, "", tmul2, "B")
    pan_n = _add(mat, unreal.MaterialExpressionPanner, -480, 360)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv_scaled, "", pan_n, "Coordinate")
    unreal.MaterialEditingLibrary.connect_material_expressions(tmul2, "", pan_n, "Time")
    unreal.MaterialEditingLibrary.connect_material_expressions(pan_n, "", s_noise, "UVs")

    # Color * soft noise variation
    tint = _add(mat, unreal.MaterialExpressionVectorParameter, 40, -80)
    tint.set_editor_property("parameter_name", "CloudTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 0.96, 0.98, 1.0))
    col = _add(mat, unreal.MaterialExpressionMultiply, 260, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_mask, "", col, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", col, "B")

    dens = _add(mat, unreal.MaterialExpressionScalarParameter, 40, 360)
    dens.set_editor_property("parameter_name", "CloudDensity")
    dens.set_editor_property("default_value", 0.85)
    # opacity = mask.a * density * (0.7 + 0.3*noise)
    n_scaled = _add(mat, unreal.MaterialExpressionMultiply, 40, 440)
    n_const = _add(mat, unreal.MaterialExpressionConstant, -40, 500)
    n_const.set_editor_property("r", 0.3)
    base = _add(mat, unreal.MaterialExpressionConstant, -40, 560)
    base.set_editor_property("r", 0.7)
    n_term = _add(mat, unreal.MaterialExpressionMultiply, 200, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_noise, "R", n_term, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(n_const, "", n_term, "B")
    n_add = _add(mat, unreal.MaterialExpressionAdd, 360, 420)
    unreal.MaterialEditingLibrary.connect_material_expressions(base, "", n_add, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(n_term, "", n_add, "B")
    opac = _add(mat, unreal.MaterialExpressionMultiply, 520, 280)
    unreal.MaterialEditingLibrary.connect_material_expressions(s_mask, "A", opac, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(dens, "", opac, "B")
    opac2 = _add(mat, unreal.MaterialExpressionMultiply, 700, 280)
    unreal.MaterialEditingLibrary.connect_material_expressions(opac, "", opac2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(n_add, "", opac2, "B")

    unreal.MaterialEditingLibrary.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opac2, "", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def build_sun_material(sun_tex):
    mat, path = _get_or_create_mat("M_LobbySunDisc")
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    except Exception:
        pass
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    except Exception:
        pass
    try:
        mat.set_editor_property("two_sided", True)
    except Exception:
        pass

    uv = _add(mat, unreal.MaterialExpressionTextureCoordinate, -500, 0)
    s = _add(mat, unreal.MaterialExpressionTextureSample, -250, 0)
    s.set_editor_property("texture", sun_tex)
    s.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", s, "UVs")

    tint = _add(mat, unreal.MaterialExpressionVectorParameter, 0, -100)
    tint.set_editor_property("parameter_name", "SunColor")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 0.9, 0.7, 1.0))
    inten = _add(mat, unreal.MaterialExpressionScalarParameter, 0, 80)
    inten.set_editor_property("parameter_name", "SunIntensity")
    inten.set_editor_property("default_value", 2.5)
    mul = _add(mat, unreal.MaterialExpressionMultiply, 220, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(s, "", mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tint, "", mul, "B")
    mul2 = _add(mat, unreal.MaterialExpressionMultiply, 400, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(mul, "", mul2, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inten, "", mul2, "B")

    unreal.MaterialEditingLibrary.connect_material_property(mul2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(s, "A", unreal.MaterialProperty.MP_OPACITY)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.MaterialEditingLibrary.layout_material_expressions(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f"mat {path}")
    return mat


def _get_subobject_subsystem():
    # UE5: SubobjectDataSubsystem is an Engine subsystem, not EditorSubsystem
    try:
        return unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    except Exception:
        pass
    try:
        return unreal.get_default_object(unreal.SubobjectDataSubsystem)
    except Exception:
        pass
    return None


def _configure_sky_smc(comp, mesh, mat, scale, loc=(0, 0, 0), rot=(0, 0, 0)):
    if not comp:
        return
    try:
        comp.set_static_mesh(mesh)
    except Exception:
        try:
            comp.set_editor_property("static_mesh", mesh)
        except Exception:
            pass
    try:
        comp.set_material(0, mat)
    except Exception:
        pass
    try:
        comp.set_relative_scale3d(unreal.Vector(*scale))
    except Exception:
        pass
    try:
        comp.set_relative_location(unreal.Vector(*loc))
    except Exception:
        pass
    try:
        comp.set_relative_rotation(unreal.Rotator(*rot))
    except Exception:
        pass
    try:
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    except Exception:
        pass
    try:
        comp.set_cast_shadow(False)
    except Exception:
        pass
    try:
        comp.set_editor_property("cast_shadow", False)
    except Exception:
        pass


def populate_blueprint(bp, sky_mat, cloud_mat, sun_mat):
    """Add SkyDome / CloudDome / SunDisc components to BP. Returns True on success."""
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    plane = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")
    if not sphere or not plane:
        raise RuntimeError("engine basic shapes missing")

    sub = _get_subobject_subsystem()
    if not sub:
        raise RuntimeError("SubobjectDataSubsystem unavailable")

    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    handles = sub.k2_gather_subobject_data_for_blueprint(bp)
    if not handles:
        raise RuntimeError("no subobject handles")
    root = handles[0]

    def add_sm(name, mesh, mat, scale, loc=(0, 0, 0), rot=(0, 0, 0)):
        params = unreal.AddNewSubobjectParams(
            parent_handle=root,
            new_class=unreal.StaticMeshComponent,
            blueprint_context=bp,
        )
        try:
            new_handle, fail = sub.add_new_subobject(params)
        except TypeError:
            new_handle = sub.add_new_subobject(params)
            fail = ""
        fail_s = str(fail) if fail is not None else ""
        if fail_s and fail_s not in ("", "None"):
            unreal.log_warning(f"add_subobject fail {name}: {fail_s}")
        try:
            sub.rename_subobject(new_handle, unreal.Text(name))
        except Exception:
            try:
                sub.rename_subobject(handle=new_handle, new_name=unreal.Text(name))
            except Exception as e:
                unreal.log(f"rename {name}: {e}")
        try:
            sub.attach_subobject(owner_handle=root, child_to_add_handle=new_handle)
        except Exception:
            try:
                sub.attach_subobject(root, new_handle)
            except Exception:
                pass

        comp = None
        try:
            data = bfl.get_data(new_handle)
            comp = bfl.get_object(data)
        except Exception:
            try:
                comp = bfl.get_object(new_handle)
            except Exception as e:
                unreal.log_warning(f"get_object {name}: {e}")
        _configure_sky_smc(comp, mesh, mat, scale, loc, rot)
        return new_handle

    add_sm("SkyDome", sphere, sky_mat, (5000.0, 5000.0, 5000.0))
    add_sm("CloudDome", sphere, cloud_mat, (4800.0, 4800.0, 4800.0))
    add_sm(
        "SunDisc",
        plane,
        sun_mat,
        (800.0, 800.0, 800.0),
        loc=(3500.0, -2000.0, 2800.0),
        rot=(0.0, 35.0, -25.0),
    )
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)
    # Verify components exist after compile
    handles2 = sub.k2_gather_subobject_data_for_blueprint(bp)
    unreal.log(f"BP subobject count after populate: {len(handles2)}")
    return len(handles2) >= 4


def create_blueprint(sky_mat, cloud_mat, sun_mat):
    """Create BP_LobbyStylizedSky Actor with dome/cloud/sun meshes."""
    if unreal.EditorAssetLibrary.does_asset_exist(BP_PATH):
        unreal.EditorAssetLibrary.delete_asset(BP_PATH)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        BP_NAME, BP_DST, unreal.Blueprint, factory
    )
    if not bp:
        raise RuntimeError("failed to create blueprint")

    try:
        ok = populate_blueprint(bp, sky_mat, cloud_mat, sun_mat)
        if ok:
            unreal.log(f"BP created via SubobjectDataSubsystem {BP_PATH}")
            return bp, True
        unreal.log_warning("BP populate returned incomplete subobjects")
    except Exception as e:
        unreal.log_warning(f"Subobject API failed: {e}")

    unreal.EditorAssetLibrary.save_asset(BP_PATH)
    unreal.log("BP shell saved; will spawn composed actor fallback")
    return bp, False


def spawn_manual_sky(sky_mat, cloud_mat, sun_mat):
    """Level-placed sky composition (fallback when BP has no components)."""
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    plane = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")

    def place_mesh(label, mesh, mat, scale, loc=(0, 0, 0), rot=(0, 0, 0)):
        a = sub.spawn_actor_from_object(mesh, unreal.Vector(*loc), unreal.Rotator(*rot))
        a.set_actor_label(label)
        a.set_actor_scale3d(unreal.Vector(*scale))
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        _configure_sky_smc(smc, mesh, mat, (1, 1, 1))  # scale already on actor
        # Re-apply material after scale path
        if smc:
            try:
                smc.set_material(0, mat)
            except Exception:
                pass
        try:
            a.set_folder_path("Environment/Sky")
        except Exception:
            pass
        return a

    place_mesh("LobbySkyDome", sphere, sky_mat, (5000, 5000, 5000))
    place_mesh("LobbyCloudDome", sphere, cloud_mat, (4800, 4800, 4800))
    place_mesh(
        "LobbySunDisc",
        plane,
        sun_mat,
        (800, 800, 800),
        loc=(3500, -2000, 2800),
        rot=(0, 35, -25),
    )
    unreal.log("spawned manual sky actors")


def spawn_in_level(sky_mat, cloud_mat, sun_mat, bp=None, bp_ok=False):
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(sub.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        if lab in (
            "LobbyStylizedSky",
            "BP_LobbyStylizedSky",
            "LobbySkyDome",
            "LobbyCloudDome",
            "LobbySunDisc",
        ):
            sub.destroy_actor(a)

    spawned = None
    if bp_ok and bp and unreal.EditorAssetLibrary.does_asset_exist(BP_PATH):
        try:
            spawned = sub.spawn_actor_from_object(
                bp, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0, 0, 0)
            )
            if spawned:
                spawned.set_actor_label("LobbyStylizedSky")
                try:
                    spawned.set_folder_path("Environment/Sky")
                except Exception:
                    pass
                unreal.log(f"spawned BP instance {spawned}")
        except Exception as e:
            unreal.log_warning(f"spawn BP failed: {e}")
            spawned = None

    if not spawned:
        spawn_manual_sky(sky_mat, cloud_mat, sun_mat)

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)


def run():
    ensure_dirs()
    tex = {}
    for fname, aname, is_n, linear in TEX_FILES:
        tex[aname] = import_texture(SRC / fname, aname, is_n, linear)

    sky_mat = build_sky_material(tex["T_LobbySky_Gradient"])
    cloud_mat = build_cloud_material(tex["T_LobbyClouds_Mask"], tex["T_LobbyClouds_Noise"])
    sun_mat = build_sun_material(tex["T_LobbySun_Disc"])

    bp, bp_ok = create_blueprint(sky_mat, cloud_mat, sun_mat)
    spawn_in_level(sky_mat, cloud_mat, sun_mat, bp, bp_ok)
    unreal.log("LOBBY_STYLIZED_SKY_OK")


if __name__ == "__main__":
    run()
