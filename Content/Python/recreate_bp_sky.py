"""Recreate BP_Sky — delegates to full restore (settings + asset + level spawn)."""
import importlib
import sys

sys.modules.pop("restore_bp_sky_full", None)
import restore_bp_sky_full as restore

restore = importlib.reload(restore)
restore.main()
