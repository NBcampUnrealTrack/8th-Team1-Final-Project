"""Tone down BP fog and punch sky brightness for readable bright day."""
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sky = None
for a in eas.get_all_level_actors():
    if "BP_LobbyStylizedSky" in a.get_class().get_name():
        sky = a
        break
if not sky:
    raise RuntimeError("no sky actor")

# Fog: very light coastal haze
fog = sky.get_component_by_class(unreal.ExponentialHeightFogComponent)
if fog:
    try:
        fog.set_fog_density(0.002)
    except Exception:
        pass
    for prop, val in (
        ("fog_density", 0.002),
        ("FogDensity", 0.002),
        ("fog_height_falloff", 0.2),
        ("FogHeightFalloff", 0.2),
        ("fog_max_opacity", 0.35),
        ("FogMaxOpacity", 0.35),
        ("start_distance", 50000.0),
        ("StartDistance", 50000.0),
    ):
        try:
            fog.set_editor_property(prop, val)
            unreal.log(f"fog {prop}={val}")
        except Exception:
            pass

# Directional + skylight
d = None
for c in sky.get_components_by_class(unreal.DirectionalLightComponent):
    d = c
    break
if d:
    try:
        d.set_intensity(14.0)
        d.set_light_color(unreal.LinearColor(1.0, 0.98, 0.9, 1.0))
        unreal.log("dir light up")
    except Exception as e:
        unreal.log(str(e))

sl = sky.get_component_by_class(unreal.SkyLightComponent)
if sl:
    try:
        sl.set_intensity(5.0)
        sl.recapture_sky()
        unreal.log("skylight recapture")
    except Exception as e:
        unreal.log(str(e))

# MIC-less: bump master defaults already baked; create MIC overrides on components
sky_mat = unreal.EditorAssetLibrary.load_asset("/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyStylizedSky")
# raise SkyBrightness via editing expression default
for e in unreal.MaterialEditingLibrary.get_material_expressions(sky_mat):
    try:
        if e.get_editor_property("parameter_name") == "SkyBrightness":
            e.set_editor_property("default_value", 2.4)
        if e.get_editor_property("parameter_name") == "BGCloudsStrength":
            e.set_editor_property("default_value", 0.35)
    except Exception:
        pass
unreal.MaterialEditingLibrary.recompile_material(sky_mat)
unreal.EditorAssetLibrary.save_asset("/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyStylizedSky")

# PP bloom/exposure via component settings
pp = sky.get_component_by_class(unreal.PostProcessComponent)
if pp:
    try:
        try:
            pp.set_editor_property("bEnabled", True)
        except Exception:
            pass
        settings = pp.get_editor_property("settings")
        settings.set_editor_property("override_bloom_intensity", True)
        settings.set_editor_property("bloom_intensity", 1.0)
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", 1.2)
        settings.set_editor_property("override_auto_exposure_method", True)
        try:
            settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
        except Exception:
            pass
        pp.set_editor_property("settings", settings)
        unreal.log("pp tuned")
    except Exception as e:
        unreal.log(f"pp {e}")

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("TONE_DOWN_FOG_OK")
