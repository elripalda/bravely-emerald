"""UI-agnostic core of the Bravely Emerald builder (no Qt, no web). See docs/BUILDER.md."""
from .builder import Builder
from .constants import LEGAL_NOTE, REPO_URL
from .errors import explain
from .host import detect_host
from .settings import load_settings, save_settings
from .validate import validate_rom, clean_path

__all__ = ["Builder", "LEGAL_NOTE", "REPO_URL", "explain", "detect_host", "load_settings", "save_settings",
           "validate_rom", "clean_path"]
