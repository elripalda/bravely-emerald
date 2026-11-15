#!/usr/bin/env python3
"""Headless driver for the builder core (no UI): checks, ROM validation and builds from a terminal.

    python tools/builder/cli.py checks   [--repo DIR]
    python tools/builder/cli.py rom FILE
    python tools/builder/cli.py build --target windows|linux32 [--rom FILE] [--jobs N] [--low] [--dir DIR] [--repo DIR] [--dry-run]
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from core import Builder, validate_rom          # noqa: E402
from core.repo import guess_repo                # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("checks", "build"):
        p = sub.add_parser(name)
        p.add_argument("--repo", default=guess_repo())
        p.add_argument("--dry-run", action="store_true")
    sub.choices["build"].add_argument("--target", required=True, choices=["windows", "linux32"])
    sub.choices["build"].add_argument("--rom", default="")
    sub.choices["build"].add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    sub.choices["build"].add_argument("--low", action="store_true", help="low priority")
    sub.choices["build"].add_argument("--dir", default="")
    sub.choices["build"].add_argument("--container", default="emerald-build")
    sub.choices["build"].add_argument("--run-test", action="store_true")
    r = sub.add_parser("rom")
    r.add_argument("file")
    a = ap.parse_args(argv)
    if a.cmd == "rom":
        v = validate_rom(a.file)
        print(json.dumps(v, indent=2))
        return 0 if v["ok"] else 1
    b = Builder(a.repo, a.dry_run)
    if a.cmd == "checks":
        for c in b.checks():
            print("%-5s %-10s %s" % (c["status"], c["id"], c["detail"]))
        return 0
    opts = {"targets": [a.target], "rom": a.rom, "jobs": a.jobs, "low_priority": a.low, "dir": a.dir,
            "container": a.container, "run_test": a.run_test}
    try:
        label, stages, steps, meta = b.plan_build(opts)
    except ValueError as e:
        print("ERROR:", e)
        return 2
    meta["container"] = a.container
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
    ev = done[0]
    print("@@ final:", ev["state"], json.dumps(t.result) if t.result else "")
    for h in t.hints:
        print("@@ hint:", h["title"], "-", h["fix"])
    return 0 if ev["state"] == "done" else 1


if __name__ == "__main__":
    sys.exit(main())
