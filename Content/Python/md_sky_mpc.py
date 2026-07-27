"""Push SoStylized MPC scalars via MaterialLibrary (UE 5.8+ compatible)."""
import unreal


def editor_world():
    sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if sub:
        world = sub.get_editor_world()
        if world:
            return world
    try:
        return unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        return None


def material_library():
    # UKismetMaterialLibrary script name in Python
    if hasattr(unreal, "MaterialLibrary"):
        return unreal.MaterialLibrary
    return unreal.KismetMaterialLibrary


def set_mpc_scalar(world, mpc, name, value):
    material_library().set_scalar_parameter_value(world, mpc, name, float(value))


def all_world_contexts():
    """Editor world + active PIE/game world (MPC instances are per-world)."""
    contexts = []
    seen = set()

    def add(world):
        if not world:
            return
        key = str(world.get_path_name()) if hasattr(world, "get_path_name") else id(world)
        if key not in seen:
            seen.add(key)
            contexts.append(world)

    sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if sub:
        add(sub.get_editor_world())
        for getter_name in ("get_game_world", "get_play_world"):
            try:
                add(getattr(sub, getter_name)())
            except Exception:
                pass

    for getter in (
        lambda: unreal.EditorLevelLibrary.get_editor_world(),
        lambda: unreal.EditorLevelLibrary.get_play_world(),
    ):
        try:
            add(getter())
        except Exception:
            pass

    return contexts


def read_mpc_scalar(mpc, name, world=None):
    world = world or editor_world()
    if not world or not mpc:
        return None
    try:
        return material_library().get_scalar_parameter_value(world, mpc, name)
    except Exception:
        return None


def map_shader_time(clock_hours, offset=0.0):
    return (float(clock_hours) + float(offset)) % 24.0


def push_scalars(mpc, scalars, world=None):
    worlds = [world] if world else all_world_contexts()
    if not worlds:
        unreal.log_warning("md_sky_mpc: no world contexts")
        return False
    if not mpc:
        unreal.log_warning("md_sky_mpc: no MPC asset")
        return False

    ok = False
    for ctx in worlds:
        for pname, val in scalars.items():
            set_mpc_scalar(ctx, mpc, pname, val)
        ok = True
        ctx_name = ctx.get_name() if hasattr(ctx, "get_name") else str(ctx)
        current = read_mpc_scalar(mpc, "Current Time", ctx)
        unreal.log(f"MPC {mpc.get_name()} world={ctx_name} Current Time={current}")
    return ok


def push_midday_scalars(mpc, midday=12.0, world=None, day_start=0.0, shader_offset=12.0):
    shader_time = map_shader_time(midday, shader_offset)
    ok = push_scalars(
        mpc,
        {
            "Current Time": shader_time,
            "Day Cycle Progress": shader_time / 24.0,
            "Orbit Orientation": midday * 15.0 - 90.0,
            "Orbit Inclination": 28.0,
            "Day Length": 600.0,
            "Night Length": 480.0,
            "Day Start Time": day_start,
        },
        world,
    )
    if ok:
        unreal.log(
            f"MPC pushed {mpc.get_path_name()} clock={midday} shader_time={shader_time} day_start={day_start}"
        )
    return ok


def set_mpc_asset_defaults(mpc, midday=12.0):
    scalars = list(mpc.get_editor_property("scalar_parameters"))
    changed = False
    for entry in scalars:
        name = str(entry.get_editor_property("parameter_name"))
        if name == "Current Time":
            entry.set_editor_property("default_value", midday)
            changed = True
        elif name == "Day Cycle Progress":
            entry.set_editor_property("default_value", midday / 24.0)
            changed = True
        elif name == "Orbit Orientation":
            entry.set_editor_property("default_value", midday * 15.0 - 90.0)
            changed = True
        elif name == "Orbit Inclination":
            entry.set_editor_property("default_value", 28.0)
            changed = True
    if changed:
        mpc.set_editor_property("scalar_parameters", scalars)
        unreal.EditorAssetLibrary.save_asset(mpc.get_path_name().split(".")[0])
    return changed
