"""Check material compile status and LandscapeLayerBlend pin connections."""
import unreal

MASTER = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
mat = unreal.EditorAssetLibrary.load_asset(MASTER)

# Compile and inspect
unreal.MaterialEditingLibrary.layout_material_expressions(mat)
unreal.MaterialEditingLibrary.recompile_material(mat)

# property wiring
for prop, label in (
    (unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor"),
    (unreal.MaterialProperty.MP_NORMAL, "Normal"),
    (unreal.MaterialProperty.MP_ROUGHNESS, "Roughness"),
):
    try:
        node = unreal.MaterialEditingLibrary.get_material_property_input_node(mat, prop)
        unreal.log(f"prop {label} <- {node.get_class().get_name() if node else None}")
    except Exception as e:
        unreal.log(f"prop {label}: {e}")

exprs = unreal.MaterialEditingLibrary.get_material_expressions(mat)
blends = [e for e in exprs if "LandscapeLayerBlend" in e.get_class().get_name()]

# Dump expression class histogram for suspicious nodes
from collections import Counter
c = Counter(e.get_class().get_name() for e in exprs)
unreal.log("classes=" + ", ".join(f"{k}:{v}" for k, v in c.most_common()))

# Try to verify each layer input has an upstream by deleting/reconnecting test:
# Instead: for each blend, ensure layers array still WeightBlend
for i, blend in enumerate(blends):
    layers = list(blend.get_editor_property("layers"))
    for li in layers:
        unreal.log(
            f"blend[{i}] name={li.get_editor_property('layer_name')} "
            f"type={li.get_editor_property('blend_type')} "
            f"preview={li.get_editor_property('preview_weight')}"
        )

# Check landscape target layer infos assignment
ls = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name() == "Landscape":
        ls = a
        break

# Inspect editable properties containing layer
props = []
try:
    # UE5 may expose target layers differently
    for attr in dir(ls):
        al = attr.lower()
        if "target" in al or "layer_info" in al or "paint" in al:
            props.append(attr)
except Exception:
    pass
unreal.log("landscape attrs sample=" + ", ".join(props[:40]))

# Try EditorUtilityLibrary / LandscapeEditorObject
try:
    # get layer infos from landscape
    if hasattr(ls, "get_editor_property"):
        for pname in (
            "editor_layer_settings",
            "target_layers",
            "landscape_layers",
            "layers",
        ):
            try:
                v = ls.get_editor_property(pname)
                unreal.log(f"ls.{pname}={v}")
            except Exception as e:
                unreal.log(f"ls.{pname} err={e}")
except Exception as e:
    unreal.log(f"prop dump err={e}")

# Call C++ register again via library if available
try:
    count = unreal.MD_LandscapeLibrary.register_lobby_landscape_paint_layers(None)
    unreal.log(f"register via library count={count}")
except Exception as e:
    unreal.log(f"library register: {e}")
    try:
        cls = unreal.MD_LandscapeLibrary
        count = cls.call_method("RegisterLobbyLandscapePaintLayers", (None,))
        unreal.log(f"register call_method count={count}")
    except Exception as e2:
        unreal.log(f"call_method: {e2}")

# After register, list targets again
try:
    unreal.log(f"targets={list(ls.get_target_layer_names())}")
except Exception as e:
    unreal.log(f"targets err={e}")

# Check if MIC has parent and overrides that break
mic = unreal.EditorAssetLibrary.load_asset(
    "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
)
unreal.log(f"mic parent={mic.get_editor_property('parent')}")

unreal.EditorAssetLibrary.save_asset(MASTER)
unreal.log("CHECK_DONE")
