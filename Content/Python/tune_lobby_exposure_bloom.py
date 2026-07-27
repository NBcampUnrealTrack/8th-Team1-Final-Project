"""Raise Lobby exposure / bloom for bright anime sky feel."""
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if a.get_class().get_name() != "PostProcessVolume":
        continue
    try:
        a.set_editor_property("unbound", True)
    except Exception:
        pass
    try:
        settings = a.get_editor_property("settings")
        # Exposure
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", 1.1)
        settings.set_editor_property("override_auto_exposure_method", True)
        try:
            settings.set_editor_property(
                "auto_exposure_method",
                unreal.AutoExposureMethod.AEM_MANUAL,
            )
            settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
            settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
        except Exception:
            pass
        # Bloom
        settings.set_editor_property("override_bloom_method", True)
        settings.set_editor_property("override_bloom_intensity", True)
        settings.set_editor_property("bloom_intensity", 0.85)
        settings.set_editor_property("override_bloom_threshold", True)
        settings.set_editor_property("bloom_threshold", 0.6)
        # Saturation / contrast mild
        settings.set_editor_property("override_color_saturation", True)
        settings.set_editor_property(
            "color_saturation", unreal.Vector4(1.12, 1.12, 1.18, 1.0)
        )
        settings.set_editor_property("override_color_contrast", True)
        settings.set_editor_property(
            "color_contrast", unreal.Vector4(1.05, 1.05, 1.05, 1.0)
        )
        a.set_editor_property("settings", settings)
        unreal.log("tuned PostProcessVolume")
    except Exception as e:
        unreal.log_warning(f"pp: {e}")

# Skylight / directional brighter
for a in eas.get_all_level_actors():
    cls = a.get_class().get_name()
    lab = a.get_actor_label() or ""
    if "SkyLight" in cls or "SkyLight" in lab:
        try:
            c = a.get_component_by_class(unreal.SkyLightComponent)
            if c:
                c.set_intensity(4.5)
                unreal.log("skylight 4.5")
        except Exception as e:
            unreal.log(str(e))
    if "DirectionalLight" in cls:
        try:
            c = a.get_component_by_class(unreal.DirectionalLightComponent)
            if c:
                c.set_intensity(10.0)
                c.set_light_color(unreal.LinearColor(1.0, 0.98, 0.92, 1.0))
                unreal.log("dir light 10")
        except Exception as e:
            unreal.log(str(e))

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("EXPOSURE_BLOOM_OK")
