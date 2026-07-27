"""Confirm landscape layer TextureSamples point at owned Lobby textures."""
import unreal

MASTER = "/Game/Developers/TaitoP/Landscape/Materials/M_LobbyLandscape_Master"
mat = unreal.EditorAssetLibrary.load_asset(MASTER)
owned = 0
external = []
nulls = 0
for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
    if "TextureSample" not in e.get_class().get_name():
        continue
    try:
        tex = e.get_editor_property("texture")
    except Exception:
        tex = None
    if not tex:
        nulls += 1
        unreal.log("NULL sample")
        continue
    p = tex.get_path_name()
    unreal.log(f"sample {p}")
    if "/Game/Developers/TaitoP/Landscape/Textures/" in p or "/Engine/" in p:
        owned += 1
    else:
        external.append(p)

unreal.log(f"owned_or_engine={owned} nulls={nulls} external={external}")
if nulls or external:
    raise RuntimeError("landscape textures still bad")
unreal.log("LANDSCAPE_TEXTURE_BINDINGS_OK")
