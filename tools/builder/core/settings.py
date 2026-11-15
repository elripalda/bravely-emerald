"""Remembered choices, in a json file next to the app (never the ROM contents)."""
import json
import os
import sys

from .constants import SETTING_KEYS

# ---------------------------------------------------------------------------------------------
def app_home():
    """Folder with the settings file: next to the app (frozen exe or source), else the user's config folder."""
    base = os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if os.environ.get("BRAVELY_BUILDER_HOME"):
        return os.environ["BRAVELY_BUILDER_HOME"]
    if os.access(base, os.W_OK):
        return base
    alt = os.path.join(os.path.expanduser("~"), ".config", "bravely-builder")
    os.makedirs(alt, exist_ok=True)
    return alt


def settings_file():
    return os.path.join(app_home(), "bravely_builder_settings.json")


def load_settings():
    try:
        with open(settings_file(), encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        data = {}
    return {k: v for k, v in data.items() if k in SETTING_KEYS and isinstance(v, SETTING_KEYS[k])}


def save_settings(new):
    cur = load_settings()
    for k, v in (new or {}).items():
        if k in SETTING_KEYS and isinstance(v, SETTING_KEYS[k]) and not (SETTING_KEYS[k] is int and isinstance(v, bool)):
            cur[k] = v
    tmp = settings_file() + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(cur, f, indent=2)
        os.replace(tmp, settings_file())
    except OSError:
        pass
    return cur
