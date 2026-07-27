"""
Auto-run once per editor session: straighten L_Lobby viewport + strip sky FX.
"""
from __future__ import annotations

import unreal

_FLAG = "MD_LobbySkyFxStripped"


def _already() -> bool:
    try:
        return bool(unreal.SystemLibrary.get_console_variable_int_value(_FLAG))
    except Exception:
        return False


def _mark():
    try:
        unreal.SystemLibrary.execute_console_command(None, f"{_FLAG} 1")
    except Exception:
        pass


def _run():
    if _already():
        return
    # Only when Lobby is current
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world and "L_Lobby" not in world.get_path_name():
            return
    except Exception:
        pass
    script = r"E:/Unreal/MarineDemolition/Content/Python/strip_lobby_sky_fx_and_level_cam.py"
    try:
        unreal.PythonScriptLibrary.execute_python_command(f'exec(open(r"{script}", encoding="utf-8").read())')
        _mark()
    except Exception:
        try:
            import runpy

            runpy.run_path(script, run_name="__main__")
            _mark()
        except Exception as e:
            unreal.log_warning(f"init strip sky failed: {e}")


try:
    _run()
except Exception as e:
    unreal.log_warning(f"init_unreal: {e}")
