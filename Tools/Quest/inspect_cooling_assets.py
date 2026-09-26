import unreal
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Assets/X7/Interior/Objects"], force_rescan=True)
for data in registry.get_assets_by_path("/Game/Assets/X7/Interior/Objects", recursive=True):
    if "cooling" in str(data.asset_name).lower():
        unreal.log("COOLING_ASSET " + str(data.package_name) + " class=" + str(data.asset_class_path))
