"""Export current BP_Sky / level instance settings to Saved/BP_Sky/lobby_profile.json."""
from __future__ import annotations

import importlib
import sys

import unreal

for _name in ("md_sky_bp_profile", "restore_bp_sky_full"):
    sys.modules.pop(_name, None)

import restore_bp_sky_full as restore

restore = importlib.reload(restore)


def main() -> None:
    actor = restore.find_level_sky_actor()
    harvested = restore.harvest_from_actor(actor) if actor else None
    saved = restore.load_profile_file()
    import md_sky_bp_profile as profile_mod

    profile = restore.merge_profiles(profile_mod.LOBBY_DEFAULTS, saved, harvested)
    restore.save_profile_file(profile)
    unreal.log(
        f"EXPORT_BP_SKY_PROFILE_OK actor={bool(actor)} "
        f"keys={len(profile.get('sky_controller', {}))}"
    )


if __name__ == "__main__":
    main()
