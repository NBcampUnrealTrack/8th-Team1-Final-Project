"""Diagnose + restore Lobby landscape paint without wiping variation material."""
from __future__ import annotations

import unreal

MASTER = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
MIC = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
LAYER_DST = "/Game/Developers/TaitoP/Landscape/Layers"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
LAYERS = ("Seabed", "Plains", "Mountain")


def diagnose():
    mat = unreal.EditorAssetLibrary.load_asset(MASTER)
    mic = unreal.EditorAssetLibrary.load_asset(MIC)
    unreal.log(f"master={mat} mic={mic}")

    # Force landscape usage flags when available
    for prop in ("b_used_with_landscape", "used_with_landscape"):
        try:
            mat.set_editor_property(prop, True)
            unreal.log(f"set {prop}=True")
        except Exception as e:
            unreal.log(f"{prop}: {e}")

    exprs = unreal.MaterialEditingLibrary.get_material_expressions(mat)
    blends = [e for e in exprs if "LandscapeLayerBlend" in e.get_class().get_name()]
    unreal.log(f"expr_count={len(exprs)} blend_count={len(blends)}")
    for i, blend in enumerate(blends):
        layers = blend.get_editor_property("layers")
        names = [str(l.get_editor_property("layer_name")) for l in layers]
        types = [str(l.get_editor_property("blend_type")) for l in layers]
        unreal.log(f"blend[{i}] layers={names} types={types}")

    # Check for broken/common bad sampler types
    for e in exprs:
        cls = e.get_class().get_name()
        if "TextureSample" not in cls:
            continue
        try:
            st = e.get_editor_property("sampler_type")
            tex = e.get_editor_property("texture")
            unreal.log(f"texsample sampler={st} tex={tex.get_name() if tex else None}")
        except Exception:
            pass

    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MASTER)

    method = unreal.LandscapeTargetLayerBlendMethod.FINAL_WEIGHT_BLENDING
    for name in LAYERS:
        path = f"{LAYER_DST}/LI_{name}"
        li = unreal.EditorAssetLibrary.load_asset(path)
        if not li:
            unreal.log_warning(f"missing {path}")
            continue
        try:
            li.set_editor_property("blend_method", method)
        except Exception as e:
            unreal.log_warning(f"set blend_method fail {name}: {e}")
        try:
            li.set_editor_property("layer_name", name)
        except Exception:
            pass
        # Clear legacy no-weight if present
        for prop in ("b_no_weight_blend", "no_weight_blend"):
            try:
                li.set_editor_property(prop, False)
            except Exception:
                pass
        unreal.EditorAssetLibrary.save_asset(path)
        try:
            unreal.log(f"LI_{name} blend_method={li.get_editor_property('blend_method')}")
        except Exception:
            pass

    ls = None
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            ls = a
            break
    if not ls:
        raise RuntimeError("no landscape")

    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)
    unreal.EditorAssetLibrary.save_asset(MIC)
    ls.set_editor_property("landscape_material", mic)
    try:
        ls.post_edit_change()
    except Exception:
        pass

    unreal.log(f"landscape={ls.get_actor_label()}")
    unreal.log(f"assigned={ls.get_editor_property('landscape_material')}")
    try:
        unreal.log(f"targets_before={list(ls.get_target_layer_names())}")
    except Exception as e:
        unreal.log(f"targets_before err={e}")

    unreal.SystemLibrary.execute_console_command(None, "MD.RegisterLobbyPaintLayers")
    try:
        unreal.log(f"targets_after={list(ls.get_target_layer_names())}")
    except Exception as e:
        unreal.log(f"targets_after err={e}")

    # Seed Seabed full so painting Plains shows change
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    size = 505
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, size, size, unreal.TextureRenderTargetFormat.RTF_R8,
        unreal.LinearColor(0, 0, 0, 1), False,
    )
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(1, 0, 0, 1))
    ok_s = ls.landscape_import_weightmap_from_render_target(rt, "Seabed")
    unreal.RenderingLibrary.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
    ok_p = ls.landscape_import_weightmap_from_render_target(rt, "Plains")
    ok_m = ls.landscape_import_weightmap_from_render_target(rt, "Mountain")
    unreal.log(f"seed Seabed={ok_s} Plains={ok_p} Mountain={ok_m}")

    # Spot-check weights
    for name in LAYERS:
        try:
            rt2 = unreal.RenderingLibrary.create_render_target2d(
                world, 64, 64, unreal.TextureRenderTargetFormat.RTF_R8,
                unreal.LinearColor(0, 0, 0, 1), False,
            )
            ok = ls.landscape_export_weightmap_to_render_target(rt2, name)
            if ok:
                pixels = unreal.RenderingLibrary.read_render_target(None, rt2, True)
                if pixels:
                    vals = [float(p.r) for p in pixels]
                    unreal.log(
                        f"weight {name} min={min(vals):.3f} max={max(vals):.3f} avg={sum(vals)/len(vals):.3f}"
                    )
        except Exception as e:
            unreal.log(f"weight check {name}: {e}")

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("PAINT_RESTORE_OK")


diagnose()
