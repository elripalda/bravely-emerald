#!/bin/bash
# Builds the Linux package of the builder inside a distrobox container (PyInstaller must run on the target OS).
# Meant for the Steam Deck / Arch based containers. Needs no root on the host: pacman runs inside the container.
#
#   bash tools/builder/packaging/build_linux_in_container.sh [--container NAME] [--keep]
#
# Creates a NEW container (default name bravely-builder-pkg), installs python, makes a venv in the container's
# copy of the work folder, runs tools/builder/package.py, leaves the tar.gz in tools/builder/dist-builder/,
# then removes the container (unless --keep). Never touches other containers.
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
NAME=bravely-builder-pkg
KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --container) NAME="${2:?}"; shift 2 ;;
        --keep) KEEP=1; shift ;;
        *) echo "unknown argument $1" >&2; exit 1 ;;
    esac
done
case "$NAME" in emerald-build) echo "refusing to use the game's build container" >&2; exit 1 ;; esac
command -v distrobox >/dev/null || { echo "distrobox not found" >&2; exit 1; }
distrobox create --yes --name "$NAME" --image docker.io/library/archlinux:latest
cleanup() { [ "$KEEP" -eq 1 ] || distrobox rm --force "$NAME" >/dev/null 2>&1 || true; }
trap cleanup EXIT
distrobox enter --name "$NAME" --no-tty -- bash -lc "
set -e
sudo pacman -Syu --noconfirm
sudo pacman -S --needed --noconfirm python python-pip tar gzip \
    libxkbcommon libxkbcommon-x11 xcb-util-cursor xcb-util-image xcb-util-keysyms xcb-util-renderutil xcb-util-wm \
    libglvnd fontconfig freetype2 dbus
cd '$REPO'
python -m venv --clear /tmp/bb-venv
/tmp/bb-venv/bin/pip install PySide6-Essentials pyinstaller
/tmp/bb-venv/bin/python tools/builder/package.py
"
ls -l "$REPO/tools/builder/dist-builder/"*.tar.gz
