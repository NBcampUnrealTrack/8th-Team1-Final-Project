"""BP_Sky lobby profile — defaults + property lists for harvest/restore."""
from __future__ import annotations

import md_sky_paths as paths

PROFILE_PATH = "Saved/BP_Sky/lobby_profile.json"

# SoftObject keys on SkyController (same as md_sky_paths.PATHS)
SOFT_OBJECT_KEYS = tuple(paths.PATHS.keys())

# Scalar/bool/vector/name props on UMD_StylizedSkyControllerComponent
SKY_CONTROLLER_PROPS = (
    "bFrozenTime",
    "FrozenTimeOfDayHours",
    "bLocalDayCycle",
    "DayLengthSeconds",
    "LocalTimeOfDayHours",
    "OrbitInclinationDegrees",
    "OrbitDegreesPerHour",
    "OrbitOrientationOffsetDegrees",
    "MpcDayLengthSeconds",
    "MpcNightLengthSeconds",
    "MpcDayStartTimeHours",
    "MpcShaderTimeOffsetHours",
    "SunriseHour",
    "SunsetHour",
    "MoonOrbitYawOffsetDegrees",
    "SunVisibilityGlowThreshold",
    "SkyDomeScale",
    "CloudDomeScale",
    "DirectionalLightIntensity",
    "SkyLightIntensity",
    "FogDensity",
    "BloomIntensity",
    "AutoExposureBias",
    "bSuppressLevelSkyActors",
    "MpcShaderSunStrengthScale",
    "MpcTimeOfDayName",
    "MpcDayCycleProgressName",
    "MpcOrbitOrientationName",
    "MpcOrbitInclinationName",
    "MpcSunStrengthName",
    "MpcSunColorName",
    "MpcDayLengthName",
    "MpcNightLengthName",
    "MpcDayStartTimeName",
)

ADAPTER_PROPS = ("bDriveFromGameState",)

# Tuned lobby defaults (reset_bp_sky_clean + fix_bp_sky_authority)
LOBBY_DEFAULTS: dict = {
    "soft_objects": dict(paths.PATHS),
    "sky_controller": {
        "bFrozenTime": True,
        "FrozenTimeOfDayHours": 12.0,
        "bLocalDayCycle": True,
        "DayLengthSeconds": 600.0,
        "LocalTimeOfDayHours": 12.0,
        "OrbitInclinationDegrees": 28.0,
        "OrbitDegreesPerHour": 15.0,
        "OrbitOrientationOffsetDegrees": -90.0,
        "MpcDayLengthSeconds": 600.0,
        "MpcNightLengthSeconds": 480.0,
        "MpcDayStartTimeHours": 0.0,
        "MpcShaderTimeOffsetHours": 12.0,
        "SunriseHour": 6.0,
        "SunsetHour": 18.0,
        "MoonOrbitYawOffsetDegrees": 180.0,
        "SunVisibilityGlowThreshold": 0.08,
        "SkyDomeScale": [1.0, 1.0, 1.0],
        "CloudDomeScale": [40.0, 40.0, 40.0],
        "DirectionalLightIntensity": 10.0,
        "SkyLightIntensity": 1.15,
        "FogDensity": 0.003,
        "BloomIntensity": 5.5,
        "AutoExposureBias": 2.0,
        "bSuppressLevelSkyActors": True,
        "MpcShaderSunStrengthScale": 0.0,
        "MpcTimeOfDayName": "Current Time",
        "MpcDayCycleProgressName": "Day Cycle Progress",
        "MpcOrbitOrientationName": "Orbit Orientation",
        "MpcOrbitInclinationName": "Orbit Inclination",
        "MpcSunStrengthName": "SunStrength",
        "MpcSunColorName": "SunColor",
        "MpcDayLengthName": "Day Length",
        "MpcNightLengthName": "Night Length",
        "MpcDayStartTimeName": "Day Start Time",
    },
    "adapter": {"bDriveFromGameState": False},
    "components": {
        "directional_light": {
            "atmosphere_sun_light": False,
            "intensity": 10.0,
        },
        "skylight": {"intensity": 1.15},
        "height_fog": {
            "fog_density": 0.003,
            "fog_height_falloff": 0.2,
        },
        "mesh": {
            "reverse_culling": True,
        },
    },
    "mpc_midday_hour": 12.0,
}
