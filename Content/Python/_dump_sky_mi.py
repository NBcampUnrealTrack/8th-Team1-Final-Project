import unreal

def dump_mi(path):
    mi = unreal.EditorAssetLibrary.load_asset(path)
    if not mi:
        unreal.log(f"MISS {path}"); return
    unreal.log(f"=== {path} ===")
    try:
        for p in mi.scalar_parameter_values:
            n = str(p.get_editor_property("parameter_info").get_editor_property("name"))
            v = p.get_editor_property("parameter_value")
            low = n.lower()
            if any(k in low for k in ("rain", "ring", "aurora", "nebula", "star", "glow", "strength", "opac", "cloud", "bright")):
                unreal.log(f"  scalar {n}={v}")
    except Exception as e:
        unreal.log(f"scalar err {e}")
    try:
        for p in mi.vector_parameter_values:
            n = str(p.get_editor_property("parameter_info").get_editor_property("name"))
            low = n.lower()
            if any(k in low for k in ("rain", "ring", "aurora", "nebula", "tint", "color")):
                unreal.log(f"  vector {n}={p.get_editor_property('parameter_value')}")
    except Exception as e:
        unreal.log(f"vector err {e}")
    try:
        for p in mi.texture_parameter_values:
            n = str(p.get_editor_property("parameter_info").get_editor_property("name"))
            t = p.get_editor_property("parameter_value")
            unreal.log(f"  tex {n}={t.get_path_name() if t else None}")
    except Exception as e:
        unreal.log(f"tex err {e}")

for p in (
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    "/Game/Developers/TaitoP/Environment/Sky/Owned/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/MI_PlanetRing_Inst",
    "/Game/Developers/TaitoP/Environment/Sky/Owned/Environment/Sky/Materials/MI_PlanetRing_Inst",
    "/Game/Developers/TaitoP/SoStylized/Environment/Sky/Materials/Presets/MI_StylizedFogPP_Classic",
):
    if unreal.EditorAssetLibrary.does_asset_exist(p):
        dump_mi(p)

# disable remaining audio + ensure ring gone on actor
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    if (a.get_actor_label() or "") != "LobbyStylizedSky":
        continue
    for c in a.get_components_by_class(unreal.AudioComponent):
        c.stop(); c.set_active(False, True); c.set_visibility(False, True)
        try: c.set_editor_property("volume_multiplier", 0.0)
        except Exception: pass
        unreal.log(f"audio off {c.get_name()}")
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if "ring" in c.get_name().lower():
            c.set_visibility(False, True); c.set_hidden_in_game(True, True)
            try: c.set_static_mesh(None)
            except Exception: pass
            unreal.log("ring forced off")
    # midday clear
    for name, val in (("start Time", 12.0), ("freezeAllTime?", True), ("dynamicWeather?", False)):
        try: a.set_editor_property(name, val); unreal.log(f"set {name}={val}")
        except Exception: pass

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("DUMP_MI_OK")
