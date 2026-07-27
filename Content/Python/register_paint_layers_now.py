import unreal

unreal.SystemLibrary.execute_console_command(None, "MD.RegisterLobbyPaintLayers")

cls = getattr(unreal, "MD_LandscapeLibrary", None)
unreal.log(f"has MD_LandscapeLibrary={bool(cls)}")
if cls is None:
    cls = unreal.load_class(None, "/Script/MarineDemolition.MD_LandscapeLibrary")
    unreal.log(f"load_class={cls}")

if cls:
    try:
        count = unreal.MD_LandscapeLibrary.register_lobby_landscape_paint_layers(None)
        unreal.log(f"register count={count}")
    except Exception as e:
        unreal.log(f"register err={e}")
        try:
            count = cls.call_method("RegisterLobbyLandscapePaintLayers", (None,))
            unreal.log(f"call_method count={count}")
        except Exception as e2:
            unreal.log(f"call_method err={e2}")

ls = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        ls = a
        break

if ls:
    unreal.log(f"targets={ls.get_target_layer_names()}")
    unreal.log(f"mat={ls.get_editor_property('landscape_material')}")
else:
    unreal.log("no landscape")

# Material compile status
mat = unreal.EditorAssetLibrary.load_asset("/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master")
if mat:
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.log("recompiled master")
