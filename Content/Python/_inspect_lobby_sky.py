import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
found = False
for a in eas.get_all_level_actors():
    lab = a.get_actor_label() or ""
    if "LobbyOriginal" in lab or "Sky" in lab or "Stylized" in lab:
        unreal.log(f"ACTOR {lab} {a.get_class().get_name()} hidden={a.is_hidden_ed()} loc={a.get_actor_location()}")
        for c in a.get_components_by_class(unreal.PrimitiveComponent):
            try:
                vis = c.is_visible()
            except Exception:
                vis = "?"
            mats = []
            if hasattr(c, "get_num_materials"):
                for i in range(c.get_num_materials()):
                    m = c.get_material(i)
                    mats.append(m.get_path_name() if m else None)
            unreal.log(f"  {c.get_name()} vis={vis} mats={mats}")
        found = True
if not found:
    unreal.log("NO_SKY_ACTORS")

# thumbnail-ish: check material emissive connected
for p in (
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSky",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalClouds",
    "/Game/Developers/TaitoP/Environment/Sky/Materials/M_LobbyOriginalSun",
):
    unreal.log(f"MAT_EXIST {p} {unreal.EditorAssetLibrary.does_asset_exist(p)}")

# capture sky light intensity
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") == "LobbyOriginalSky":
        for cc in (unreal.DirectionalLightComponent, unreal.SkyLightComponent, unreal.ExponentialHeightFogComponent):
            c = a.get_component_by_class(cc)
            if c:
                try:
                    unreal.log(f"LIGHT {cc.__name__} inten={c.get_editor_property('intensity')} mob={c.mobility}")
                except Exception as e:
                    unreal.log(f"LIGHT {cc.__name__} {e}")
