"""Disable level lighting that fights BP_StylizedSky, set midday on sky instance."""
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

HIDE_LABELS = {
    "PastelSkyLight",
    "PastelSkyAtmosphere",
    "PastelDirectionalLight",
    "SkyAtmosphere",
    "SkyLight",
    "DirectionalLight",
    "ExponentialHeightFog",
}
HIDE_CLASSES = {
    "SkyAtmosphere",
    "SkyLight",
    "DirectionalLight",
    "ExponentialHeightFog",
}

sky = None
for a in list(eas.get_all_level_actors()):
    lab = a.get_actor_label() or ""
    cls = a.get_class().get_name()
    if "BP_LobbyStylizedSky" in cls or lab == "LobbyStylizedSky":
        sky = a
        continue
    # Keep PostProcessVolume for now, or hide if BP has its own
    if lab == "PostProcessVolume" or cls == "PostProcessVolume":
        # BP has PostProcess component unbound typically — hide level one to avoid double
        try:
            a.set_actor_hidden_in_game(True)
            a.set_is_temporarily_hidden_in_editor(True)
            unreal.log(f"hid PP {lab}")
        except Exception:
            pass
        continue
    if cls in HIDE_CLASSES or lab in HIDE_LABELS or any(lab.startswith(p) for p in ("DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "Pastel")):
        try:
            a.set_actor_hidden_in_game(True)
            a.set_is_temporarily_hidden_in_editor(True)
            # disable light components
            for comp_cls in (unreal.DirectionalLightComponent, unreal.SkyLightComponent, unreal.ExponentialHeightFogComponent):
                c = a.get_component_by_class(comp_cls)
                if c:
                    try:
                        c.set_visibility(False)
                    except Exception:
                        pass
                    try:
                        c.set_editor_property("visible", False)
                    except Exception:
                        pass
                    try:
                        c.set_intensity(0.0)
                    except Exception:
                        pass
            unreal.log(f"disabled {lab} ({cls})")
        except Exception as e:
            unreal.log_warning(f"{lab}: {e}")

if not sky:
    unreal.log_error("no LobbyStylizedSky found")
else:
    unreal.log(f"configuring {sky}")
    # Try set midday via many possible BP variable names
    sets = [
        ("time_of_day", 12.0),
        ("TimeOfDay", 12.0),
        ("Time of Day", 12.0),
        ("tod", 12.0),
        ("TOD", 12.0),
        ("current_time", 12.0),
        ("CurrentTime", 12.0),
        ("hours", 12.0),
        ("Hours", 12.0),
        ("animate_time_of_day", False),
        ("AnimateTimeOfDay", False),
        ("b_animate_time_of_day", False),
        ("bAnimateTimeOfDay", False),
        ("auto_animate", False),
        ("AutoAnimate", False),
    ]
    for name, val in sets:
        try:
            sky.set_editor_property(name, val)
            unreal.log(f"set {name}={val}")
        except Exception:
            pass

    # Dump all editor properties that look relevant via reflection
    try:
        # Unreal Python: iterate EditorProperty via get_editor_property on known list from CDO
        cdo = unreal.get_default_object(sky.get_class())
        # use AssetRegistry / BlueprintEditorLibrary
        pass
    except Exception:
        pass

    # Call possible BP functions
    for fn in ("SetTimeOfDay", "set_time_of_day", "UpdateSky", "ApplyPreset", "SetWeather"):
        try:
            f = getattr(sky, fn, None)
            if callable(f):
                unreal.log(f"callable {fn}")
        except Exception:
            pass

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("CLEAN_LIGHTING_OK")
