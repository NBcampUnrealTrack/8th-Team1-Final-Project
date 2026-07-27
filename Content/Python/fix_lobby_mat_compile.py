"""Fix M_LobbyLandscape_Master compile: Roughness samples must be Linear Grayscale."""
import unreal

MASTER = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
MIC = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

mat = unreal.EditorAssetLibrary.load_asset(MASTER)
fixed = 0
for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
    if e.get_class().get_name() != "MaterialExpressionTextureSampleParameter2D":
        continue
    try:
        pname = str(e.get_editor_property("parameter_name") or "")
    except Exception:
        pname = ""
    if not pname.endswith("_R"):
        continue
    e.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    fixed += 1
    unreal.log(f"fixed sampler {pname} -> LinearGrayscale")

# Ensure UsedWithLandscape
try:
    unreal.MaterialEditingLibrary.set_material_usage(
        mat, unreal.MaterialUsage.MATUSAGE_LANDSCAPE, True
    )
    unreal.log("MATUSAGE_LANDSCAPE enabled")
except Exception as e:
    unreal.log(f"usage set skipped: {e}")

unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(MASTER)
unreal.log(f"fixed_count={fixed}")

# Clear NoWeightBlend on layer infos if present
for name in ("Seabed", "Plains", "Mountain"):
    li = unreal.EditorAssetLibrary.load_asset(f"/Game/Developers/TaitoP/Landscape/Layers/LI_{name}")
    if not li:
        continue
    for prop in ("b_no_weight_blend", "no_weight_blend"):
        try:
            li.set_editor_property(prop, False)
            unreal.log(f"LI_{name}.{prop}=False")
        except Exception:
            pass
    try:
        li.set_editor_property("layer_name", name)
    except Exception:
        pass
    unreal.EditorAssetLibrary.save_asset(f"/Game/Developers/TaitoP/Landscape/Layers/LI_{name}")

mic = unreal.EditorAssetLibrary.load_asset(MIC)
unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)
unreal.EditorAssetLibrary.save_asset(MIC)

# Reassign + seed Plains full weight so paint preview isn't black
ls = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        ls = a
        break

if ls:
    ls.set_editor_property("landscape_material", mic)
    try:
        ls.post_edit_change()
    except Exception:
        pass

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    # Landscape is 505x505 samples
    size = 505
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, size, size, unreal.TextureRenderTargetFormat.RTF_R8,
        unreal.LinearColor(1, 0, 0, 1), False,
    )
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(1, 0, 0, 1))
    ok_p = ls.landscape_import_weightmap_from_render_target(rt, "Plains")
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
    ok_s = ls.landscape_import_weightmap_from_render_target(rt, "Seabed")
    ok_m = ls.landscape_import_weightmap_from_render_target(rt, "Mountain")
    unreal.log(f"seed Plains={ok_p} Seabed={ok_s} Mountain={ok_m}")

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.EditorAssetLibrary.save_asset(LEVEL)
unreal.log("FIX_COMPILE_OK")
