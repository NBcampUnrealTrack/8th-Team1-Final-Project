"""
Migrate BP_Sky off SoStylized → /Game/Developers/TaitoP/Environment/Sky/
Duplicates deps, rewrites internal refs, points BP_Sky SkyController at owned paths.

Run (PIE stopped):
  py "E:/Unreal/MarineDemolition/Content/Python/migrate_bp_sky_off_sostylized.py"

After verify log shows NO_SOSTYLIZED_LEAKS, delete:
  Content/Developers/TaitoP/SoStylized/
"""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_component", "md_sky_paths", "md_sky_mpc", "md_sky_import", "md_sky_owned_assets"):
    sys.modules.pop(_name, None)

import md_sky_paths as paths
import md_sky_component as sky_comp
import md_sky_owned_assets as owned_assets

paths = importlib.reload(paths)
sky_comp = importlib.reload(sky_comp)
owned_assets = importlib.reload(owned_assets)

BP_SKY = paths.BP_SKY
SKY_ROOT = paths.SKY_ROOT
SRC_PREFIXES = paths.SRC_PREFIXES

def get_bp_sky_cdo():
    if hasattr(sky_comp, "load_bp_sky_and_cdo"):
        _, cdo, _ = sky_comp.load_bp_sky_and_cdo()
        if cdo:
            return cdo
    if hasattr(sky_comp, "get_bp_sky_cdo"):
        cdo = sky_comp.get_bp_sky_cdo()
        if cdo:
            return cdo
    bp_path = find_bp_sky_path()
    if not bp_path:
        return None
    bp_name = bp_path.rsplit("/", 1)[-1]
    cls = unreal.load_class(None, f"{bp_path}.{bp_name}_C")
    if not cls:
        return None
    return unreal.get_default_object(cls)


def find_bp_sky_path() -> str:
    if hasattr(sky_comp, "find_bp_sky_path"):
        p = sky_comp.find_bp_sky_path()
        if p:
            return p
    for p in (BP_SKY, f"{SKY_ROOT}/BP_Sky"):
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            return p
    return BP_SKY


def get_bp_sky_blueprint():
    if hasattr(sky_comp, "load_bp_sky_blueprint"):
        bp = sky_comp.load_bp_sky_blueprint()
        if bp:
            return bp
    bp_path = find_bp_sky_path()
    if not bp_path:
        return None
    asset = unreal.EditorAssetLibrary.load_asset(bp_path)
    if asset and isinstance(asset, unreal.Blueprint):
        return asset
    bp_name = bp_path.rsplit("/", 1)[-1]
    return unreal.load_object(None, f"{bp_path}.{bp_name}")


def compile_bp_sky(bp, bp_path: str) -> None:
    if not bp:
        return
    if isinstance(bp, unreal.Blueprint):
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        return
    if hasattr(bp, "generated_class"):
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            return
        except TypeError:
            pass
    bp_name = bp_path.rsplit("/", 1)[-1]
    bp_obj = unreal.load_object(None, f"{bp_path}.{bp_name}")
    if bp_obj and isinstance(bp_obj, unreal.Blueprint):
        unreal.BlueprintEditorLibrary.compile_blueprint(bp_obj)


def refresh_owned_registry():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.scan_paths_synchronous([SKY_ROOT], True)
    except Exception:
        pass


SEED_ROOTS = [
    f"{SRC_PREFIXES[0]}/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Materials/Presets/MI_StylizedClouds_Classic",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Meshes/SM_StylizedSkyDome",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Meshes/SM_StylizedSkyDome_Clouds",
    f"{SRC_PREFIXES[0]}/Environment/MPC_GlobalEnvironment",
    f"{SRC_PREFIXES[1]}/Environment/Sky/Materials/Presets/MI_StylizedSky_Classic",
    f"{SRC_PREFIXES[1]}/Environment/Sky/Curves/Curve_MoonColor_Classic",
    f"{SRC_PREFIXES[1]}/Environment/Sky/Curves/Curve_MoonGlow_Classic",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Curves/Curve_SunColor_Classic",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Curves/Curve_SunGlow_Classic",
    f"{SRC_PREFIXES[1]}/Environment/Sky/Curves/Curve_SunColor_Classic",
    f"{SRC_PREFIXES[1]}/Environment/Sky/Curves/Curve_SunGlow_Classic",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Textures/T_Skylight_HDR",
    f"{SRC_PREFIXES[1]}/Environment/Sky/Textures/T_Skylight_HDR",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Materials/M_StylizedSky",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Materials/M_StylizedClouds",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Materials/Presets/MI_Sun_Classic",
    f"{SRC_PREFIXES[0]}/Environment/Sky/Meshes/SM_SunAndMoonPlane",
]

EXCLUDE = ("/Effects/", "/Sounds/", "/Demo/", "/Maps/")


def collect_deps(roots):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    for prop, val in (
        ("include_soft_package_references", True),
        ("include_hard_package_references", True),
        ("include_soft_path_references", True),
    ):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    seen = set()
    stack = [r for r in roots if unreal.EditorAssetLibrary.does_asset_exist(r)]
    while stack:
        p = stack.pop()
        if p in seen:
            continue
        seen.add(p)
        try:
            deps = ar.get_dependencies(unreal.Name(p), opts) or []
        except Exception:
            continue
        for d in deps:
            s = str(d)
            if any(s.startswith(prefix + "/") or s == prefix for prefix in SRC_PREFIXES):
                if not any(x in s for x in EXCLUDE):
                    stack.append(s)
    return sorted(seen)


def ensure_dir(package_path: str):
    folder = package_path.rsplit("/", 1)[0]
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        unreal.EditorAssetLibrary.make_directory(folder)


def duplicate_one(src: str) -> str:
    try:
        dst = paths.map_src_to_owned(src)
    except ValueError:
        unreal.log_warning(f"SKIP unmapped {src}")
        return ""
    ensure_dir(dst)
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        return dst
    if not unreal.EditorAssetLibrary.does_asset_exist(src):
        return ""
    try:
        if unreal.EditorAssetLibrary.duplicate_asset(src, dst):
            unreal.EditorAssetLibrary.save_asset(dst)
            unreal.log(f"DUP {src} -> {dst}")
            return dst
    except Exception as e:
        unreal.log_warning(f"dup fail {src}: {e}")
    asset = unreal.EditorAssetLibrary.load_asset(src)
    if not asset:
        return ""
    name = src.rsplit("/", 1)[-1]
    folder = dst.rsplit("/", 1)[0]
    dup = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, folder, asset)
    if dup:
        unreal.EditorAssetLibrary.save_loaded_asset(dup)
        unreal.log(f"DUP {src} -> {dst}")
        return dst
    return ""


def rank_path(p: str) -> int:
    if "/Textures/" in p or p.endswith("MPC_GlobalEnvironment"):
        return 0
    if "/Curves/" in p:
        return 1
    if "/Materials/" in p and "/Presets/" not in p:
        return 2
    if "/Presets/" in p:
        return 3
    if "/Meshes/" in p:
        return 4
    return 5


def list_owned_packages():
    if not unreal.EditorAssetLibrary.does_directory_exist(SKY_ROOT):
        return []
    assets = unreal.EditorAssetLibrary.list_assets(SKY_ROOT, True, False)
    return sorted({a.split(".")[0] for a in assets})


def build_redirect_map() -> dict[str, str]:
    """Map every SoStylized prefix variant to its owned twin (src need not exist on disk)."""
    redirect: dict[str, str] = {}
    for owned_pkg in list_owned_packages():
        for src in paths.owned_to_src_candidates(owned_pkg):
            redirect[src] = owned_pkg
    return redirect


def leak_source_candidates(leak: str) -> list[str]:
    """Find on-disk SoStylized packages matching a leaked path."""
    out = []
    if unreal.EditorAssetLibrary.does_asset_exist(leak):
        out.append(leak)
    try:
        owned = paths.map_src_to_owned(leak)
        for prefix in SRC_PREFIXES:
            if leak.startswith(prefix + "/"):
                tail = leak[len(prefix) :].lstrip("/")
                alt_prefix = SRC_PREFIXES[1] if prefix == SRC_PREFIXES[0] else SRC_PREFIXES[0]
                alt = f"{alt_prefix}/{tail}"
                if alt not in out and unreal.EditorAssetLibrary.does_asset_exist(alt):
                    out.append(alt)
        if owned:
            for prefix in SRC_PREFIXES:
                for src in paths.owned_to_src_candidates(owned):
                    if src not in out and unreal.EditorAssetLibrary.does_asset_exist(src):
                        out.append(src)
    except ValueError:
        pass
    return list(dict.fromkeys(out))


def duplicate_leak_package(leak: str) -> str:
    try:
        dst = paths.map_src_to_owned(leak)
    except ValueError as e:
        unreal.log_warning(f"SKIP unmapped leak {leak}: {e}")
        return ""
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        return dst
    for src in leak_source_candidates(leak):
        if src == leak or paths.is_sostylized_path(src):
            got = duplicate_one(src)
            if got or unreal.EditorAssetLibrary.does_asset_exist(dst):
                return dst
    return ""


def consolidate_leak_list(leaks: list[str], redirect: dict[str, str]) -> int:
    n = consolidate_redirect_pairs(redirect)
    for leak in leaks:
        try:
            dst = paths.map_src_to_owned(leak)
        except ValueError:
            continue
        if not unreal.EditorAssetLibrary.does_asset_exist(dst):
            continue
        dst_obj = unreal.EditorAssetLibrary.load_asset(dst)
        if not dst_obj:
            continue
        for src in leak_source_candidates(leak):
            if src == dst or not unreal.EditorAssetLibrary.does_asset_exist(src):
                continue
            src_obj = unreal.EditorAssetLibrary.load_asset(src)
            if not src_obj:
                continue
            try:
                unreal.EditorAssetLibrary.consolidate_assets(dst_obj, [src_obj])
                unreal.log(f"CONSOLIDATE_LEAK {src} -> {dst}")
                n += 1
            except Exception as e:
                unreal.log_warning(f"consolidate leak {src}: {e}")
    if n:
        refresh_owned_registry()
    return n


def finish_remaining_leaks(root_assets: list[str], max_rounds: int = 3) -> list[str]:
    roots = list(dict.fromkeys(root_assets + all_bp_sky_paths()))
    leaks: list[str] = []
    for rnd in range(max_rounds):
        leaks = collect_sostylized_leaks(roots)
        if not leaks:
            unreal.log(f"FINISH round={rnd + 1} leaks=0")
            break
        unreal.log(f"FINISH round={rnd + 1} leaks={len(leaks)}")
        for leak in leaks:
            unreal.log(f"  LEAK {leak}")
            duplicate_leak_package(leak)
        refresh_owned_registry()
        redirect = build_redirect_map()
        rewrite_soft_refs(redirect, roots)
        for pkg in list_owned_packages():
            fix_asset_refs(pkg, redirect)
        for bp in roots:
            fix_asset_refs(bp, redirect)
        consolidated = consolidate_leak_list(leaks, redirect)
        if consolidated:
            unreal.log(f"FINISH consolidated={consolidated}")
        refresh_owned_registry()
    return collect_sostylized_leaks(roots)


def all_bp_sky_paths() -> list[str]:
    out = []
    for p in (paths.BP_SKY, f"{SKY_ROOT}/BP_Sky"):
        if unreal.EditorAssetLibrary.does_asset_exist(p) and p not in out:
            out.append(p)
    found = find_bp_sky_path()
    if found and found not in out:
        out.insert(0, found)
    return out


def rewrite_soft_refs(redirect: dict[str, str], extra_packages: list[str]) -> None:
    if not redirect:
        return
    packages = list(dict.fromkeys(list_owned_packages() + extra_packages + all_bp_sky_paths()))
    loaded_pkgs = []
    loaded_assets = []
    for p in packages:
        try:
            pkg = unreal.load_package(p)
            if pkg:
                loaded_pkgs.append(pkg)
        except Exception:
            pass
        try:
            asset = unreal.EditorAssetLibrary.load_asset(p)
            if asset:
                loaded_assets.append(asset)
        except Exception:
            pass
    if not loaded_pkgs and not loaded_assets:
        return
    at = unreal.AssetToolsHelpers.get_asset_tools()
    soft_map = {
        unreal.SoftObjectPath(src): unreal.SoftObjectPath(dst)
        for src, dst in redirect.items()
    }
    for targets, label in ((loaded_pkgs, "packages"), (loaded_assets, "assets")):
        if not targets:
            continue
        try:
            at.rename_referencing_soft_object_paths(targets, soft_map)
            unreal.log(f"SOFT_REWRITE ok {label}={len(targets)} pairs={len(soft_map)}")
            return
        except Exception as e:
            unreal.log_warning(f"SOFT_REWRITE fail ({label}): {e}")
    try:
        name_map = {unreal.Name(src): unreal.Name(dst) for src, dst in redirect.items()}
        at.rename_referencing_soft_object_paths(loaded_pkgs, name_map)
        unreal.log("SOFT_REWRITE ok (Name map)")
    except Exception as e2:
        unreal.log_warning(f"SOFT_REWRITE fail2: {e2}")


def consolidate_all_owned_twins(redirect: dict[str, str] | None = None) -> int:
    """Merge every on-disk Developers/SoStylized twin into its owned copy (deepest paths first)."""
    if redirect is None:
        redirect = build_redirect_map()
    pairs: list[tuple[str, str]] = []
    for src, dst in redirect.items():
        if not paths.is_deletable_sostylized_path(src):
            continue
        if src == dst:
            continue
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            continue
        if not unreal.EditorAssetLibrary.does_asset_exist(dst):
            continue
        pairs.append((src, dst))
    pairs.sort(key=lambda item: item[0].count("/"), reverse=True)
    n = 0
    for src, dst in pairs:
        src_obj = unreal.EditorAssetLibrary.load_asset(src)
        dst_obj = unreal.EditorAssetLibrary.load_asset(dst)
        if not src_obj or not dst_obj:
            continue
        try:
            unreal.EditorAssetLibrary.consolidate_assets(dst_obj, [src_obj])
            unreal.log(f"CONSOLIDATE_ALL {src} -> {dst}")
            n += 1
        except Exception as e:
            unreal.log_warning(f"consolidate_all {src}: {e}")
    if n:
        refresh_owned_registry()
    return n


def consolidate_redirect_pairs(redirect: dict[str, str]) -> int:
    """Merge SoStylized twins into owned copies; updates hard refs and deletes src."""
    n = 0
    for src, dst in sorted(redirect.items()):
        if src == dst:
            continue
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            continue
        if not unreal.EditorAssetLibrary.does_asset_exist(dst):
            continue
        src_obj = unreal.EditorAssetLibrary.load_asset(src)
        dst_obj = unreal.EditorAssetLibrary.load_asset(dst)
        if not src_obj or not dst_obj:
            continue
        try:
            unreal.EditorAssetLibrary.consolidate_assets(dst_obj, [src_obj])
            unreal.log(f"CONSOLIDATE {src} -> {dst}")
            n += 1
        except Exception as e:
            unreal.log_warning(f"consolidate {src}: {e}")
    if n:
        refresh_owned_registry()
    return n


def fix_asset_refs(dst: str, redirect: dict[str, str]) -> bool:
    asset = unreal.EditorAssetLibrary.load_asset(dst)
    if not asset:
        return False
    changed = False

    def remap_obj(obj):
        nonlocal changed
        if not obj:
            return obj
        try:
            path = obj.get_path_name().split(".")[0]
        except Exception:
            return obj
        owned = redirect.get(path)
        if not owned and paths.is_sostylized_path(path):
            try:
                owned = paths.map_src_to_owned(path)
            except ValueError:
                owned = ""
        if owned:
            neu = unreal.EditorAssetLibrary.load_asset(owned)
            if neu:
                changed = True
                return neu
        return obj

    if isinstance(asset, unreal.Material):
        for expr in unreal.MaterialEditingLibrary.get_material_expressions(asset):
            for prop in ("texture", "collection", "material_function", "function"):
                try:
                    v = expr.get_editor_property(prop)
                except Exception:
                    continue
                nv = remap_obj(v)
                if nv is not v:
                    try:
                        expr.set_editor_property(prop, nv)
                    except Exception:
                        pass
        if changed:
            unreal.MaterialEditingLibrary.recompile_material(asset)
    elif isinstance(asset, unreal.MaterialInstanceConstant):
        try:
            parent = asset.get_editor_property("parent")
            np = remap_obj(parent)
            if np is not parent:
                asset.set_editor_property("parent", np)
        except Exception:
            pass
        try:
            for tp in list(asset.texture_parameter_values):
                tex = tp.get_editor_property("parameter_value")
                nt = remap_obj(tex)
                if nt is not tex:
                    tp.set_editor_property("parameter_value", nt)
        except Exception:
            pass
    elif isinstance(asset, unreal.MaterialFunction):
        try:
            for expr in unreal.MaterialEditingLibrary.get_material_expressions(asset):
                for prop in ("texture", "collection", "material_function", "function"):
                    try:
                        v = expr.get_editor_property(prop)
                    except Exception:
                        continue
                    nv = remap_obj(v)
                    if nv is not v:
                        try:
                            expr.set_editor_property(prop, nv)
                        except Exception:
                            pass
        except Exception as e:
            unreal.log_warning(f"MF fix {dst}: {e}")
    elif _is_curve_asset(asset):
        _fix_curve_asset_refs(asset, redirect, remap_obj)
    elif isinstance(asset, unreal.Blueprint):
        sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        bfl = unreal.SubobjectDataBlueprintFunctionLibrary
        for h in sub.k2_gather_subobject_data_for_blueprint(asset):
            data = bfl.get_data(h)
            try:
                obj = bfl.get_associated_object(data)
            except Exception:
                obj = bfl.get_object(data)
            if not obj:
                continue
            for prop in (
                "static_mesh",
                "cubemap",
                "override_materials",
                "asset",
                "niagara_system_asset",
                "sound",
                "sound_class",
            ):
                try:
                    v = obj.get_editor_property(prop)
                except Exception:
                    continue
                if isinstance(v, (list, tuple)):
                    continue
                nv = remap_obj(v)
                if nv is not v:
                    try:
                        obj.set_editor_property(prop, nv)
                        changed = True
                    except Exception:
                        pass
            try:
                if hasattr(obj, "get_material") and hasattr(obj, "set_material"):
                    mat = obj.get_material(0)
                    nm = remap_obj(mat)
                    if nm is not mat and nm:
                        obj.set_material(0, nm)
                        changed = True
            except Exception:
                pass
        if changed:
            try:
                unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            except Exception as e:
                unreal.log_warning(f"compile {dst}: {e}")

    if changed:
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        unreal.log(f"FIX_REFS {dst}")
    return changed


def _is_curve_asset(asset) -> bool:
    try:
        name = asset.get_class().get_name()
    except Exception:
        return False
    return "Curve" in name


def _fix_curve_asset_refs(asset, redirect: dict[str, str], remap_obj) -> None:
    for prop in (
        "external_curve",
        "curve",
        "float_curve",
        "color_curve",
        "source_curve",
        "source_curve_horizontal",
        "source_curve_vertical",
    ):
        try:
            v = asset.get_editor_property(prop)
        except Exception:
            continue
        if v is None:
            continue
        if isinstance(v, (list, tuple)):
            new_list = []
            item_changed = False
            for item in v:
                nv = remap_obj(item)
                new_list.append(nv)
                if nv is not item:
                    item_changed = True
            if item_changed:
                try:
                    asset.set_editor_property(prop, new_list)
                except Exception:
                    pass
        else:
            nv = remap_obj(v)
            if nv is not v:
                try:
                    asset.set_editor_property(prop, nv)
                except Exception:
                    pass
    try:
        rows = asset.get_editor_property("gradient_curves")
        if rows:
            for row in rows:
                for prop in ("curve", "external_curve"):
                    try:
                        v = row.get_editor_property(prop)
                        nv = remap_obj(v)
                        if nv is not v:
                            row.set_editor_property(prop, nv)
                    except Exception:
                        pass
    except Exception:
        pass


def fix_material_refs(dst_package: str):
    fix_asset_refs(dst_package, build_redirect_map())


def collect_sostylized_leaks(root_assets: list[str]) -> list[str]:
    leaks: set[str] = set()
    for root in root_assets:
        leaks.update(_collect_sostylized_leaks_one(root))
    return sorted(leaks)


def _collect_sostylized_leaks_one(root_asset: str) -> list[str]:
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    opts = unreal.AssetRegistryDependencyOptions()
    for prop, val in (
        ("include_soft_package_references", True),
        ("include_hard_package_references", True),
        ("include_soft_path_references", True),
    ):
        try:
            opts.set_editor_property(prop, val)
        except Exception:
            pass
    leaks = []
    stack = [root_asset]
    seen = set()
    while stack:
        p = stack.pop()
        if p in seen:
            continue
        seen.add(p)
        try:
            deps = ar.get_dependencies(unreal.Name(p), opts) or []
        except Exception:
            continue
        for d in deps:
            s = str(d)
            if paths.is_deletable_sostylized_path(s):
                leaks.append(s)
            elif s.startswith(SKY_ROOT) and s not in seen:
                stack.append(s)
    return sorted(set(leaks))


def remediate_leaks(bp_paths: list[str], max_rounds: int = 5) -> list[str]:
    roots = list(dict.fromkeys(bp_paths + all_bp_sky_paths()))
    leaks: list[str] = []
    for rnd in range(max_rounds):
        leaks = collect_sostylized_leaks(roots)
        if not leaks:
            unreal.log(f"REMEDIATE round={rnd + 1} leaks=0")
            break
        unreal.log(f"REMEDIATE round={rnd + 1} leaks={len(leaks)}")
        for src in leaks:
            duplicate_one(src)
        refresh_owned_registry()
        redirect = build_redirect_map()
        unreal.log(f"REMEDIATE redirects={len(redirect)}")
        rewrite_soft_refs(redirect, roots)
        for pkg in list_owned_packages():
            fix_asset_refs(pkg, redirect)
        for bp in roots:
            fix_asset_refs(bp, redirect)
        consolidated = consolidate_redirect_pairs(redirect)
        if consolidated:
            unreal.log(f"REMEDIATE consolidated={consolidated}")
        for bp in roots:
            cdo = None
            if bp == find_bp_sky_path():
                cdo = get_bp_sky_cdo()
            if cdo:
                apply_bp_sky_refs(cdo)
            bp_obj = unreal.EditorAssetLibrary.load_asset(bp)
            try:
                compile_bp_sky(bp_obj, bp)
            except Exception as e:
                unreal.log_warning(f"compile skipped {bp}: {e}")
            unreal.EditorAssetLibrary.save_asset(bp)
        refresh_owned_registry()
    return collect_sostylized_leaks(roots)


def apply_bp_sky_refs(target):
    import importlib
    import sys

    for _name in ("md_sky_mpc", "md_sky_import"):
        sys.modules.pop(_name, None)
    import md_sky_import as md_sky_mpc

    md_sky_mpc = importlib.reload(md_sky_mpc)

    for prop, p in paths.PATHS.items():
        asset = None
        for candidate in paths.candidate_paths(prop):
            if unreal.EditorAssetLibrary.does_asset_exist(candidate):
                asset = unreal.EditorAssetLibrary.load_asset(candidate)
                if asset:
                    break
        if asset:
            sky_comp.set_sky_prop(target, prop, asset)
        else:
            unreal.log_warning(f"MISSING {prop} -> {p}")
    sky_comp.configure_lobby_frozen_sky(target, 12.0)
    sky_comp.set_sky_prop(target, "MpcShaderTimeOffsetHours", 12.0)
    sky_comp.set_sky_prop(target, "MpcDayStartTimeHours", 0.0)
    mpc = unreal.EditorAssetLibrary.load_asset(paths.PATHS["EnvironmentMPC"])
    if mpc:
        md_sky_mpc.push_midday_scalars(mpc, day_start=0.0, shader_offset=12.0)
    try:
        target.call_method("RefreshSkyNow")
    except Exception:
        pass


def verify_no_leaks(root_assets: list[str]):
    leaks = collect_sostylized_leaks(root_assets)
    if leaks:
        unreal.log_warning(f"SOSTYLIZED_LEAKS={len(leaks)}")
        for L in leaks:
            unreal.log_warning(f"  LEAK {L}")
    else:
        unreal.log("NO_SOSTYLIZED_LEAKS on BP_Sky closure (Developers/TaitoP/SoStylized)")
    return leaks


def run():
    if not unreal.EditorAssetLibrary.does_directory_exist(SKY_ROOT):
        unreal.EditorAssetLibrary.make_directory(SKY_ROOT)
    for sub in ("Materials", "Textures", "Meshes", "Curves", "Textures/CloudLayers", "Effects"):
        p = f"{SKY_ROOT}/{sub}"
        if not unreal.EditorAssetLibrary.does_directory_exist(p):
            unreal.EditorAssetLibrary.make_directory(p)

    packages = collect_deps(SEED_ROOTS)
    unreal.log(f"MIGRATE packages={len(packages)}")
    for src in sorted(packages, key=rank_path):
        duplicate_one(src)

    owned = []
    for src in packages:
        try:
            dst = paths.map_src_to_owned(src)
            if unreal.EditorAssetLibrary.does_asset_exist(dst):
                owned.append(dst)
        except ValueError:
            pass
    for dst in owned:
        fix_material_refs(dst)

    refresh_owned_registry()
    copied = owned_assets.ensure_minimum_owned()
    if copied:
        refresh_owned_registry()

    bp_path = find_bp_sky_path()
    if not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        raise RuntimeError(f"missing {bp_path}")

    bp = get_bp_sky_blueprint()
    cdo = get_bp_sky_cdo()
    if not cdo:
        raise RuntimeError(f"BP_Sky CDO unavailable: {bp_path}")
    apply_bp_sky_refs(cdo)
    try:
        compile_bp_sky(bp, bp_path)
    except Exception as e:
        unreal.log_warning(f"compile skipped: {e}")
    unreal.EditorAssetLibrary.save_asset(bp_path)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if "BP_Sky" in (a.get_actor_label() or "") or "MD_StylizedSky" in a.get_class().get_name():
            apply_bp_sky_refs(a)

    remediate_leaks([bp_path])

    leaks = finish_remaining_leaks([bp_path])

    if leaks:
        verify_no_leaks([bp_path])
    else:
        unreal.log("NO_SOSTYLIZED_LEAKS on BP_Sky closure (Developers/TaitoP/SoStylized)")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(f"MIGRATE_BP_SKY_OFF_SOSTYLIZED_OK leaks={len(leaks)}")


run()
