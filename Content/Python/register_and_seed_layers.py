"""Register Seabed/Plains/Mountain target layers and seed Plains weight=1."""
import unreal

LAYER_DST = "/Game/Developers/TaitoP/Landscape/Layers"
NAMES = ("Seabed", "Plains", "Mountain")


def run():
    ls = None
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            ls = a
            break
    if not ls:
        raise RuntimeError("no landscape")

    # Keep visibility layer, add paint layers
    tl = ls.get_editor_property("target_layers")
    # Copy existing
    new_tl = dict(tl) if tl else {}

    for name in NAMES:
        li = unreal.EditorAssetLibrary.load_asset(f"{LAYER_DST}/LI_{name}")
        if not li:
            unreal.log_warning(f"missing LI_{name}")
            continue
        try:
            li.set_editor_property("layer_name", name)
        except Exception:
            pass
        settings = unreal.LandscapeTargetLayerSettings()
        settings.set_editor_property("layer_info_obj", li)
        new_tl[name] = settings
        unreal.log(f"registered target layer {name}")

    ls.set_editor_property("target_layers", new_tl)
    try:
        ls.post_edit_change()
    except Exception:
        pass

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    size = 256
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, size, size, unreal.TextureRenderTargetFormat.RTF_R8,
        unreal.LinearColor(1, 0, 0, 1), False
    )
    # Plains full
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(1, 0, 0, 1))
    ok_p = ls.landscape_import_weightmap_from_render_target(rt, "Plains")
    # Others empty
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
    ok_s = ls.landscape_import_weightmap_from_render_target(rt, "Seabed")
    ok_m = ls.landscape_import_weightmap_from_render_target(rt, "Mountain")
    unreal.log(f"REGISTER_SEED Plains={ok_p} Seabed={ok_s} Mountain={ok_m}")
    unreal.log(f"target_layers now={ls.get_editor_property('target_layers')}")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()


run()
