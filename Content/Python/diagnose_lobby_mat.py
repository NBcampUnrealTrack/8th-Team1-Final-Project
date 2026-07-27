"""Diagnose M_LobbyLandscape_Master compile / layer blend state."""
import unreal

MASTER = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
MIC = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"

mat = unreal.EditorAssetLibrary.load_asset(MASTER)
mic = unreal.EditorAssetLibrary.load_asset(MIC)
unreal.log(f"master={mat} mic={mic}")
if mic:
    unreal.log(f"mic parent={mic.get_editor_property('parent')}")

exprs = unreal.MaterialEditingLibrary.get_material_expressions(mat)
unreal.log(f"expr_count={len(exprs)}")
blends = 0
for e in exprs:
    cls = e.get_class().get_name()
    name = e.get_name()
    extra = ""
    if "LandscapeLayerBlend" in cls:
        blends += 1
        try:
            layers = e.get_editor_property("layers")
            extra = f" layers={len(layers)}"
            for i, layer in enumerate(layers):
                ln = layer.get_editor_property("layer_name")
                bt = layer.get_editor_property("blend_type")
                unreal.log(f"  blend layer[{i}] {ln} {bt}")
        except Exception as ex:
            extra = f" layers_err={ex}"
    if "TextureSample" in cls:
        try:
            tex = e.get_editor_property("texture")
            pname = ""
            try:
                pname = e.get_editor_property("parameter_name")
            except Exception:
                pass
            extra = f" tex={tex.get_path_name() if tex else None} param={pname}"
        except Exception:
            pass
    if any(k in cls for k in ("LandscapeLayerBlend", "TextureSample", "MakeMaterial", "BreakMaterial", "LandscapeLayerCoords")):
        unreal.log(f"- {cls}::{name}{extra}")

unreal.log(f"blend_nodes={blends}")

# Recompile and check usage
try:
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.log("recompile issued")
except Exception as e:
    unreal.log(f"recompile err {e}")

# Check material domain / blend mode / shading
for p in ("material_domain", "blend_mode", "shading_model", "usage", "b_used_with_landscape", "used_with_landscape"):
    try:
        unreal.log(f"prop {p}={mat.get_editor_property(p)}")
    except Exception as e:
        unreal.log(f"prop {p} ERR {e}")

# Landscape assignment
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        unreal.log(f"landscape mat={a.get_editor_property('landscape_material')}")
        unreal.log(f"targets={a.get_target_layer_names()}")
        break
