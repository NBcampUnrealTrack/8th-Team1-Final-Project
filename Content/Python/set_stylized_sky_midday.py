"""Force BP_LobbyStylizedSky to frozen midday Clear."""
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sky = None
for a in eas.get_all_level_actors():
    if "BP_LobbyStylizedSky" in a.get_class().get_name():
        sky = a
        break
if not sky:
    raise RuntimeError("no sky")

# UE python usually uses snake_case for BP vars with spaces
trials = [
    ("start_time", 12.0),
    ("StartTime", 12.0),
    ("start Time", 12.0),
    ("current_time_of_day", 12.0),
    ("day_cycle_enabled", False),
    ("dayCycleEnabled", False),
    ("freeze_all_time", True),
    ("freezeAllTime", True),
    ("dynamic_weather", False),
    ("dynamicWeather", False),
    ("clouds", True),
    ("cloud_shadows", True),
    ("starting_weather", unreal.Name("Clear")),
    ("StartingWeather", unreal.Name("Clear")),
]
for name, val in trials:
    try:
        sky.set_editor_property(name, val)
        unreal.log(f"OK {name}={val}")
    except Exception as e:
        unreal.log(f"fail {name}: {e}")

# Enum for weather - try E_WeatherStates
try:
    # loaded enum
    enum = unreal.load_object(None, "/Game/Developers/TaitoP/SoStylized/Environment/Sky/E_WeatherStates.E_WeatherStates")
    unreal.log(f"enum={enum}")
except Exception as e:
    unreal.log(f"enum load {e}")

# Read back key props
for name in (
    "start_time",
    "current_time_of_day",
    "day_cycle_enabled",
    "freeze_all_time",
    "dynamic_weather",
    "starting_weather",
    "clouds",
):
    try:
        unreal.log(f"read {name}={sky.get_editor_property(name)}")
    except Exception as e:
        unreal.log(f"read fail {name}: {e}")

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("SET_MIDDAY_OK")
