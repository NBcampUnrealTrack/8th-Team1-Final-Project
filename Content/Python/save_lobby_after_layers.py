import unreal

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.EditorAssetLibrary.save_asset("/Game/Developers/TaitoP/Maps/L_Lobby")

ls = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        ls = a
        break

unreal.log(f"FINAL targets={list(ls.get_target_layer_names()) if ls else None}")
unreal.log(f"FINAL mat={ls.get_editor_property('landscape_material') if ls else None}")
