"""
Replace expensive WaterBodyOcean/WaterZone with a cheap translucent plane.
Keeps water at pier 2/3 height. Much better for Lobby FPS.
"""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
MAT_DST = "/Game/Developers/TaitoP/Landscape/Materials"
MAT_PATH = f"{MAT_DST}/M_LobbySea_Simple"
MIC_PATH = f"{MAT_DST}/MI_LobbySea_Simple"
SEA_LABEL = "LobbySea"
WATER_Z = 4302.8
# Cover landscape (~1134m) with margin — plane is 100uu default
PLANE_SCALE = 1500.0  # 100 * 1500 = 150000 cm = 1.5km


def destroy_water_actors():
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(sub.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab in (SEA_LABEL, "LobbyWaterZone", "LobbySeaPlane") or cls in (
            "WaterBodyOcean",
            "WaterZone",
            "WaterBrushManager",
            "WaterBodyLake",
            "WaterBodyRiver",
        ):
            sub.destroy_actor(a)
            unreal.log(f"destroyed {lab or cls}")


def ensure_simple_water_material():
    """Cheap translucent pastel sea — no refraction cascade / water info RT."""
    if unreal.EditorAssetLibrary.does_asset_exist(MAT_PATH):
        mat = unreal.EditorAssetLibrary.load_asset(MAT_PATH)
        for e in list(unreal.MaterialEditingLibrary.get_material_expressions(mat)):
            unreal.MaterialEditingLibrary.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_LobbySea_Simple", MAT_DST, unreal.Material, unreal.MaterialFactoryNew()
        )

    # Translucent, unlit-ish cheap water
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    except Exception:
        try:
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODE_TRANSLUCENT)
        except Exception as e:
            unreal.log(f"blend_mode: {e}")
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    except Exception:
        pass
    for prop, val in (
        ("two_sided", True),
        ("b_tangent_space_normal", True),
        ("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING),
    ):
        try:
            mat.set_editor_property(prop, val)
        except Exception:
            pass

    def add(cls, x, y):
        return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)

    def connect(a, ap, b, bp):
        unreal.MaterialEditingLibrary.connect_material_expressions(a, ap, b, bp)

    # Base color pastel cyan
    base = add(unreal.MaterialExpressionVectorParameter, -400, 0)
    base.set_editor_property("parameter_name", "SeaColor")
    base.set_editor_property("default_value", unreal.LinearColor(0.25, 0.55, 0.75, 1.0))

    # Soft opacity
    opac = add(unreal.MaterialExpressionScalarParameter, -400, 200)
    opac.set_editor_property("parameter_name", "Opacity")
    opac.set_editor_property("default_value", 0.72)

    # Slight roughness / specular for wet look (cheap)
    rough = add(unreal.MaterialExpressionScalarParameter, -400, 320)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.15)

    spec = add(unreal.MaterialExpressionScalarParameter, -400, 420)
    spec.set_editor_property("parameter_name", "Specular")
    spec.set_editor_property("default_value", 0.55)

    # Simple animated UV ripple via time * panner on normal optional — skip for FPS
    # Flat normal
    nrm = add(unreal.MaterialExpressionConstant3Vector, -400, 520)
    nrm.set_editor_property("constant", unreal.LinearColor(0.5, 0.5, 1.0, 0.0))

    unreal.MaterialEditingLibrary.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    unreal.MaterialEditingLibrary.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MAT_PATH)

    if unreal.EditorAssetLibrary.does_asset_exist(MIC_PATH):
        mic = unreal.EditorAssetLibrary.load_asset(MIC_PATH)
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LobbySea_Simple", MAT_DST, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        mic, "SeaColor", unreal.LinearColor(0.22, 0.52, 0.72, 1.0)
    )
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "Opacity", 0.70)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "Roughness", 0.18)
    unreal.EditorAssetLibrary.save_asset(MIC_PATH)
    return mic


def spawn_sea_plane(mic):
    plane = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = sub.spawn_actor_from_object(plane, unreal.Vector(0.0, 0.0, WATER_Z), unreal.Rotator(0, 0, 0))
    if not actor:
        raise RuntimeError("failed to spawn plane")
    actor.set_actor_label(SEA_LABEL)
    actor.set_actor_scale3d(unreal.Vector(PLANE_SCALE, PLANE_SCALE, 1.0))

    # Assign material
    smc = actor.get_component_by_class(unreal.StaticMeshComponent)
    if smc:
        smc.set_material(0, mic)
        # No collision needed for visual sea (or keep QueryOnly)
        try:
            smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        except Exception:
            pass
        try:
            smc.set_editor_property("cast_shadow", False)
            smc.set_cast_shadow(False)
        except Exception:
            pass
        try:
            smc.set_mobility(unreal.ComponentMobility.STATIC)
        except Exception:
            pass

    try:
        actor.set_folder_path("Environment")
    except Exception:
        pass
    return actor


def run():
    destroy_water_actors()
    mic = ensure_simple_water_material()
    actor = spawn_sea_plane(mic)
    loc = actor.get_actor_location()
    scale = actor.get_actor_scale3d()
    unreal.log(f"sea plane loc=({loc.x:.1f},{loc.y:.1f},{loc.z:.1f}) scale={scale.x:.0f}")

    # Confirm no water plugin actors left
    left = []
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        cls = a.get_class().get_name()
        if "Water" in cls:
            left.append(f"{a.get_actor_label()}:{cls}")
    unreal.log("remaining_water=" + (", ".join(left) if left else "none"))

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("LIGHT_SEA_OK")


run()
