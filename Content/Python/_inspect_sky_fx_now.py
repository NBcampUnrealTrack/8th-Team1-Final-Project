import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    lab = a.get_actor_label() or ""
    if lab != "LobbyStylizedSky" and "BP_StylizedSky" not in a.get_class().get_name():
        continue
    unreal.log(f"ACTOR {lab}")
    for c in a.get_components_by_class(unreal.ActorComponent):
        name = c.get_name(); cls = c.get_class().get_name()
        vis = None
        try:
            vis = c.is_visible() if hasattr(c, "is_visible") else c.get_editor_property("visible")
        except Exception:
            pass
        mesh = None
        try:
            m = c.get_editor_property("static_mesh")
            mesh = m.get_name() if m else None
        except Exception:
            pass
        mat = None
        try:
            m0 = c.get_material(0) if hasattr(c, "get_material") else None
            mat = m0.get_path_name() if m0 else None
        except Exception:
            pass
        unreal.log(f"  COMP {name}|{cls}|vis={vis}|mesh={mesh}|mat={mat}")
    for attr in dir(a):
        if attr.startswith("_"):
            continue
        low = attr.lower()
        if any(k in low for k in ("rainbow", "ring", "aurora", "meteor", "shoot", "planet", "sound", "audio", "weather")):
            try:
                unreal.log(f"  PROP {attr}={a.get_editor_property(attr)}")
            except Exception:
                pass

# camera
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
loc, rot = ues.get_level_viewport_camera_info()
unreal.log(f"CAM loc={loc} pitch={rot.pitch} yaw={rot.yaw} roll={rot.roll}")
# force again
ues.set_level_viewport_camera_info(loc, unreal.Rotator(pitch=-6.0, yaw=float(rot.yaw), roll=0.0))
loc2, rot2 = ues.get_level_viewport_camera_info()
unreal.log(f"CAM2 pitch={rot2.pitch} yaw={rot2.yaw} roll={rot2.roll}")
unreal.log("INSPECT_DONE")
