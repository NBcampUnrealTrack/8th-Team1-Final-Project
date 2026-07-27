"""
Apply pastel-tone lighting to L_Lobby.

  py "E:/Unreal/MarineDemolition/Content/Python/setup_lobby_pastel_lighting.py"
"""
import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"


def _lin(r, g, b, a=1.0):
    return unreal.LinearColor(r, g, b, a)


def run():
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)

    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world_actors = list(actor_sub.get_all_level_actors())

    def find_by_label_or_class(label_sub: str, class_name: str):
        for a in world_actors:
            if label_sub.lower() in (a.get_actor_label() or "").lower():
                return a
            if a.get_class().get_name() == class_name:
                return a
        return None

    # --- Directional Light: soft peach sun ---
    sun = find_by_label_or_class("PastelSun", "DirectionalLight") or find_by_label_or_class(
        "DirectionalLight", "DirectionalLight"
    )
    if sun:
        sun.set_actor_label("PastelSun")
        sun.set_actor_rotation(unreal.Rotator(-38.0, 50.0, 0.0), False)
        comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
        if comp:
            comp.set_editor_property("intensity", 4.5)
            comp.set_editor_property("light_color", unreal.Color(255, 214, 210, 255))  # peach pastel
            comp.set_editor_property("indirect_lighting_intensity", 1.2)
            comp.set_editor_property("atmosphere_sun_light", True)
            try:
                comp.set_editor_property("use_temperature", True)
                comp.set_editor_property("temperature", 6500.0)
            except Exception:
                pass
        unreal.log("Configured PastelSun")

    # --- Sky Light: soft lavender fill ---
    sky = find_by_label_or_class("PastelSkyLight", "SkyLight") or find_by_label_or_class("SkyLight", "SkyLight")
    if sky:
        sky.set_actor_label("PastelSkyLight")
        comp = sky.get_component_by_class(unreal.SkyLightComponent)
        if comp:
            comp.set_editor_property("intensity", 1.4)
            comp.set_editor_property("light_color", unreal.Color(220, 228, 255, 255))  # soft blue-lavender
            try:
                comp.set_editor_property("lower_hemisphere_is_black", False)
                comp.set_editor_property("lower_hemisphere_color", _lin(0.95, 0.90, 0.92, 1.0))
            except Exception:
                pass
            # Capture if real-time
            try:
                comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
                comp.recapture_sky()
            except Exception:
                pass
        unreal.log("Configured PastelSkyLight")

    # --- Height Fog: mint / lilac haze ---
    fog = find_by_label_or_class("PastelFog", "ExponentialHeightFog") or find_by_label_or_class(
        "ExponentialHeightFog", "ExponentialHeightFog"
    )
    if fog:
        fog.set_actor_label("PastelFog")
        comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if comp:
            comp.set_editor_property("fog_density", 0.018)
            comp.set_editor_property("fog_height_falloff", 0.15)
            comp.set_editor_property("fog_inscattering_color", _lin(0.78, 0.86, 0.95, 1.0))  # pastel blue
            try:
                comp.set_editor_property("directional_inscattering_color", _lin(1.0, 0.88, 0.90, 1.0))
                comp.set_editor_property("directional_inscattering_exponent", 4.0)
                comp.set_editor_property("directional_inscattering_start_distance", 5000.0)
            except Exception:
                pass
            try:
                comp.set_editor_property("volumetric_fog", True)
                comp.set_editor_property("volumetric_fog_albedo", unreal.Color(230, 220, 235, 255))
                comp.set_editor_property("volumetric_fog_extinction_scale", 0.6)
            except Exception:
                pass
        unreal.log("Configured PastelFog")

    # --- Sky Atmosphere soft pastels ---
    atmo = find_by_label_or_class("PastelSkyAtmosphere", "SkyAtmosphere") or find_by_label_or_class(
        "SkyAtmosphere", "SkyAtmosphere"
    )
    if atmo:
        atmo.set_actor_label("PastelSkyAtmosphere")
        comp = atmo.get_component_by_class(unreal.SkyAtmosphereComponent)
        if comp:
            try:
                # Mie slightly pinkish scatter for soft look
                comp.set_editor_property("mie_scattering_scale", 0.004)
                # Rayleigh a bit softer
                comp.set_editor_property("rayleigh_scattering_scale", 0.033)
            except Exception:
                pass
        unreal.log("Configured PastelSkyAtmosphere")

    # --- Post Process: pastel grade ---
    ppv = find_by_label_or_class("PastelPPV", "PostProcessVolume") or find_by_label_or_class(
        "PostProcessVolume", "PostProcessVolume"
    )
    if ppv:
        ppv.set_actor_label("PastelPPV")
        ppv.set_editor_property("b_unbound", True)
        try:
            settings = ppv.get_editor_property("settings")
            # Soft bloom
            settings.override_bloom_intensity = True
            settings.bloom_intensity = 0.35
            settings.override_bloom_threshold = True
            settings.bloom_threshold = 0.8
            # Exposure
            settings.override_auto_exposure_bias = True
            settings.auto_exposure_bias = 0.35
            # Saturation slightly lifted (pastel punch without neon)
            settings.override_color_saturation = True
            settings.color_saturation = unreal.Vector4(1.05, 1.05, 1.08, 1.0)
            settings.override_color_contrast = True
            settings.color_contrast = unreal.Vector4(0.95, 0.95, 0.98, 1.0)
            settings.override_color_gamma = True
            settings.color_gamma = unreal.Vector4(1.05, 1.02, 1.08, 1.0)
            # Soft shadows lift (pastel)
            settings.override_color_gain = True
            settings.color_gain = unreal.Vector4(1.02, 1.0, 1.04, 1.0)
            # Ambient occlusion soft
            settings.override_ambient_occlusion_intensity = True
            settings.ambient_occlusion_intensity = 0.35
            ppv.set_editor_property("settings", settings)
        except Exception as exc:
            unreal.log_warning(f"PPV settings partial: {exc}")
        unreal.log("Configured PastelPPV")

    # Soft fill light (opposite side) - lavender
    fill = None
    for a in world_actors:
        if a.get_actor_label() == "PastelFill":
            fill = a
            break
    if fill is None:
        fill = actor_sub.spawn_actor_from_class(
            unreal.DirectionalLight, unreal.Vector(0, 0, 400), unreal.Rotator(-25.0, -130.0, 0.0)
        )
        fill.set_actor_label("PastelFill")
    fcomp = fill.get_component_by_class(unreal.DirectionalLightComponent)
    if fcomp:
        fcomp.set_editor_property("intensity", 1.2)
        fcomp.set_editor_property("light_color", unreal.Color(210, 220, 255, 255))
        fcomp.set_editor_property("cast_shadows", False)
        try:
            fcomp.set_editor_property("atmosphere_sun_light", False)
        except Exception:
            pass

    unreal.EditorLevelLibrary.save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("Pastel lighting applied to L_Lobby")


run()
