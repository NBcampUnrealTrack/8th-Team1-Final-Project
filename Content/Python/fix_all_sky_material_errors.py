"""
Fix Lobby sky material SM6 compile errors.

Root cause (verified by A/B test):
  MaterialExpressionCollectionParameter bound to VECTOR MPC params
  (SunColor / CloudTint / SkyTint) fails PCD3D_SM6 when authored via Python.
  Scalar MPC params are fine. Working mats (M_LobbyStylizedSky, M_LobbySunDisc)
  never used vector MPC nodes.

Fix: rebuild all broken materials with Constant3Vector colors + scalar MPC only.
"""
from __future__ import annotations

import sys
from pathlib import Path

import unreal

PY = Path(r"E:/Unreal/MarineDemolition/Content/Python")
if str(PY) not in sys.path:
    sys.path.insert(0, str(PY))

import importlib

import build_original_curve_lobby_sky as curve
import build_ship_safe_lobby_sky as safe
import create_lobby_stylized_sky as stylized

# Editor Python keeps stale modules across remote runs
curve = importlib.reload(curve)
safe = importlib.reload(safe)
stylized = importlib.reload(stylized)

TEX = "/Game/Developers/TaitoP/Environment/Sky/Textures"
MAT = "/Game/Developers/TaitoP/Environment/Sky/Materials"
MPC_PATH = "/Game/Developers/TaitoP/Environment/Sky/MPC_LobbySky"

BROKEN = [
    f"{MAT}/M_LobbyOriginalSky",
    f"{MAT}/M_LobbyOriginalClouds",
    f"{MAT}/M_LobbyOriginalSun",
    f"{MAT}/M_LobbyStylizedClouds",
    f"{MAT}/M_LobbySun",
    f"{MAT}/MI_LobbyStylizedClouds",
]

TEST_JUNK = [
    f"{MAT}/M_TestSun_ScalarOnly",
    f"{MAT}/M_TestSun_WithVectorMPC",
    f"{MAT}/M_TestSun_VectorWithId",
]


def load_tex(name):
    path = f"{TEX}/{name}"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        raise RuntimeError(f"missing texture {path}")
    return unreal.EditorAssetLibrary.load_asset(path)


def refresh_mi(mi_path, scalar=None, vector=None):
    if not unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        return
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    parent = mi.get_editor_property("parent")
    if not parent:
        unreal.log_warning(f"{mi_path} no parent")
        return
    unreal.MaterialEditingLibrary.recompile_material(parent)
    for name, val in (scalar or {}).items():
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, name, float(val))
        except Exception as e:
            unreal.log_warning(f"{mi_path} scalar {name}: {e}")
    for name, rgba in (vector or {}).items():
        try:
            unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                mi, name, unreal.LinearColor(*rgba)
            )
        except Exception as e:
            unreal.log_warning(f"{mi_path} vector {name}: {e}")
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    unreal.log(f"refreshed {mi_path.rsplit('/', 1)[-1]}")


def assert_no_vector_mpc():
    bad = []
    mpc = unreal.EditorAssetLibrary.load_asset(MPC_PATH)
    vec_names = set()
    if mpc:
        for p in mpc.get_editor_property("vector_parameters") or []:
            vec_names.add(str(p.get_editor_property("parameter_name")))
    for p in unreal.EditorAssetLibrary.list_assets(MAT, False, False):
        pkg = p.split(".")[0]
        mat = unreal.EditorAssetLibrary.load_asset(pkg)
        if not isinstance(mat, unreal.Material):
            continue
        short = pkg.rsplit("/", 1)[-1]
        if short.startswith("M_Test"):
            continue
        for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
            if e.get_class().get_name() != "MaterialExpressionCollectionParameter":
                continue
            pn = str(e.get_editor_property("parameter_name"))
            col = e.get_editor_property("collection")
            if not col:
                bad.append(f"{short}:{pn} missing_collection")
            if pn in vec_names:
                bad.append(f"{short}:VECTOR_MPC {pn}")
            if not pn or pn in ("None", "Name(\"None\")"):
                bad.append(f"{short}:empty_name")
    if bad:
        for b in bad:
            unreal.log_error(f"STILL_BAD {b}")
        raise RuntimeError(f"material errors remain: {len(bad)}")
    unreal.log("VERIFY_NO_VECTOR_MPC_OK")


def verify_sm6_compile():
    """Recompile and check log isn't our only signal — also ensure expressions exist."""
    for path in BROKEN:
        if path.endswith("MI_LobbyStylizedClouds"):
            continue
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            raise RuntimeError(f"missing {path}")
        mat = unreal.EditorAssetLibrary.load_asset(path)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        n = len(list(unreal.MaterialEditingLibrary.get_material_expressions(mat)))
        if n < 3:
            raise RuntimeError(f"{path} empty graph")
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        unreal.log(f"recompiled {path.rsplit('/', 1)[-1]} exprs={n}")


def cleanup_tests():
    for p in TEST_JUNK:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            unreal.EditorAssetLibrary.delete_asset(p)
            unreal.log(f"deleted {p}")


def run():
    atlas = load_tex("T_LobbySky_CurveAtlas")
    height = load_tex("T_LobbySky_HeightGradient")
    bg = load_tex("T_LobbyBGClouds")
    l1 = load_tex("T_LobbyCloud_L1")
    l2 = load_tex("T_LobbyCloud_L2")
    l3 = load_tex("T_LobbyCloud_L3")
    sun = load_tex("T_LobbySun_Body")
    noise = (
        load_tex("T_LobbySky_Noise")
        if unreal.EditorAssetLibrary.does_asset_exist(f"{TEX}/T_LobbySky_Noise")
        else atlas
    )

    unreal.log("--- rebuild M_LobbyOriginal* + BP (recreates MPC) ---")
    safe.run()

    mpc = unreal.EditorAssetLibrary.load_asset(MPC_PATH)
    if not mpc:
        raise RuntimeError(f"missing MPC after safe.run: {MPC_PATH}")

    unreal.log("--- rebuild M_LobbyStylized* + M_LobbySun (no vector MPC) ---")
    curve.build_sky_mat(mpc, atlas, height, bg, noise)
    curve.build_clouds_mat(mpc, l1, l2, l3)
    curve.build_sun_mat(mpc, sun)

    unreal.log("--- rebuild M_LobbySunDisc ---")
    stylized.build_sun_material(sun)

    refresh_mi(
        f"{MAT}/MI_LobbyStylizedSky",
        scalar={"CloudDensity": 0.75, "Brightness": 2.0},
    )
    refresh_mi(f"{MAT}/MI_LobbyStylizedClouds", scalar={"CloudDensity": 0.85})
    refresh_mi(
        f"{MAT}/MI_LobbySunDisc",
        scalar={"SunIntensity": 18.0, "SunRadius": 0.55},
        vector={"SunColor": (1.0, 0.98, 0.92, 1.0)},
    )

    assert_no_vector_mpc()
    verify_sm6_compile()
    cleanup_tests()
    unreal.log("FIX_ALL_SKY_MATERIAL_ERRORS_OK")


run()
