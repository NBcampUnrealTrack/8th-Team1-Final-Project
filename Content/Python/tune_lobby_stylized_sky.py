"""
Tune Lobby stylized sky after first placement:
- lower cloud opacity (pastel, not overcast)
- reorient SunDisc to face world origin (not edge-on)
- mute competing SkyAtmosphere so skydome reads
"""
from __future__ import annotations

import math

import unreal

BP_PATH = "/Game/Developers/TaitoP/Environment/Sky/BP_LobbyStylizedSky"
CLOUD_MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyStylizedClouds"
SKY_MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyStylizedSky"
SUN_MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbySunDisc"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def _look_at_rotator(from_loc: unreal.Vector, to_loc: unreal.Vector) -> unreal.Rotator:
    """Plane default faces +Z; rotate so plane faces toward to_loc."""
    d = unreal.Vector(
        to_loc.x - from_loc.x,
        to_loc.y - from_loc.y,
        to_loc.z - from_loc.z,
    )
    # Normalize
    length = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z) or 1.0
    fx, fy, fz = d.x / length, d.y / length, d.z / length
    # Pitch/Yaw for facing direction (Unreal: X forward). Plane faces +Z by default,
    # so rotate 90 pitch then yaw toward target.
    yaw = math.degrees(math.atan2(fy, fx))
    horiz = math.sqrt(fx * fx + fy * fy) or 1e-6
    pitch = math.degrees(math.atan2(fz, horiz))
    # Plane normal is +Z; point normal toward -look (toward camera/origin):
    # Use rotator that aims +X at target, then roll/pitch plane: Pitch=-90 so +Z -> +X
    return unreal.Rotator(-90.0 + pitch, yaw, 0.0)


def retune_materials():
    # Cloud density via MIC overrides on BP instance is harder; rebuild scalar defaults
    # by creating MICs used by BP components — simpler: edit material params via MIC assets.
    at = unreal.AssetToolsHelpers.get_asset_tools()
    dst = "/Game/Developers/TaitoP/Environment/Sky/Materials"

    def mic(name, parent_path, scalars, vectors):
        path = f"{dst}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            mi = unreal.EditorAssetLibrary.load_asset(path)
        else:
            parent = unreal.EditorAssetLibrary.load_asset(parent_path)
            mi = at.create_asset(name, dst, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            mi.set_editor_property("parent", parent)
        for k, v in scalars.items():
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, k, v)
        for k, v in vectors.items():
            unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                mi, k, unreal.LinearColor(*v)
            )
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
        unreal.log(f"MIC {path}")
        return mi

    sky_mi = mic(
        "MI_LobbyStylizedSky",
        SKY_MAT,
        {"Brightness": 1.35},
        {"SkyTint": (1.02, 1.0, 1.05, 1.0)},
    )
    cloud_mi = mic(
        "MI_LobbyStylizedClouds",
        CLOUD_MAT,
        {"CloudDensity": 0.28, "CloudSpeed": 0.006, "CloudTiling": 1.2, "NoiseSpeed": 0.002},
        {"CloudTint": (1.05, 0.98, 1.02, 1.0)},
    )
    sun_mi = mic(
        "MI_LobbySunDisc",
        SUN_MAT,
        {"SunIntensity": 3.2},
        {"SunColor": (1.0, 0.92, 0.75, 1.0)},
    )
    return sky_mi, cloud_mi, sun_mi


def _set_rel_xform(comp, loc=None, rot=None, scale=None):
    if loc is not None:
        try:
            comp.set_editor_property("relative_location", unreal.Vector(*loc) if not isinstance(loc, unreal.Vector) else loc)
        except Exception:
            pass
    if rot is not None:
        try:
            comp.set_editor_property("relative_rotation", unreal.Rotator(*rot) if not isinstance(rot, unreal.Rotator) else rot)
        except Exception:
            pass
    if scale is not None:
        try:
            comp.set_editor_property("relative_scale3d", unreal.Vector(*scale) if not isinstance(scale, unreal.Vector) else scale)
        except Exception:
            try:
                comp.set_relative_scale3d(unreal.Vector(*scale) if not isinstance(scale, unreal.Vector) else scale)
            except Exception:
                pass


def update_bp_and_instance(sky_mi, cloud_mi, sun_mi):
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    plane = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Plane")
    # Reimport softened cloud mask
    try:
        task = unreal.AssetImportTask()
        task.filename = "E:/Unreal/MarineDemolition/Art/Textures/Sky/T_LobbyClouds_Mask.png"
        task.destination_path = "/Game/Developers/TaitoP/Environment/Sky/Textures"
        task.destination_name = "T_LobbyClouds_Mask"
        task.replace_existing = True
        task.automated = True
        task.save = True
        task.factory = unreal.TextureFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.EditorAssetLibrary.load_asset("/Game/Developers/TaitoP/Environment/Sky/Textures/T_LobbyClouds_Mask")
        if tex:
            tex.set_editor_property("srgb", True)
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
            unreal.log("reimported T_LobbyClouds_Mask")
    except Exception as e:
        unreal.log_warning(f"cloud reimport: {e}")

    # Ensure gradient is color not normal
    grad = unreal.EditorAssetLibrary.load_asset("/Game/Developers/TaitoP/Environment/Sky/Textures/T_LobbySky_Gradient")
    if grad:
        try:
            grad.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            grad.set_editor_property("srgb", True)
            unreal.EditorAssetLibrary.save_loaded_asset(grad)
        except Exception as e:
            unreal.log_warning(f"grad fix: {e}")

    bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    handles = sub.k2_gather_subobject_data_for_blueprint(bp)

    sun_loc = (4200.0, -1800.0, 3200.0)
    sun_rot = _look_at_rotator(unreal.Vector(*sun_loc), unreal.Vector(0.0, 0.0, 800.0))

    for h in handles:
        data = bfl.get_data(h)
        try:
            obj = bfl.get_associated_object(data)
        except Exception:
            obj = bfl.get_object(data)
        if not obj or not isinstance(obj, unreal.StaticMeshComponent):
            continue
        name = obj.get_name()
        if "SkyDome" in name:
            obj.set_static_mesh(sphere)
            obj.set_material(0, sky_mi)
            _set_rel_xform(obj, scale=(5500, 5500, 5500))
            try:
                obj.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            except Exception:
                pass
            try:
                obj.set_cast_shadow(False)
            except Exception:
                pass
        elif "CloudDome" in name:
            obj.set_static_mesh(sphere)
            obj.set_material(0, cloud_mi)
            _set_rel_xform(obj, scale=(5200, 5200, 5200))
            try:
                obj.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            except Exception:
                pass
            try:
                obj.set_cast_shadow(False)
            except Exception:
                pass
        elif "SunDisc" in name:
            obj.set_static_mesh(plane)
            obj.set_material(0, sun_mi)
            _set_rel_xform(obj, loc=sun_loc, rot=sun_rot, scale=(1200, 1200, 1200))
            try:
                obj.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            except Exception:
                pass
            try:
                obj.set_cast_shadow(False)
            except Exception:
                pass
        unreal.log(f"updated template {name}")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(BP_PATH)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab in ("LobbyStylizedSky",) or "BP_LobbyStylizedSky" in cls:
            eas.destroy_actor(a)
        if "SkyAtmosphere" in cls or lab.startswith("SkyAtmosphere"):
            try:
                a.set_actor_hidden_in_game(True)
                a.set_is_temporarily_hidden_in_editor(True)
                unreal.log(f"hid {lab or cls}")
            except Exception as e:
                unreal.log_warning(f"hide atmosphere: {e}")

    spawned = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if spawned:
        spawned.set_actor_label("LobbyStylizedSky")
        try:
            spawned.set_folder_path("Environment/Sky")
        except Exception:
            pass
        unreal.log(f"respawned {spawned}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("TUNE_LOBBY_SKY_OK")


def run():
    sky_mi, cloud_mi, sun_mi = retune_materials()
    update_bp_and_instance(sky_mi, cloud_mi, sun_mi)


run()
