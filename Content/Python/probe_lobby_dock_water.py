"""Probe Lobby dock bounds and existing water actors for sea placement."""
import unreal

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
interesting = []
for a in actors:
    label = a.get_actor_label() or ""
    cls = a.get_class().get_name()
    name = a.get_name()
    low = (label + " " + name + " " + cls).lower()
    if any(k in low for k in ("dock", "pier", "water", "sea", "ocean", "medieval", "lobby", "landscape", "plane")):
        loc = a.get_actor_location()
        try:
            origin, extent = a.get_actor_bounds(False)
        except Exception:
            origin, extent = loc, unreal.Vector(0, 0, 0)
        interesting.append((label, cls, name, loc, origin, extent))

unreal.log(f"actor_count={len(actors)} interesting={len(interesting)}")
for label, cls, name, loc, origin, extent in sorted(interesting, key=lambda x: x[0]):
    zmin = origin.z - extent.z
    zmax = origin.z + extent.z
    unreal.log(
        f"{label} | {cls} | loc=({loc.x:.0f},{loc.y:.0f},{loc.z:.0f}) "
        f"bounds_z=[{zmin:.0f}..{zmax:.0f}] extent=({extent.x:.0f},{extent.y:.0f},{extent.z:.0f})"
    )

# List water-related classes available
for name in dir(unreal):
    if "water" in name.lower() or "ocean" in name.lower():
        unreal.log(f"ue_type={name}")

# Search assets
reg = unreal.AssetRegistryHelpers.get_asset_registry()
for filt in ("*Water*", "*Ocean*", "*Sea*", "*Plane*"):
    assets = unreal.EditorAssetLibrary.list_assets("/Game", True, False)
    # too heavy; use registry filter
try:
    ar_filter = unreal.ARFilter(class_names=["StaticMesh", "Material", "MaterialInstanceConstant"], recursive_paths=True)
except Exception:
    ar_filter = None

# Find dock meshes / water mats under Medieval_Dock and Developers
for path in (
    "/Game/Developers/TaitoP/Medieval_Dock",
    "/Game/Developers/TaitoP",
    "/Game/Water",
    "/Engine/BasicShapes",
):
    if unreal.EditorAssetLibrary.does_directory_exist(path):
        assets = unreal.EditorAssetLibrary.list_assets(path, True, False)
        hits = [a for a in assets if any(k in a.lower() for k in ("water", "ocean", "sea", "dock", "pier", "plane"))]
        unreal.log(f"{path}: total={len(assets)} hits={len(hits)}")
        for h in hits[:30]:
            unreal.log(f"  {h}")
    else:
        unreal.log(f"missing dir {path}")

unreal.log("PROBE_DONE")
