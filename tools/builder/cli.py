#!/usr/bin/env python3
"""Headless driver of the builder core (no window). Also reachable as flags of the app itself, so the packaged
exe can be tested without a display:  BravelyEmeraldBuilder.exe --selftest   |   --build --rom X --out DIR

    cli.py checks   [--repo DIR]
    cli.py rom FILE
    cli.py source   [--repo DIR] [--force]     (resolve the source; extract the bundle)
    cli.py selftest
    cli.py build --target windows|linux32 [--rom FILE] [--jobs N] [--low] [--out DIR] [--repo DIR] [--dry-run]

Without --repo the source is the bundled snapshot (extracted into the work folder), else a checkout (see core/source.py).
"""
import argparse
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from core import Builder, validate_rom          # noqa: E402
from core.source import Source, SourceError     # noqa: E402


def setup_stdout():
    """A windowed (no console) exe has no stdout: use the redirected handle if there is one, else a log file."""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        return
    try:
        sys.stdout = open(1, "w", encoding="utf-8", errors="replace", closefd=False)
        sys.stderr = sys.stdout
    except OSError:
        from core.settings import app_home
        sys.stdout = sys.stderr = open(os.path.join(app_home(), "cli.log"), "w", encoding="utf-8")


def make_builder(repo_arg, dry_run=False, force=False):
    src = Source(force_repo=repo_arg)
    print("source:", src.describe(), flush=True)
    path = src.prepare(force=force, progress=lambda m: print("  " + m, flush=True))
    print("repository:", path, flush=True)
    return Builder(path, dry_run), src


def write_synthetic_rom(path):
    buf = bytearray(16 * 1024 * 1024)
    buf[0xA0:0xAC] = b"POKEMON EMER"
    buf[0xAC:0xB0] = b"BPEE"
    buf[0xBD] = (0 - (sum(buf[0xA0:0xBD]) + 25)) & 255
    with open(path, "wb") as f:
        f.write(buf)


def run_task(b, label, stages, steps, meta):
    done = []

    def on_event(ev):
        if ev["type"] == "log":
            print(ev["text"], flush=True)
        elif ev["type"] == "stage":
            print("@@ stage:", ev["stage"], flush=True)
        elif ev["type"] == "state":
            done.append(ev)

    t = b.start(label, stages, steps, meta, "build", on_event)
    try:
        while not done:
            time.sleep(0.5)
    except KeyboardInterrupt:
        b.cancel()
        while not done:
            time.sleep(0.5)
    print("@@ final:", done[0]["state"], json.dumps(t.result) if t.result else "", flush=True)
    for h in t.hints:
        print("@@ hint:", h["title"], "-", h["fix"], flush=True)
    return 0 if done[0]["state"] == "done" else 1, t


def selftest():
    """Extract the bundled source, check it, and dry-run the build of this host's target (prints the commands)."""
    b, src = make_builder(None, dry_run=True)
    if src.mode != "bundled":
        print("SELFTEST FAILED: the source is not the bundled snapshot (mode %s)" % src.mode)
        return 1
    for rel in ("fetch_assets.sh", "CMakeLists.txt", "assets/voxel/buildings.bin", "tools/windows/build.sh", "tools/deck/build_deck.sh",
                "design/stitch/_tools/export_assets.py", "vita/assets.mk"):
        ok = os.path.isfile(os.path.join(b.repo, *rel.split("/")))
        print("  %-40s %s" % (rel, "present" if ok else "MISSING"))
        if not ok:
            print("SELFTEST FAILED")
            return 1
    print("manifest:", json.dumps(src.manifest))
    target = "windows" if b.host["kind"] == "windows" else "linux32"
    rom = os.path.join(tempfile.mkdtemp(prefix="bb-selftest-"), "synthetic.gba")
    write_synthetic_rom(rom)
    opts = {"targets": [target], "rom": rom, "jobs": 2, "low_priority": True, "force_assets": True,
            "dir": os.path.join(os.path.dirname(rom), "out")}
    label, stages, steps, meta = b.plan_build(opts)
    print("stages:", " > ".join(stages))
    rc, t = run_task(b, label, stages, steps, meta)
    print("SELFTEST OK" if rc == 0 else "SELFTEST FAILED")
    return rc


def main(argv=None):
    setup_stdout()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("checks", "build", "source"):
        p = sub.add_parser(name)
        p.add_argument("--repo", default=None)
        p.add_argument("--dry-run", action="store_true")
    sub.choices["source"].add_argument("--force", action="store_true")
    bp = sub.choices["build"]
    bp.add_argument("--target", default=None, choices=["windows", "linux32"])
    bp.add_argument("--rom", default="")
    bp.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    bp.add_argument("--low", action="store_true", help="low priority")
    bp.add_argument("--out", default="", help="install folder (the game is copied there)")
    bp.add_argument("--container", default="emerald-build")
    bp.add_argument("--run-test", action="store_true")
    sub.add_parser("selftest")
    r = sub.add_parser("rom")
    r.add_argument("file")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "rom":
            v = validate_rom(a.file)
            print(json.dumps(v, indent=2))
            return 0 if v["ok"] else 1
        if a.cmd == "selftest":
            return selftest()
        if a.cmd == "source":
            make_builder(a.repo, a.dry_run, a.force)
            return 0
        b, _ = make_builder(a.repo, a.dry_run)
        if a.cmd == "checks":
            for c in b.checks():
                print("%-5s %-10s %s" % (c["status"], c["id"], c["detail"]))
            return 0
        target = a.target or ("windows" if b.host["kind"] == "windows" else "linux32")
        opts = {"targets": [target], "rom": a.rom, "jobs": a.jobs, "low_priority": a.low, "dir": a.out,
                "container": a.container, "run_test": a.run_test}
        try:
            label, stages, steps, meta = b.plan_build(opts)
        except ValueError as e:
            print("ERROR:", e)
            return 2
        meta["container"] = a.container
        return run_task(b, label, stages, steps, meta)[0]
    except SourceError as e:
        print("ERROR:", e)
        return 3


if __name__ == "__main__":
    sys.exit(main())
