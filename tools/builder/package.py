#!/usr/bin/env python3
"""Packages the builder with PyInstaller (it must run on the OS it targets).

    python tools/builder/package.py [--skip-pyinstaller]

Windows: tools/builder/dist-builder/BravelyEmeraldBuilder/BravelyEmeraldBuilder.exe (one-folder app) and
         tools/builder/dist-builder/BravelyEmeraldBuilder-win.zip
Linux:   tools/builder/dist-builder/BravelyEmeraldBuilder-linux-x86_64.tar.gz holding the app folder,
         install.sh and the .desktop template (see packaging/). On the Steam Deck run
         packaging/build_linux_in_container.sh, which does this inside a fresh container.

Needs in the active Python: PySide6 (PySide6-Essentials is enough) and pyinstaller. An installer (Inno Setup or
NSIS) is optional and not built here; the zip is the Windows deliverable.
"""
import argparse
import os
import shutil
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "dist-builder")
WORK = os.path.join(HERE, "build-builder")
NAME = "BravelyEmeraldBuilder"

# Qt parts the app does not use; excluded so the folder stays small
EXCLUDES = ["PySide6.QtWebEngineCore", "PySide6.QtWebEngineWidgets", "PySide6.QtWebEngineQuick", "PySide6.QtQuick", "PySide6.QtQml",
            "PySide6.Qt3DCore", "PySide6.QtMultimedia", "PySide6.QtCharts", "PySide6.QtDataVisualization", "PySide6.QtPdf",
            "PySide6.QtSql", "PySide6.QtTest", "PySide6.QtBluetooth", "PySide6.QtNfc", "PySide6.QtSensors", "tkinter", "unittest"]

README = """Bravely Emerald builder
=======================
Run {exe}. The app builds Pokemon Emerald (the Steam Deck / Linux 32-bit port or the Windows port) from your own
ROM dump. It needs a clone of the repository (https://github.com/marcoscodas/bravely-emerald): on first run choose an
existing clone or press "Clone repository". The ROM is only read to check it; nothing is uploaded.
The result contains Nintendo's game data: use it yourself and do not share it.
Documentation: docs/BUILDER.md in the repository.
"""


def run_pyinstaller():
    try:
        import PyInstaller.__main__ as pyi
    except ImportError:
        sys.exit("PyInstaller is not installed in this Python: pip install pyinstaller PySide6-Essentials")
    args = ["--noconfirm", "--clean", "--windowed", "--name", NAME, "--distpath", OUT, "--workpath", WORK, "--specpath", WORK,
            "--paths", HERE]
    for m in EXCLUDES:
        args += ["--exclude-module", m]
    args.append(os.path.join(HERE, "app.py"))
    pyi.run(args)


def package_windows():
    folder = os.path.join(OUT, NAME)
    exe = os.path.join(folder, NAME + ".exe")
    if not os.path.isfile(exe):
        sys.exit("PyInstaller did not produce %s" % exe)
    with open(os.path.join(folder, "README.txt"), "w", encoding="utf-8") as f:
        f.write(README.format(exe=NAME + ".exe"))
    zp = os.path.join(OUT, NAME + "-win.zip")
    if os.path.exists(zp):
        os.remove(zp)
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for root, _, files in os.walk(folder):
            for fn in files:
                full = os.path.join(root, fn)
                z.write(full, os.path.join(NAME, os.path.relpath(full, folder)))
    print("zip: %s (%.1f MB)" % (zp, os.path.getsize(zp) / 1048576))
    print("folder: %s (%.1f MB)" % (folder, dir_size(folder) / 1048576))
    print("No installer was built (Inno Setup or NSIS are optional later); distribute the zip.")
    return zp


def package_linux():
    folder = os.path.join(OUT, NAME)
    exe = os.path.join(folder, NAME)
    if not os.path.isfile(exe):
        sys.exit("PyInstaller did not produce %s" % exe)
    with open(os.path.join(folder, "README.txt"), "w", encoding="utf-8") as f:
        f.write(README.format(exe=NAME))
    stage = os.path.join(WORK, "linux-stage")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    shutil.copytree(folder, os.path.join(stage, NAME), symlinks=True)
    for fn in ("install.sh", "bravely-emerald-builder.desktop.in"):
        shutil.copy(os.path.join(HERE, "packaging", fn), os.path.join(stage, fn))
    os.chmod(os.path.join(stage, "install.sh"), 0o755)
    tg = os.path.join(OUT, NAME + "-linux-x86_64.tar.gz")
    with tarfile.open(tg, "w:gz") as t:
        t.add(stage, arcname=NAME + "-linux-x86_64")
    print("tar.gz: %s (%.1f MB)" % (tg, os.path.getsize(tg) / 1048576))
    return tg


def dir_size(path):
    return sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(path) for f in fs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--skip-pyinstaller", action="store_true", help="only (re)make the archive from an existing build")
    a = ap.parse_args()
    if not a.skip_pyinstaller:
        run_pyinstaller()
    if sys.platform.startswith("win"):
        package_windows()
    elif sys.platform.startswith("linux"):
        package_linux()
    else:
        sys.exit("Packaging is supported on Windows and Linux only.")


if __name__ == "__main__":
    main()
