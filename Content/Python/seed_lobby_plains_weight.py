"""Seed Plains layer to full weight so LandscapeLayerBlend is visible (no null world)."""
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
landscape = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        landscape = a
        break
if not landscape:
    raise RuntimeError("no landscape")

size = 256
rt = unreal.RenderingLibrary.create_render_target2d(
    world, size, size, unreal.TextureRenderTargetFormat.RTF_R8,
    unreal.LinearColor(1, 0, 0, 1), False
)
unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(1, 0, 0, 1))
ok_p = landscape.landscape_import_weightmap_from_render_target(rt, "Plains")
unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
ok_s = landscape.landscape_import_weightmap_from_render_target(rt, "Seabed")
ok_m = landscape.landscape_import_weightmap_from_render_target(rt, "Mountain")
unreal.log(f"SEED_OK Plains={ok_p} Seabed={ok_s} Mountain={ok_m}")
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
