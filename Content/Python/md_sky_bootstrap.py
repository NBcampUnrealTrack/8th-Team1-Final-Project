"""Bootstrap for UE 5.8 sky scripts — call purge_md_sky_modules() before importing md_sky_import."""
import importlib
import sys


def purge_md_sky_modules():
    for name in ("md_sky_mpc", "md_sky_import"):
        sys.modules.pop(name, None)


def load_md_sky_mpc():
    purge_md_sky_modules()
    import md_sky_import as mpc
    return importlib.reload(mpc)
