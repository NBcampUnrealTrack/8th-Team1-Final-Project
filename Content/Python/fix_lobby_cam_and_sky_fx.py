"""Fix Lobby viewport (roll=0, correct pitch/yaw) and permanently strip sky FX."""
from __future__ import annotations

import unreal

LEVEL = "/Game/Developers/TaitoP/Maps/L_Lobby"
OWNED_BP = "/Game/Developers/TaitoP/Environment/Sky/Owned/Environment/Sky/BP_StylizedSky"


def set_cam(loc, pitch, yaw, roll=0.0):
    rot = unreal.Rotator(pitch=float(pitch), yaw=float(yaw), roll=float(roll))
    try:
        ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        ues.set_level_viewport_camera_info(loc, rot)
        unreal.log(f"cam via UES pitch={pitch} yaw={yaw} roll={roll}")
        return
    except Exception as e:
        unreal.log(f"UES cam fail: {e}")
    try:
        unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, rot)
        unreal.log(f"cam via ELL pitch={pitch} yaw={yaw} roll={roll}")
    except Exception as e:
        unreal.log_warning(f"cam fail: {e}")


def straighten_viewport():
    loc = unreal.Vector(6543.06, 12378.60, 5635.81)
    pitch, yaw = -8.0, 103.8
    try:
        ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        cur_loc, cur_rot = ues.get_level_viewport_camera_info()
        loc = cur_loc
        # If previous bug swapped axes (pitch~100, yaw~0), recover from yaw-looking pitch
        p = float(cur_rot.pitch)
        y = float(cur_rot.yaw)
        r = float(cur_rot.roll)
        unreal.log(f"cur cam p={p} y={y} r={r}")
        if abs(y) < 5.0 and abs(p) > 45.0:
            # recover: previous script put yaw into pitch
            yaw = p
            pitch = -8.0
        else:
            pitch = p if abs(p) < 89 else -8.0
            yaw = y
    except Exception as e:
        unreal.log(f"get cam: {e}")
    set_cam(loc, pitch, yaw, 0.0)


def disable_comp(c, label=""):
    name = c.get_name()
    try:
        if hasattr(c, "set_visibility"):
            c.set_visibility(False, True)
        if hasattr(c, "set_hidden_in_game"):
            c.set_hidden_in_game(True, True)
        if hasattr(c, "deactivate"):
            c.deactivate()
        if hasattr(c, "stop"):
            c.stop()
        if hasattr(c, "set_active"):
            c.set_active(False, True)
        for p in ("volume_multiplier", "volume"):
            try:
                c.set_editor_property(p, 0.0)
            except Exception:
                pass
        # Also clear mesh/system refs where possible so they don't respawn visually
        for p in ("static_mesh", "asset", "niagara_system_override"):
            try:
                c.set_editor_property(p, None)
            except Exception:
                pass
        unreal.log(f"disabled {label or name}")
    except Exception as e:
        unreal.log(f"disable fail {name}: {e}")


def strip_actor(actor):
    for name, val in (
        ("dynamicWeather?", False),
        ("shootingStars?", False),
        ("meteorShower?", False),
        ("rainbow?", False),
        ("planetRing?", False),
        ("rings?", False),
        ("audio?", False),
        ("sounds?", False),
    ):
        try:
            actor.set_editor_property(name, val)
            unreal.log(f"set {name}={val}")
        except Exception:
            pass

    for c in list(actor.get_components_by_class(unreal.ActorComponent)):
        name = c.get_name()
        cls = c.get_class().get_name()
        low = (name + " " + cls).lower()
        kill = any(
            k in low
            for k in (
                "ring",
                "rainbow",
                "planet",
                "meteor",
                "shoot",
                "star",
                "audio",
                "sound",
                "niagara",
                "lightning",
                "rain",
                "wind",
                "trash_audio",
            )
        )
        # Keep sky dome / clouds / sun / lights / fog / pp
        keep = any(
            k in low
            for k in (
                "skydome",
                "cloud",
                "sun",
                "moon",
                "directionallight",
                "skylight",
                "fog",
                "postprocess",
                "orbit",
                "root",
            )
        )
        if keep and "ring" not in low and "niagara" not in low and "audio" not in low:
            continue
        if kill or isinstance(c, unreal.AudioComponent) or "Niagara" in cls:
            # don't kill if it's the main sky dome mistakenly
            if "skydome" in low and "cloud" not in name.lower() and "ring" not in low:
                continue
            disable_comp(c)


def strip_bp_defaults():
    """Hide Ring / FX on the Owned BP CDO so they stay off after reload."""
    if not unreal.EditorAssetLibrary.does_asset_exist(OWNED_BP):
        unreal.log_warning(f"missing {OWNED_BP}")
        return
    bp = unreal.EditorAssetLibrary.load_asset(OWNED_BP)
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    bfl = unreal.SubobjectDataBlueprintFunctionLibrary
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        data = bfl.get_data(h)
        try:
            obj = bfl.get_associated_object(data)
        except Exception:
            obj = bfl.get_object(data)
        if not obj:
            continue
        name = obj.get_name()
        low = name.lower()
        if any(k in low for k in ("ring", "shoot", "meteor", "lightning", "rain", "wind", "audio", "sound", "star")):
            try:
                if hasattr(obj, "set_visibility"):
                    obj.set_editor_property("visible", False)
            except Exception:
                pass
            try:
                obj.set_editor_property("b_visible", False)
            except Exception:
                pass
            try:
                if isinstance(obj, unreal.StaticMeshComponent):
                    obj.set_editor_property("static_mesh", None)
                unreal.log(f"BP cleared {name}")
            except Exception as e:
                unreal.log(f"BP clear {name}: {e}")
            try:
                obj.set_editor_property("component_class", None)
            except Exception:
                pass
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_asset(OWNED_BP)
    unreal.log("BP defaults stripped")


def run():
    straighten_viewport()
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label() or ""
        cls = a.get_class().get_name()
        if lab == "LobbyStylizedSky" or "BP_StylizedSky" in cls:
            unreal.log(f"strip {lab}")
            strip_actor(a)
    strip_bp_defaults()
    # Respawn so BP defaults apply cleanly
    bp = unreal.EditorAssetLibrary.load_asset(OWNED_BP)
    if bp:
        for a in list(eas.get_all_level_actors()):
            lab = a.get_actor_label() or ""
            cls = a.get_class().get_name()
            if lab == "LobbyStylizedSky" or "BP_StylizedSky" in cls:
                eas.destroy_actor(a)
        actor = eas.spawn_actor_from_object(bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        actor.set_actor_label("LobbyStylizedSky")
        try:
            actor.set_folder_path("Environment/Sky")
        except Exception:
            pass
        for name, val in (
            ("dayCycleEnabled?", True),
            ("freezeAllTime?", True),
            ("start Time", 12.0),
            ("dynamicWeather?", False),
            ("clouds?", True),
        ):
            try:
                actor.set_editor_property(name, val)
            except Exception:
                pass
        strip_actor(actor)
    straighten_viewport()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.EditorAssetLibrary.save_asset(LEVEL)
    unreal.log("FIX_CAM_AND_FX_OK")


run()
