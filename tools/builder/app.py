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
    try:
        from PySide6.QtCore import QTimer
        from PySide6.QtWidgets import QApplication
    except ImportError:
        sys.exit("PySide6 is needed: pip install PySide6   (or use the packaged BravelyEmeraldBuilder)")
    from ui import classic
    from ui.main_window import MainWindow
    app = QApplication(sys.argv[:1])
    app.setApplicationName("Bravely Emerald Builder")
    classic.apply(app)
    win = MainWindow(args.repo, args.dry_run)
    win.show()
    if args.smoke:
        def finish():
            msg = "SMOKE OK: window %dx%d visible=%s host=%s source=%s" % (win.width(), win.height(), win.isVisible(),
                                                                         win.builder.host["kind"], win.source.mode)
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
