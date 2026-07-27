"""Remove broken BP_StylizedSky from L_Lobby; restore SoStylized dome (not Engine Sphere)."""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
BP_SKY = "/Game/Developers/TaitoP/BP_Sky"
SKY_MESH = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome"
SKY_MESH_ALT = "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome"
CLOUD_MESH = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds"
CLOUD_MESH_ALT = "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds"
SKY_MAT = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic"
CLOUD_MAT = "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic"
# Engine Sphere + SoStylized MI = black sky; always use SM_StylizedSkyDome at scale 1.
SKY_SCALE = unreal.Vector(1.0, 1.0, 1.0)
CLOUD_SCALE = unreal.Vector(40.0, 40.0, 40.0)


def load_first(*paths):
    for p in paths:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            return unreal.EditorAssetLibrary.load_asset(p), p
    return None, None


def is_broken_stylized_sky(actor):
    lab = actor.get_actor_label() or ""
    cls = ""
    path = ""
    try:
        cls = actor.get_class().get_name()
        path = actor.get_class().get_path_name()
    except Exception:
        pass
    path_name = ""
    try:
        path_name = actor.get_path_name()
    except Exception:
        pass
    blob = f"{lab}|{cls}|{path}|{path_name}"
    return (
        "BP_StylizedSky" in blob
        or lab in ("LobbyStylizedSky", "StylizedSky")
        or ("SoStylized" in blob and "StylizedSky" in blob)
    )


def run():
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    except Exception as e:
        unreal.log_warning(f"load_level warn: {e}")

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for a in list(eas.get_all_level_actors()):
        if is_broken_stylized_sky(a):
            eas.destroy_actor(a)
            removed += 1
            unreal.log(f"removed broken sky actor: {a}")

    for a in list(eas.get_all_level_actors()):
        try:
            pn = a.get_path_name()
        except Exception:
            continue
        if "BP_StylizedSky_C" in pn:
            eas.destroy_actor(a)
            removed += 1
            unreal.log(f"removed unresolved path actor: {pn}")

    sky_mesh, sky_path = load_first(SKY_MESH, SKY_MESH_ALT)
    cloud_mesh, cloud_path = load_first(CLOUD_MESH, CLOUD_MESH_ALT)
    sky_mat, _ = load_first(SKY_MAT)
    cloud_mat, _ = load_first(CLOUD_MAT)
    unreal.log(f"sky={sky_path} cloud={cloud_path}")

    if unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
        cdo = unreal.get_default_object(bp.generated_class())
        for prop, val in (
            ("bUseFullSphereSkyDome", False),
            ("SkyDomeScale", SKY_SCALE),
            ("CloudDomeScale", CLOUD_SCALE),
            ("SunMeshScale", unreal.Vector(1, 1, 1)),
            ("SunOrbitRadius", 0.0),
            ("bFrozenTime", True),
            ("FrozenTimeOfDayHours", 12.0),
        ):
            try:
                cdo.set_editor_property(prop, val)
                unreal.log(f"set CDO {prop}")
            except Exception as e:
                unreal.log(f"skip CDO {prop}: {e}")
        if sky_mesh:
            try:
                cdo.set_editor_property("SkyDomeMesh", sky_mesh)
            except Exception as e:
                unreal.log(f"SkyDomeMesh: {e}")
        if cloud_mesh:
            try:
                cdo.set_editor_property("CloudDomeMesh", cloud_mesh)
            except Exception as e:
                unreal.log(f"CloudDomeMesh: {e}")
        if sky_mat:
            try:
                cdo.set_editor_property("SkyMaterial", sky_mat)
            except Exception as e:
                unreal.log(f"SkyMaterial: {e}")
        if cloud_mat:
            try:
                cdo.set_editor_property("CloudMaterial", cloud_mat)
            except Exception as e:
                unreal.log(f"CloudMaterial: {e}")
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as e:
            unreal.log(f"compile: {e}")
        unreal.EditorAssetLibrary.save_asset(BP_SKY)

    has_sky = False
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        lab = a.get_actor_label() or ""
        if "BP_Sky" in cls or lab in ("BP_Sky", "MD_StylizedSky"):
            has_sky = True
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                n = c.get_name()
                if "SkyDomeClouds" in n or "Cloud" in n:
                    if cloud_mesh:
                        c.set_static_mesh(cloud_mesh)
                    if cloud_mat:
                        c.set_material(0, cloud_mat)
                    c.set_relative_scale3d(CLOUD_SCALE)
                elif "SkyDome" in n:
                    if sky_mesh:
                        c.set_static_mesh(sky_mesh)
                    if sky_mat:
                        c.set_material(0, sky_mat)
                    c.set_relative_scale3d(SKY_SCALE)
            try:
                a.set_editor_property("bUseFullSphereSkyDome", False)
                a.set_editor_property("SkyDomeScale", SKY_SCALE)
                a.set_editor_property("CloudDomeScale", CLOUD_SCALE)
                if sky_mesh:
                    a.set_editor_property("SkyDomeMesh", sky_mesh)
                if sky_mat:
                    a.set_editor_property("SkyMaterial", sky_mat)
            except Exception as e:
                unreal.log(f"instance props: {e}")
            unreal.log(f"updated instance {lab}/{cls}")
            break

    if not has_sky and unreal.EditorAssetLibrary.does_asset_exist(BP_SKY):
        bp = unreal.EditorAssetLibrary.load_asset(BP_SKY)
        actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        actor.set_actor_label("BP_Sky")
        try:
            actor.set_folder_path("Environment/Sky")
        except Exception:
            pass
        unreal.log(f"spawned {actor}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log(f"FIX_LOBBY_SKY_OK removed={removed}")


run()
