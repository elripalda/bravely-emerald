#!/bin/bash
# Builds the Linux package on a Debian (or Ubuntu) system, e.g. WSL Debian: the oldest glibc you have gives the widest range
# of distros the binary runs on. PyInstaller must run on the OS it targets.
#
#   bash tools/builder/packaging/build_linux_debian.sh [--venv DIR]
#
# Needs: python3, python3-venv, python3-pip, git, xz-utils and the Qt xcb runtime libraries (apt: libgl1 libegl1 libxkbcommon0
# libxkbcommon-x11-0 libdbus-1-3 libfontconfig1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0
# libxcb-shape0 libxcb-xinerama0 libxcb-xkb1). Run it on a copy of the repository inside the Linux filesystem (not /mnt/c).
# The venv lives outside the repository (default ~/bb-venv). Output: tools/builder/dist-builder/BravelyEmeraldBuilder-linux-x86_64.tar.gz
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
VENV="$HOME/bb-venv"
while [ $# -gt 0 ]; do
    case "$1" in
        --venv) VENV="${2:?}"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 1 ;;
    esac
done
[ -d "$VENV" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install -q PySide6-Essentials pyinstaller
cd "$REPO"
"$VENV/bin/python" tools/builder/package.py
ldd --version | head -1
ls -l "$REPO/tools/builder/dist-builder/"*.tar.gz
