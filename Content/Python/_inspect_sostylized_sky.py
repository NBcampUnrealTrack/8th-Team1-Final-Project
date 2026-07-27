import unreal

ROOT = "/Game/Developers/TaitoP/SoStylized"
unreal.log(f"exists={unreal.EditorAssetLibrary.does_directory_exist(ROOT)}")

for a in unreal.EditorAssetLibrary.list_assets(ROOT, True, False):
    al = a.lower()
    if any(k in al for k in ("sky", "cloud", "sun", "moon", "atmosphere", "skydome", "skybox", "day", "night", "bp_")):
        unreal.log(f"HIT {a}")

for sub in ("Materials", "Environment", "Textures", "Effects", "Demo", "Maps"):
    p = f"{ROOT}/{sub}"
    if unreal.EditorAssetLibrary.does_directory_exist(p):
        assets = unreal.EditorAssetLibrary.list_assets(p, True, False)
        unreal.log(f"=== {sub} count={len(assets)} ===")
        for a in assets[:50]:
            unreal.log(f"  {a}")
