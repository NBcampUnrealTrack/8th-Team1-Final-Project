"""Brighten Lobby landscape pastels — less muddy, more vivid pastel."""
import unreal

MIC = "/Game/Developers/TaitoP/Landscape/Materials/MI_LobbyLandscape"
LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"

# name: tint RGB, hue, sat (additive to 1.0 in graph: sat_amt = 1+sat, so 0=full, -1=full desat),
# luminosity, macro strength
TUNING = {
    "Seabed": {
        "tint": (0.55, 0.82, 0.98),   # clear sky-blue sand
        "hue": 0.42,
        "sat": -0.05,                 # barely desat (was -0.28)
        "lum": 0.18,
        "macro": 0.08,
        "n_int": 0.9,
    },
    "Plains": {
        "tint": (0.55, 0.92, 0.48),   # fresh mint/grass green
        "hue": 0.38,
        "sat": -0.02,
        "lum": 0.14,
        "macro": 0.08,
        "n_int": 0.8,
    },
    "Mountain": {
        "tint": (0.78, 0.74, 0.68),   # warm taupe stone
        "hue": 0.22,
        "sat": -0.12,
        "lum": 0.10,
        "macro": 0.10,
        "n_int": 1.4,
    },
}

mic = unreal.EditorAssetLibrary.load_asset(MIC)
if not mic:
    raise RuntimeError("missing MIC")

unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, "MacroScale", 0.035)

for name, t in TUNING.items():
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        mic, f"{name}_Tint",
        unreal.LinearColor(t["tint"][0], t["tint"][1], t["tint"][2], 1.0),
    )
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{name}_HUE", float(t["hue"]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{name}_Saturation", float(t["sat"]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{name}_Luminosity", float(t["lum"]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{name}_MacroStrength", float(t["macro"]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, f"{name}_Normal_Intensity", float(t["n_int"]))
    unreal.log(f"{name} tint={t['tint']} hue={t['hue']} sat={t['sat']} lum={t['lum']}")

# Also bump master defaults so future MICs inherit
mat = unreal.EditorAssetLibrary.load_asset(
    "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
)
if mat:
    for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
        cls = e.get_class().get_name()
        if "ScalarParameter" in cls:
            pname = str(e.get_editor_property("parameter_name") or "")
            for name, t in TUNING.items():
                if pname == f"{name}_Saturation":
                    e.set_editor_property("default_value", float(t["sat"]))
                elif pname == f"{name}_HUE":
                    e.set_editor_property("default_value", float(t["hue"]))
                elif pname == f"{name}_Luminosity":
                    e.set_editor_property("default_value", float(t["lum"]))
                elif pname == f"{name}_MacroStrength":
                    e.set_editor_property("default_value", float(t["macro"]))
                elif pname == f"{name}_Normal_Intensity":
                    e.set_editor_property("default_value", float(t["n_int"]))
        elif "VectorParameter" in cls:
            pname = str(e.get_editor_property("parameter_name") or "")
            for name, t in TUNING.items():
                if pname == f"{name}_Tint":
                    e.set_editor_property(
                        "default_value",
                        unreal.LinearColor(t["tint"][0], t["tint"][1], t["tint"][2], 1.0),
                    )
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(
        "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
    )

unreal.EditorAssetLibrary.save_asset(MIC)

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

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.EditorAssetLibrary.save_asset(LEVEL)
unreal.log("COLOR_BRIGHTEN_OK")
