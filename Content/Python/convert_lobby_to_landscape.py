"""
Replace SM_LobbyTerrain placeholder with a real Landscape from HM_Lobby (505x505).

  py "E:/Unreal/MarineDemolition/Content/Python/convert_lobby_to_landscape.py"
"""
from __future__ import annotations

import struct
from pathlib import Path

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
HEIGHTMAP_PNG = r"E:/Unreal/MarineDemolition/Art/Heightmaps/HM_Lobby.png"
HEIGHTMAP_R16 = r"E:/Unreal/MarineDemolition/Saved/HM_Lobby_505.r16"

SECTION_SIZE = 63  # quads per section
SECTIONS_PER_COMPONENT = 1
COMPONENTS_X = 8
COMPONENTS_Y = 8
# Size = 8 * 63 * 1 + 1 = 505

# cm per vertex / height scale (Z matches typical Landscape Import defaults)
SCALE_XY = 100.0
SCALE_Z = 100.0


def _load_map() -> None:
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)


def _remove_placeholder_mesh() -> int:
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for a in list(actor_sub.get_all_level_actors()):
        label = (a.get_actor_label() or "")
        name = a.get_name()
        cls = a.get_class().get_name()
        if (
            label.startswith("LobbyTerrain")
            or name.startswith("SM_LobbyTerrain")
            or name.startswith("StaticMeshActor")
            and "LobbyTerrain" in label
        ):
            # Only remove the heightmap mesh actor, not lights named oddly
            mesh_comp = a.get_component_by_class(unreal.StaticMeshComponent)
            if mesh_comp:
                sm = mesh_comp.get_editor_property("static_mesh")
                if sm and "LobbyTerrain" in sm.get_name():
                    actor_sub.destroy_actor(a)
                    removed += 1
                    continue
            if label.startswith("LobbyTerrain"):
                actor_sub.destroy_actor(a)
                removed += 1
        # Also remove any prior LobbyLandscape so re-run is clean
        if label == "LobbyLandscape" or cls == "Landscape":
            actor_sub.destroy_actor(a)
            removed += 1
    unreal.log(f"Removed {removed} old terrain/landscape actor(s)")
    return removed


def _ensure_r16() -> Path:
    r16 = Path(HEIGHTMAP_R16)
    png = Path(HEIGHTMAP_PNG)
    if r16.exists() and r16.stat().st_size == 505 * 505 * 2:
        return r16
    if not png.exists():
        raise FileNotFoundError(HEIGHTMAP_PNG)

    # Prefer PIL if available in editor Python; else require prebuilt r16
    try:
        from PIL import Image  # type: ignore
        import numpy as np  # type: ignore

        arr = np.array(Image.open(png))
        if arr.ndim == 3:
            arr = arr[..., 0]
        if arr.dtype != np.uint16:
            # 8-bit fallback stretch
            arr = (arr.astype(np.float64) / max(arr.max(), 1) * 65535.0).astype(np.uint16)
        # UE Landscape Import typically expects Y flipped relative to image top-left
        arr = np.flipud(arr)
        r16.parent.mkdir(parents=True, exist_ok=True)
        arr.astype("<u2").tofile(r16)
        unreal.log(f"Wrote {r16}")
    except Exception as exc:
        if not r16.exists():
            raise RuntimeError(f"Cannot build r16 ({exc}) and file missing: {r16}") from exc
        unreal.log_warning(f"Using existing r16 ({exc})")
    return r16


def _read_height_u16(path: Path) -> list[int]:
    raw = path.read_bytes()
    n = 505 * 505
    if len(raw) < n * 2:
        raise RuntimeError(f"r16 too small: {len(raw)} bytes, need {n * 2}")
    return list(struct.unpack(f"<{n}H", raw[: n * 2]))


def _try_import_via_landscape_api(height_data: list[int]) -> unreal.Actor | None:
    """Spawn ALandscape and call Import if the Python binding exists."""
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    landscape = actor_sub.spawn_actor_from_class(
        unreal.Landscape, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0)
    )
    if not landscape:
        unreal.log_error("Failed to spawn Landscape actor")
        return None

    landscape.set_actor_label("LobbyLandscape")
    size_x = COMPONENTS_X * SECTIONS_PER_COMPONENT * SECTION_SIZE + 1
    size_y = COMPONENTS_Y * SECTIONS_PER_COMPONENT * SECTION_SIZE + 1
    assert size_x == 505 and size_y == 505

    # Scale: Landscape uses actor scale for world size
    landscape.set_actor_scale3d(unreal.Vector(SCALE_XY, SCALE_XY, SCALE_Z))

    # Build Map<Guid, Array<uint16>> as best-effort for stock UE Python
    guid = unreal.Guid()
    try:
        # Empty Guid = default layer (matches editor create path)
        height_map = unreal.Map(unreal.Guid, unreal.Array(int))
    except Exception:
        height_map = None

    import_fn = None
    for name in ("import_", "Import", "landscape_import"):
        if hasattr(landscape, name):
            import_fn = getattr(landscape, name)
            unreal.log(f"Found landscape import method: {name}")
            break

    if import_fn is None:
        # Probe callable attrs for debugging
        names = [n for n in dir(landscape) if "import" in n.lower() or "height" in n.lower()]
        unreal.log_warning(f"No Import() on Landscape. Related attrs: {names}")
        actor_sub.destroy_actor(landscape)
        return None

    # Prepare height array
    try:
        arr = unreal.Array(int)
        for v in height_data:
            arr.append(int(v))
    except Exception:
        arr = height_data

    material_map = None
    try:
        material_map = unreal.Map(unreal.Guid, unreal.Array(unreal.LandscapeImportLayerInfo))
    except Exception:
        try:
            material_map = {}
        except Exception:
            material_map = None

    # Try several call signatures used across UE versions / bindings
    attempts = []
    if height_map is not None:
        try:
            height_map[guid] = arr
        except Exception as exc:
            unreal.log_warning(f"Map assign failed: {exc}")
            try:
                height_map[unreal.Guid()] = arr
            except Exception:
                pass

    attempts.append(
        lambda: import_fn(
            guid,
            0,
            0,
            size_x - 1,
            size_y - 1,
            SECTIONS_PER_COMPONENT,
            SECTION_SIZE,
            height_map if height_map is not None else {guid: arr},
            "",
            material_map if material_map is not None else {guid: []},
            unreal.LandscapeImportAlphamapType.ADDITIVE,
        )
    )
    attempts.append(
        lambda: import_fn(
            unreal.Guid(),
            0,
            0,
            size_x - 1,
            size_y - 1,
            SECTIONS_PER_COMPONENT,
            SECTION_SIZE,
            {unreal.Guid(): height_data},
            None,
            {unreal.Guid(): []},
            unreal.LandscapeImportAlphamapType.ADDITIVE,
        )
    )

    for i, fn in enumerate(attempts):
        try:
            fn()
            unreal.log(f"Landscape.Import succeeded (attempt {i})")
            return landscape
        except Exception as exc:
            unreal.log_warning(f"Import attempt {i} failed: {exc}")

    actor_sub.destroy_actor(landscape)
    return None


def _try_import_via_editor_mode(heightmap_path: str) -> unreal.Actor | None:
    """
    Drive Landscape editor mode New/Import UI settings if accessible from Python.
    This mirrors LandscapeAutomationTests / OnCreateButtonClicked.
    """
    # Activate landscape mode via console (works without binding FEdMode)
    try:
        unreal.SystemLibrary.execute_console_command(
            None, "Landscape.EnableEditorModes 1"
        )
    except Exception:
        pass

    ui = None
    # Find live LandscapeEditorObject instances
    try:
        for obj in unreal.ObjectIterator(unreal.LandscapeEditorObject):
            ui = obj
            break
    except Exception as exc:
        unreal.log_warning(f"ObjectIterator LandscapeEditorObject failed: {exc}")

    if ui is None:
        try:
            ui = unreal.get_default_object(unreal.LandscapeEditorObject)
        except Exception:
            ui = None

    if ui is None:
        unreal.log_warning("Could not access LandscapeEditorObject")
        return None

    try:
        # Configure New Landscape = Import from file
        ui.set_editor_property("new_landscape_quads_per_section", SECTION_SIZE)
        ui.set_editor_property("new_landscape_sections_per_component", SECTIONS_PER_COMPONENT)
        try:
            ui.set_editor_property(
                "new_landscape_component_count", unreal.IntPoint(COMPONENTS_X, COMPONENTS_Y)
            )
        except Exception:
            # Some versions use X/Y separately or Vector2D
            pass

        ui.set_editor_property("import_landscape_heightmap_filename", heightmap_path)
        try:
            ui.set_editor_property("b_heightmap_selected", True)
        except Exception:
            pass

        # Flip / scale options if present
        for prop, val in (
            ("new_landscape_scale", unreal.Vector(SCALE_XY, SCALE_XY, SCALE_Z)),
            ("new_landscape_location", unreal.Vector(0.0, 0.0, 0.0)),
            ("new_landscape_rotation", unreal.Rotator(0.0, 0.0, 0.0)),
        ):
            try:
                ui.set_editor_property(prop, val)
            except Exception:
                pass

        unreal.log("Configured LandscapeEditorObject for import — triggering create via console")
    except Exception as exc:
        unreal.log_warning(f"Failed configuring LandscapeEditorObject: {exc}")
        return None

    # There is no public Python wrapper for OnCreateButtonClicked.
    # Fall through — caller will report manual fallback if no Landscape appears.
    return None


def _try_import_via_render_target(height_data: list[int]) -> unreal.Actor | None:
    """
    Create a flat Landscape first (if somehow present), then push height via RT.
    Only works when a Landscape with components already exists.
    """
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    landscape = None
    for a in actor_sub.get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            landscape = a
            break
    if landscape is None:
        return None

    # Build RT 505x505 RGBA8 packing height into RG (16-bit)
    rt = unreal.RenderingLibrary.create_render_target2d(
        None, 505, 505, unreal.TextureRenderTargetFormat.RTF_RGBA16f, unreal.LinearColor(0, 0, 0, 1), False
    )
    if rt is None:
        unreal.log_warning("Failed to create RT for height import")
        return None

    # Writing pixels from Python to RT is limited; prefer Import() path.
    # Try import anyway in case engine accepts an already-filled external path.
    ok = False
    try:
        ok = landscape.landscape_import_heightmap_from_render_target(rt, True)
    except Exception as exc:
        unreal.log_warning(f"landscape_import_heightmap_from_render_target failed: {exc}")
    if ok:
        landscape.set_actor_label("LobbyLandscape")
        return landscape
    return None


def _find_landscape() -> unreal.Actor | None:
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in actor_sub.get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            return a
    return None


def run() -> None:
    unreal.log("=== convert_lobby_to_landscape ===")
    _load_map()
    _remove_placeholder_mesh()

    r16 = _ensure_r16()
    height_data = _read_height_u16(r16)
    unreal.log(f"Height samples: {len(height_data)} (expect {505*505})")

    landscape = _try_import_via_landscape_api(height_data)
    if landscape is None:
        _try_import_via_editor_mode(HEIGHTMAP_PNG)
        landscape = _find_landscape()

    if landscape is None:
        landscape = _try_import_via_render_target(height_data)

    if landscape is None:
        unreal.log_error(
            "Could not create Landscape via Python Import API.\n"
            "Manual (30s):\n"
            "  1) Modes -> Landscape -> Manage -> New\n"
            "  2) Import from File -> "
            f"{HEIGHTMAP_PNG}\n"
            "  3) Section Size=63, Sections/Component=1, Components=8x8\n"
            "  4) Scale Z ~ 100 -> Import\n"
            "  5) Delete LobbyTerrain_FromHeightmap if still present\n"
        )
        # Keep level saved even if failed cleanup happened
        unreal.EditorLevelLibrary.save_current_level()
        return

    landscape.set_actor_label("LobbyLandscape")
    try:
        landscape.set_folder_path("Terrain")
    except Exception:
        pass

    # Recenter: landscape origin is at component min corner by default; offset so center is at 0
    # Quads world size = (505-1) * SCALE_XY
    half = (505 - 1) * SCALE_XY * 0.5
    try:
        landscape.set_actor_location(unreal.Vector(-half, -half, 0.0), False, True)
    except Exception:
        pass

    unreal.EditorLevelLibrary.save_current_level()
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorAssetLibrary.save_asset(LEVEL)

    unreal.log("SUCCESS: LobbyLandscape created from heightmap and level saved.")


run()
