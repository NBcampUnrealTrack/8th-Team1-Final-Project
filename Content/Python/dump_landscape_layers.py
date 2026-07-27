import unreal

ls = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        ls = a
        break

for name in (
    "editor_layer_settings",
    "target_layers",
    "landscape_material",
    "landscape_layers",
    "editor_layers",
):
    try:
        v = ls.get_editor_property(name)
        unreal.log(f"prop {name}={v}")
    except Exception as e:
        unreal.log(f"prop {name} ERR {e}")

info = ls.get_landscape_info()
unreal.log(f"info={info}")
if info:
    for pname in ("layers", "layer_info_map", "target_layer_settings", "landscape_proxy"):
        try:
            unreal.log(f"info.{pname}={info.get_editor_property(pname)}")
        except Exception as e:
            unreal.log(f"info.{pname} ERR {e}")

names = [m for m in dir(ls) if "layer" in m.lower()]
unreal.log("landscape layer methods: " + ", ".join(names[:50]))
names2 = [m for m in dir(info) if "layer" in m.lower()] if info else []
unreal.log("info layer methods: " + ", ".join(names2[:50]))
