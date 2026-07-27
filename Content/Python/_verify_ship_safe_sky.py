import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sky_actors = []
for a in eas.get_all_level_actors():
    lab = a.get_actor_label() or ""
    cls = a.get_class().get_name()
    path = a.get_path_name()
    blob = lab + "|" + cls + "|" + path
    if any(
        k in blob
        for k in (
            "LobbyOriginal",
            "LobbyStylized",
            "BP_StylizedSky",
            "BP_Lobby",
            "SoStylized",
            "/Owned/",
            "PastelSun",
            "PastelFill",
            "PastelSky",
            "PastelFog",
            "PastelPPV",
            "SkyAtmosphere",
        )
    ):
        sky_actors.append(blob[:200])
unreal.log(f"ACTORS {len(sky_actors)}")
for s in sky_actors:
    unreal.log(s)

ar = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions()
for prop in ("include_soft_package_references", "include_hard_package_references"):
    try:
        opts.set_editor_property(prop, True)
    except Exception:
        pass
paths = [
    "/Game/Developers/TaitoP/Environment/Sky/BP_LobbyOriginalSky",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSky",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalClouds",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSun",
    "/Game/Developers/TaitoP/Environment/Sky/MPC_LobbySky",
]
leaks = []
for p in paths:
    unreal.log(f"EXIST {p.split('/')[-1]} {unreal.EditorAssetLibrary.does_asset_exist(p)}")
    try:
        deps = ar.get_dependencies(unreal.Name(p), opts) or []
    except Exception as e:
        unreal.log(f"DEPERR {p} {e}")
        continue
    for d in deps:
        s = str(d)
        if "SoStylized" in s or "/Owned/" in s:
            leaks.append(f"{p} -> {s}")
        else:
            unreal.log(f"OKDEP {p.split('/')[-1]} -> {s}")
unreal.log(f"LEAKS {len(leaks)}")
for L in leaks:
    unreal.log_warning(f"LEAK {L}")
