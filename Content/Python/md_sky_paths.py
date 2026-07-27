"""Canonical shared sky paths — promoted from Developers/TaitoP."""
from __future__ import annotations

BP_SKY = "/Game/Environment/Sky/BP_MD_StylizedSky"
LEVEL_LOBBY = "/Game/Maps/L_Lobby"
LANDSCAPE_LOBBY = "/Game/Environment/Lobby/Landscape"

SKY_ROOT = "/Game/Environment/Sky"

# Legacy Developers paths (redirectors / old scripts)
LEGACY_DEV_ROOT = "/Game/Developers/TaitoP"
LEGACY_BP_SKY = f"{LEGACY_DEV_ROOT}/BP_Sky"
LEGACY_LEVEL = f"{LEGACY_DEV_ROOT}/Maps/L_Lobby"
LEGACY_SKY_ROOT = f"{LEGACY_DEV_ROOT}/Environment/Sky"

# Legacy / reference sources (migrate FROM these)
SRC_PREFIXES = (
    f"{LEGACY_DEV_ROOT}/SoStylized",
    "/Game/SoStylized",
)

DELETABLE_SRC_PREFIX = f"{LEGACY_DEV_ROOT}/SoStylized"

PATHS = {
    "SkyDomeMesh": f"{SKY_ROOT}/Meshes/SM_StylizedSkyDome",
    "CloudDomeMesh": f"{SKY_ROOT}/Meshes/SM_StylizedSkyDome_Clouds",
    "SkyMaterial": f"{SKY_ROOT}/Materials/Presets/MI_StylizedSky_Classic",
    "CloudMaterial": f"{SKY_ROOT}/Materials/Presets/MI_StylizedClouds_Classic",
    "EnvironmentMPC": f"{SKY_ROOT}/MPC_GlobalEnvironment",
    "SkyLightCubemap": f"{SKY_ROOT}/Textures/T_Skylight_HDR",
    "SunColorCurve": f"{SKY_ROOT}/Curves/Curve_SunColor_Classic",
    "SunGlowCurve": f"{SKY_ROOT}/Curves/Curve_SunGlow_Classic",
    "MoonColorCurve": f"{SKY_ROOT}/Curves/Curve_MoonColor_Classic",
    "MoonGlowCurve": f"{SKY_ROOT}/Curves/Curve_MoonGlow_Classic",
}

MPC_PATHS = (PATHS["EnvironmentMPC"],)

FALLBACKS: dict[str, tuple[str, ...]] = {
    "SkyDomeMesh": (
        LEGACY_SKY_ROOT + "/Meshes/SM_StylizedSkyDome",
        f"{SRC_PREFIXES[0]}/Environment/Sky/Meshes/SM_StylizedSkyDome",
        "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome",
    ),
    "CloudDomeMesh": (
        LEGACY_SKY_ROOT + "/Meshes/SM_StylizedSkyDome_Clouds",
        f"{SRC_PREFIXES[0]}/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
        "/Game/SoStylized/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
    ),
    "SkyMaterial": (
        LEGACY_SKY_ROOT + "/Materials/Presets/MI_StylizedSky_Classic",
        f"{SRC_PREFIXES[0]}/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
        "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    ),
    "CloudMaterial": (
        LEGACY_SKY_ROOT + "/Materials/Presets/MI_StylizedClouds_Classic",
        f"{SRC_PREFIXES[0]}/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
        "/Game/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
    ),
    "EnvironmentMPC": (
        LEGACY_SKY_ROOT + "/MPC_GlobalEnvironment",
        f"{SRC_PREFIXES[0]}/Environment/MPC_GlobalEnvironment",
        "/Game/SoStylized/Environment/MPC_GlobalEnvironment",
    ),
    "SkyLightCubemap": (
        LEGACY_SKY_ROOT + "/Textures/T_Skylight_HDR",
        f"{SRC_PREFIXES[0]}/Environment/Sky/Textures/T_Skylight_HDR",
        "/Game/SoStylized/Environment/Sky/Textures/T_Skylight_HDR",
    ),
    "SunColorCurve": (
        LEGACY_SKY_ROOT + "/Curves/Curve_SunColor_Classic",
        f"{SRC_PREFIXES[0]}/Environment/Sky/Curves/Curve_SunColor_Classic",
        "/Game/SoStylized/Environment/Sky/Curves/Curve_SunColor_Classic",
    ),
    "SunGlowCurve": (
        LEGACY_SKY_ROOT + "/Curves/Curve_SunGlow_Classic",
        f"{SRC_PREFIXES[0]}/Environment/Sky/Curves/Curve_SunGlow_Classic",
        "/Game/SoStylized/Environment/Sky/Curves/Curve_SunGlow_Classic",
    ),
    "MoonColorCurve": (
        "/Game/SoStylized/Environment/Sky/Curves/Curve_MoonColor_Classic",
    ),
    "MoonGlowCurve": (
        "/Game/SoStylized/Environment/Sky/Curves/Curve_MoonGlow_Classic",
    ),
}


def candidate_paths(key: str) -> tuple[str, ...]:
    owned = PATHS.get(key)
    if not owned:
        return FALLBACKS.get(key, ())
    out = [owned]
    if owned:
        transitional = owned
        transitional = transitional.replace("StylizedSkyDome_Clouds", "SkyDome_Clouds")
        transitional = transitional.replace("StylizedSkyDome", "SkyDome")
        transitional = transitional.replace("StylizedClouds", "Clouds")
        transitional = transitional.replace("StylizedSky", "Sky")
        if transitional != owned:
            out.append(transitional)
    out.extend(FALLBACKS.get(key, ()))
    return tuple(dict.fromkeys(out))


CLOUD_LAYER_PATHS = (
    f"{SKY_ROOT}/Textures/CloudLayers/T_CloudLayer01",
    f"{SKY_ROOT}/Textures/CloudLayers/T_CloudLayer13",
    f"{SKY_ROOT}/Textures/CloudLayers/T_CloudLayer_Overhead04",
)


def map_src_to_owned(src: str) -> str:
    for prefix in SRC_PREFIXES:
        if not src.startswith(prefix + "/") and src != prefix:
            continue
        tail = src[len(prefix) :].lstrip("/")
        if tail == "Environment/MPC_GlobalEnvironment":
            return f"{SKY_ROOT}/MPC_GlobalEnvironment"
        if tail.startswith("Environment/Sky/"):
            return f"{SKY_ROOT}/{tail[len('Environment/Sky/'):]}"
        if tail.startswith("Effects/"):
            return f"{SKY_ROOT}/{tail}"
        if tail.startswith("Textures/"):
            return f"{SKY_ROOT}/Textures/{tail[len('Textures/'):]}"
        if tail.startswith("Materials/"):
            return f"{SKY_ROOT}/Materials/{tail[len('Materials/'):]}"
        raise ValueError(f"unmapped tail under {prefix}: {tail}")
    raise ValueError(f"not a SoStylized path: {src}")


def owned_to_src_candidates(owned_pkg: str) -> tuple[str, ...]:
    if not owned_pkg.startswith(SKY_ROOT + "/") and owned_pkg != SKY_ROOT:
        return ()
    tail = owned_pkg[len(SKY_ROOT) :].lstrip("/")
    out: list[str] = []
    for prefix in SRC_PREFIXES:
        if tail == "MPC_GlobalEnvironment":
            out.append(f"{prefix}/Environment/MPC_GlobalEnvironment")
        elif tail.startswith(("Materials/", "Textures/", "Effects/")):
            out.append(f"{prefix}/{tail}")
        else:
            out.append(f"{prefix}/Environment/Sky/{tail}")
    return tuple(dict.fromkeys(out))


def is_sostylized_path(path: str) -> bool:
    return any(path.startswith(p + "/") or path == p for p in SRC_PREFIXES)


def is_deletable_sostylized_path(path: str) -> bool:
    return path.startswith(DELETABLE_SRC_PREFIX + "/") or path == DELETABLE_SRC_PREFIX
