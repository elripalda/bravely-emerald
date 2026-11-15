#!/usr/bin/env python3
"""Bravely Emerald builder: desktop app (PySide6). Builds the game from your own ROM.

    pip install PySide6          (PySide6-Essentials is enough)
    python tools/builder/app.py [--repo DIR] [--dry-run] [--smoke]

--selftest extracts the bundled source and dry-runs this host's build; --build --rom X --out DIR builds without a window.
--smoke opens the window, lets it run for a moment and quits (used to test the packaged app).
The logic is in tools/builder/core (no Qt); the windows are in tools/builder/ui. See docs/BUILDER.md.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)


BASE_HEIGHT = 392


def screen_height():
    """Primary screen height in pixels, found without Qt (the scale must be set before Qt starts). 0 when unknown."""
    try:
        if os.name == "nt":
            import ctypes
            return int(ctypes.windll.user32.GetSystemMetrics(1))
        import re
        import subprocess
        out = subprocess.run(["xrandr", "--current"], capture_output=True, text=True, timeout=3).stdout
        m = re.search(r"current \d+ x (\d+)", out)
        return int(m.group(1)) if m else 0
    except Exception:
        return 0


def set_integer_scale():
    """The whole UI is drawn at 1x and enlarged by a whole number only, so bevels stay crisp pixels.
    BRAVELY_SCALE=1..4 overrides: the default is the largest whole number that keeps the window on the screen."""
    if os.environ.get("QT_SCALE_FACTOR") or os.environ.get("QT_QPA_PLATFORM") == "offscreen" and not os.environ.get("BRAVELY_SCALE"):
        return
    env = os.environ.get("BRAVELY_SCALE")
    if env and env.isdigit() and 1 <= int(env) <= 4:
        scale = int(env)
    else:
        h = screen_height()
        scale = max(1, min(4, (h - 80) // BASE_HEIGHT)) if h else 2
    os.environ["QT_SCALE_FACTOR"] = str(scale)
    os.environ["QT_SCALE_FACTOR_ROUNDING_POLICY"] = "Floor"


def main(argv=None):
    # headless modes, used to test the packaged exe: they run the core without a window (see cli.py)
    if argv is None:
        argv = sys.argv[1:]
    if "--selftest" in argv:
        import cli
        return cli.main(["selftest"])
    if "--build" in argv:
        import cli
        rest = [a for a in argv if a != "--build"]
        return cli.main(["build"] + rest)
    ap = argparse.ArgumentParser(description="Bravely Emerald builder")
    ap.add_argument("--repo", default=None, help="folder of the repository (default: auto-detect, else asked in the window)")
    ap.add_argument("--dry-run", action="store_true", help="show the commands in the log, execute nothing")
    ap.add_argument("--smoke", action="store_true", help="start, create the window, quit after a second (self test)")
    args = ap.parse_args(argv)
    set_integer_scale()
    try:
        from PySide6.QtCore import QTimer
        from PySide6.QtWidgets import QApplication
    except ImportError:
        sys.exit("PySide6 is needed: pip install PySide6   (or use the packaged BravelyEmeraldBuilder)")
    from ui import win95
    from ui.main_window import MainWindow
    app = QApplication(sys.argv[:1])
    app.setApplicationName("Bravely Emerald Builder")
    win95.apply(app)
    win = MainWindow(args.repo, args.dry_run)
    win.show()
    if args.smoke:
        def finish():
            msg = "SMOKE OK: window %dx%d visible=%s host=%s source=%s" % (win.width(), win.height(), win.isVisible(),
                                                                         win.builder.host["kind"], win.source.mode)
            from core.source import help_file
            msg += " style=%s help=%s" % (type(app.style()).__name__, bool(help_file()))
            png = os.environ.get("BRAVELY_SMOKE_PNG")
            if png:
                win.grab().save(png)
            if sys.stdout is not None:
                print(msg, flush=True)
            else:                       # a windowed (no console) exe has no stdout: write next to the settings file
                from core.settings import app_home
                with open(os.path.join(app_home(), "smoke.txt"), "w", encoding="utf-8") as f:
                    f.write(msg + chr(10))
            win.close()
        QTimer.singleShot(1500, finish)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
