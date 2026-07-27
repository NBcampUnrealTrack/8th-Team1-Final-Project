"""Reload md_sky_mpc on every exec — UE caches Python modules while the editor stays open."""
import importlib

import md_sky_mpc as _mod

_mod = importlib.reload(_mod)

all_world_contexts = _mod.all_world_contexts
editor_world = _mod.editor_world
material_library = _mod.material_library
set_mpc_scalar = _mod.set_mpc_scalar
read_mpc_scalar = _mod.read_mpc_scalar
push_scalars = _mod.push_scalars
push_midday_scalars = _mod.push_midday_scalars
set_mpc_asset_defaults = _mod.set_mpc_asset_defaults
map_shader_time = _mod.map_shader_time
