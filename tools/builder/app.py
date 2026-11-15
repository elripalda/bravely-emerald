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
    if "--probe-screen" in argv:                      # used by core/scale.py to learn the screen before Qt is configured
        from PySide6.QtGui import QGuiApplication
        import json
        qa = QGuiApplication([])
        sc = qa.primaryScreen()
        g, d = sc.geometry(), sc.devicePixelRatio()
        line = json.dumps([int(g.width() * d), int(g.height() * d), d])
        if sys.stdout is not None:
            print(line, flush=True)
        else:
            os.write(1, (line + "\n").encode())
        return 0
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
    from core import scale as uiscale
    scale_info = uiscale.configure()
    try:
        from PySide6.QtCore import QTimer
        from PySide6.QtWidgets import QApplication
    except ImportError:
        sys.exit("PySide6 is needed: pip install PySide6   (or use the packaged BravelyEmeraldBuilder)")
    from PySide6.QtCore import QObject, Signal, qInstallMessageHandler
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtWidgets import QMessageBox
    from core import crash
    from core.instance import InstanceLock, MESSAGE as ALREADY_RUNNING
    from ui import win95
    from ui.main_window import MainWindow, info_box, TITLE
    app = QApplication(sys.argv[:1])
    app.setApplicationName("Bravely Emerald Builder")
    win95.apply(app)

    class CrashBridge(QObject):                       # lets any thread ask the GUI thread for the message box
        crashed = Signal(str, str)

    bridge = CrashBridge()

    def show_crash(text, path):
        box = info_box(None, "error", "Something went wrong",
                       "Something went wrong and the program has to stop what it was doing.\n\nThe details were saved in:\n%s\n\n"
                       "Press Copy log, then paste it into a new issue at the project page. Nothing was lost: finished build steps are kept."
                       % (path or "(the log could not be saved)"), QMessageBox.Ok)
        copy = box.addButton("Copy log", QMessageBox.ActionRole)
        box.exec()
        if box.clickedButton() is copy:
            QGuiApplication.clipboard().setText(text)

    bridge.crashed.connect(show_crash)
    crash.install(lambda text, path: bridge.crashed.emit(text, path or ""))
    qInstallMessageHandler(crash.qt_message_handler)
    lock = InstanceLock()
    if not lock.acquire():
        info_box(None, "info", TITLE, ALREADY_RUNNING, QMessageBox.Ok).exec()
        return 0
    win = MainWindow(args.repo, args.dry_run)
    win.note(scale_info)
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
