#!/usr/bin/env python3
"""Packages the builder with PyInstaller (it must run on the OS it targets).

    python tools/builder/package.py [--skip-pyinstaller]

Windows: tools/builder/dist-builder/BravelyEmeraldBuilder/BravelyEmeraldBuilder.exe (one-folder app) and
         tools/builder/dist-builder/BravelyEmeraldBuilder-win.zip
Linux:   tools/builder/dist-builder/BravelyEmeraldBuilder-linux-x86_64.tar.gz holding the app folder,
         install.sh and the .desktop template (see packaging/). On the Steam Deck run
         packaging/build_linux_in_container.sh, which does this inside a fresh container.

The app carries a snapshot of the project source (see core/source.py, make_bundle below), so users need only a ROM.

Needs in the active Python: PySide6 (PySide6-Essentials is enough) and pyinstaller. An installer (Inno Setup or
NSIS) is optional and not built here; the zip is the Windows deliverable.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BUNDLE = os.path.join(HERE, "bundle")
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


def git(*args):
    return subprocess.run(["git", "-c", "core.autocrlf=false"] + list(args), cwd=REPO, check=True, capture_output=True).stdout


def make_bundle():
    """Snapshot of the tracked files of HEAD (git archive), minus what no build needs, as bundle/source.tar.xz + manifest."""
    from core.source import excluded, EXCLUDE_PREFIXES, KEEP_PREFIXES, ARCHIVE_NAME, MANIFEST_NAME
    commit = git("rev-parse", "HEAD").decode().strip()
    date = git("show", "-s", "--format=%cI", "HEAD").decode().strip()
    if git("status", "--porcelain", "--untracked-files=no").strip():
        print("WARNING: tracked files have uncommitted changes; the snapshot holds the committed state of %s only" % commit[:10])
    shutil.rmtree(BUNDLE, ignore_errors=True)
    os.makedirs(BUNDLE)
    archive = os.path.join(BUNDLE, ARCHIVE_NAME)
    proc = subprocess.Popen(["git", "-c", "core.autocrlf=false", "archive", "--format=tar", "HEAD"], cwd=REPO, stdout=subprocess.PIPE)
    count = size = skipped = 0
    with tarfile.open(fileobj=proc.stdout, mode="r|") as src, tarfile.open(archive, "w:xz", preset=6) as dst:
        for m in src:
            if not m.isfile():
                continue                                   # directories are implied; the tree has no links
            if excluded(m.name):
                skipped += 1
                continue
            m.uid = m.gid = 0
            m.uname = m.gname = ""
            dst.addfile(m, src.extractfile(m))
            count += 1
            size += m.size
    if proc.wait() != 0:
        sys.exit("git archive failed")
    h = hashlib.sha256()
    with open(archive, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    manifest = {"format": 1, "commit": commit, "date": date, "built": __import__("time").strftime("%Y-%m-%dT%H:%M:%S"),
                "files": count, "bytes": size, "archive": ARCHIVE_NAME, "sha256": h.hexdigest(),
                "excluded_prefixes": list(EXCLUDE_PREFIXES), "kept_prefixes": list(KEEP_PREFIXES), "excluded_files": skipped}
    with open(os.path.join(BUNDLE, MANIFEST_NAME), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print("bundle: %s files, %.1f MB unpacked, %.1f MB compressed (%s), %d files left out, commit %s"
          % (count, size / 1048576, os.path.getsize(archive) / 1048576, ARCHIVE_NAME, skipped, commit[:10]))
    return manifest


def run_pyinstaller():
    try:
        import PyInstaller.__main__ as pyi
    except ImportError:
        sys.exit("PyInstaller is not installed in this Python: pip install pyinstaller PySide6-Essentials")
    args = ["--noconfirm", "--clean", "--windowed", "--name", NAME, "--distpath", OUT, "--workpath", WORK, "--specpath", WORK,
            "--paths", HERE, "--add-data", BUNDLE + os.pathsep + "bundle",
            "--add-data", os.path.join(REPO, "docs", "BUILDER_HELP.md") + os.pathsep + "help",
            "--add-data", os.path.join(HERE, "fonts") + os.pathsep + "fonts"]
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
    make_bundle()
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
