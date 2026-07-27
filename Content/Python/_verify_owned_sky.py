"""Deep-verify Owned sky has zero SoStylized package deps; capture status."""
from __future__ import annotations

import unreal

OWNED_ROOT = "/Game/Developers/TaitoP/Environment/Sky/Owned"
OWNED_BP = f"{OWNED_ROOT}/Environment/Sky/BP_StylizedSky"

ar = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions()
for prop, val in (
    ("include_soft_package_references", True),
    ("include_hard_package_references", True),
    ("include_soft_path_references", True),
):
    try:
        opts.set_editor_property(prop, val)
    except Exception:
        pass


def collect(root):
    seen = set()
    stack = [root]
    while stack:
        p = stack.pop()
        if p in seen:
            continue
        seen.add(p)
        try:
            deps = ar.get_dependencies(unreal.Name(p), opts) or []
        except Exception:
            continue
        for d in deps:
            stack.append(str(d))
    return seen


deps = collect(OWNED_BP)
leaks = sorted(d for d in deps if "SoStylized" in d)
unreal.log(f"OWNED_TRANSITIVE={len(deps)} LEAKS={len(leaks)}")
for L in leaks[:50]:
    unreal.log(f"LEAK {L}")

# Actor check
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    lab = a.get_actor_label() or ""
    cls = a.get_class().get_name()
    if "Sky" in lab or "Stylized" in cls:
        path = a.get_path_name()
        unreal.log(f"ACTOR {lab} | {cls} | {path}")
        if "LobbyStylized" in lab:
            # lighting snapshot
            for cc, tag in (
                (unreal.DirectionalLightComponent, "Dir"),
                (unreal.SkyLightComponent, "SkyL"),
                (unreal.ExponentialHeightFogComponent, "Fog"),
                (unreal.PostProcessComponent, "PP"),
            ):
                c = a.get_component_by_class(cc)
                if not c:
                    continue
                try:
                    if tag == "Dir":
                        unreal.log(f"  Dir intensity={c.get_editor_property('intensity')} color={c.get_editor_property('light_color')}")
                    elif tag == "SkyL":
                        cube = c.get_editor_property("cubemap")
                        unreal.log(
                            f"  SkyL intensity={c.get_editor_property('intensity')} cubemap={cube.get_path_name() if cube else None}"
                        )
                    elif tag == "Fog":
                        unreal.log(
                            f"  Fog density={c.get_editor_property('fog_density')} vol={c.get_editor_property('enable_volumetric_fog')}"
                        )
                    elif tag == "PP":
                        s = c.get_editor_property("settings")
                        unreal.log(f"  PP bloom={s.get_editor_property('bloom_intensity')} bias={s.get_editor_property('auto_exposure_bias')}")
                except Exception as e:
                    unreal.log(f"  {tag} err {e}")

# Key owned textures exist
for p in (
    f"{OWNED_ROOT}/Environment/Sky/Textures/T_Skylight_HDR",
    f"{OWNED_ROOT}/Environment/Sky/Textures/BackgroundClouds/T_BackroundClouds1A",
    f"{OWNED_ROOT}/Environment/Sky/Textures/CloudLayers/T_CloudLayer01",
    f"{OWNED_ROOT}/Environment/Sky/Textures/Space/T_Sun01_BC",
    f"{OWNED_ROOT}/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    f"{OWNED_ROOT}/Environment/MPC_GlobalEnvironment",
):
    unreal.log(f"{'OK' if unreal.EditorAssetLibrary.does_asset_exist(p) else 'MISS'} {p}")

unreal.log("DEEP_VERIFY_OK" if not leaks else "DEEP_VERIFY_LEAKS")
