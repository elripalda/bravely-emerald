"""Facts taken from the repository's own docs and scripts."""
import os
import re
import sys

# --- facts taken from the repository's own docs and scripts ---------------------------------
ROM_SIZE = 16 * 1024 * 1024            # fetch_assets.sh: size 16777216
ROM_TITLE = b"POKEMON EMER"            # offset 0xA0, 12 bytes
EMERALD_US_SHA1 = "f3ae088181bf583e55daf962a92bb46f4f1d07b7"   # fetch_assets.sh EMERALD_US_SHA1
MSYS_PACKAGES = [                       # docs/WINDOWS_BUILD.md, Rebuild step 1
    "make", "git", "cmake", "tar", "patch", "diffutils", "python", "zip", "unzip",
    "mingw-w64-i686-toolchain", "mingw-w64-i686-cmake", "mingw-w64-i686-ninja",
    "mingw-w64-x86_64-gcc", "mingw-w64-x86_64-libpng", "mingw-w64-x86_64-pkgconf",
    "mingw-w64-x86_64-python-pillow",
]
MSYS_INSTALLER_URL = "https://github.com/msys2/msys2-installer/releases/latest/download/msys2-x86_64-latest.exe"
MSYS_INSTALLER_FILE = "msys2-x86_64-latest.exe"
MSYS_INSTALLER_HOST_OK = ("https://github.com/msys2/msys2-installer/",)
MIN_FREE_GB = 15
HARD_MIN_FREE_GB = 4

SETTING_KEYS = {"targets": list, "rom": str, "dir": str, "jobs": int, "low_priority": bool,
                "run_test": bool, "container": str, "msys_root": str}
CONTAINER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,40}$")

LEGAL_NOTE = ("Build from your own Pokemon Emerald cartridge dump only. The result contains Nintendo's game data: "
              "use it yourself and do not share it. This repository and this app hold no ROM and no game data.")

REPO_URL = "https://github.com/marcoscodas/bravely-emerald"
