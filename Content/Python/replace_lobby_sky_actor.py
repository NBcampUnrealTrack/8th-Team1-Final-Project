"""Replace legacy BP_Sky actors in L_Lobby with BP_MD_StylizedSky."""
import unreal

LEVEL = "/Game/Maps/L_Lobby"
NEW_BP = "/Game/Environment/Sky/BP_MD_StylizedSky"

bp_asset = unreal.EditorAssetLibrary.load_asset(NEW_BP)
if not bp_asset:
    unreal.log_error(f"missing {NEW_BP}")
else:
    bp_class = bp_asset.generated_class()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    replaced = 0
    for actor in list(eas.get_all_level_actors()):
        cls = actor.get_class().get_name()
        label = actor.get_actor_label()
        if "BP_MD_StylizedSky" in cls:
            unreal.log(f"already promoted: {label} ({cls})")
            continue
        if "BP_Sky" not in cls and label != "BP_Sky":
            continue
        xform = actor.get_actor_transform()
        folder = actor.get_folder_path()
        tags = list(actor.tags)
        loc = xform.translation
        rot = xform.rotation.rotator()
        new_actor = eas.spawn_actor_from_class(bp_class, loc, rot)
        if not new_actor:
            unreal.log_error(f"spawn failed for {label}")
            continue
        new_actor.set_actor_transform(xform, False, True)
        new_actor.set_actor_label(label or "BP_Sky")
        if folder:
            new_actor.set_folder_path(folder)
        for tag in tags:
            new_actor.tags.append(tag)
        eas.destroy_actor(actor)
        replaced += 1
        unreal.log(f"replaced {label} -> {NEW_BP}")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"REPLACE_SKY_ACTOR_OK replaced={replaced}")
