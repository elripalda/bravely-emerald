"""Renders every page of the window to PNG files with QWidget.grab() (works with QT_QPA_PLATFORM=offscreen).

    python tools/builder/tests/shots.py OUTDIR [--repo DIR]

Uses a synthetic ROM header and a fake task, so nothing is built.
"""
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-shots-"))

from PySide6.QtWidgets import QApplication      # noqa: E402
from core.tasks import Step                      # noqa: E402
from tests.test_core import make_rom             # noqa: E402
from ui import theme                             # noqa: E402
from ui.main_window import MainWindow            # noqa: E402


def pump(app, secs):
    end = time.time() + secs
    while time.time() < end:
        app.processEvents()
        time.sleep(0.02)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    repo = sys.argv[sys.argv.index("--repo") + 1] if "--repo" in sys.argv else None
    app = QApplication([])
    theme.apply(app)
    w = MainWindow(repo)
    w.resize(1000, 900)
    w.show()
    d = tempfile.mkdtemp()
    rom = os.path.join(d, "emerald.gba")
    make_rom(rom)
    w.rom_edit.setText(rom)
    w.check_rom()
    t0 = time.time()
    while (not w.refresh_btn.isEnabled() or not w.checks) and time.time() - t0 < 90:   # the checks run in a thread
        pump(app, 0.2)
    print('checks took %.1fs, %d rows' % (time.time() - t0, len(w.checks)))
    w.tabs.setCurrentIndex(0)
    w.grab().save(os.path.join(out, "01_setup.png"))
    bad = os.path.join(d, "short.gba")
    make_rom(bad, size=2 * 1024 * 1024)
    w.rom_edit.setText(bad)
    w.check_rom()
    sb = w.tabs.widget(0).verticalScrollBar()
    sb.setValue(sb.maximum())
    pump(app, 0.3)
    w.grab().save(os.path.join(out, "01b_setup_bad_rom.png"))

    # a fake build: prints cmake-like progress lines, then fails with a known error on request
    code = ("import time\nfor i in range(1,101,5):\n print('[%3d%%] Building C object x%d.o' % (i,i),flush=True); time.sleep(0.02)\n"
            "print('Linking C executable pokeemerald',flush=True)\n"
            "print('png.h: No such file or directory' if FAIL else 'done',flush=True)\nimport sys\nsys.exit(1 if FAIL else 0)\n")
    for name, fail in (("02_build_running", False), ("03_build_failed", True)):
        step = Step("fake build", "compile", [sys.executable, "-u", "-c", code.replace('FAIL', str(fail))],
                    stage_rules=[(r"Linking C executable", "link")])
        if name.endswith("running"):
            step.argv[3] = code.replace('FAIL', 'False').replace("print('Linking", "time.sleep(60); print('Linking")
        w.begin_task("Windows build", ["prereq", "files", "tools", "assets", "deps", "compile", "link", "package"],
                     [step], {"logdir": tempfile.mkdtemp(), "low": False}, "build")
        pump(app, 3 if not fail else 4)
        w.tabs.setCurrentIndex(1)
        w.grab().save(os.path.join(out, name + ".png"))
        if not fail:
            w.cancel()
            pump(app, 2)
    res = {"ok": True, "target": "windows", "folder": r"F:\bravely-emerald\dist-win", "zip": r"F:\bravely-emerald\pokeemerald-win.zip",
           "savedir": r"F:\bravely-emerald\dist-win\userdata\saves", "log": r"F:\bravely-emerald\build-win\builder-logs\build.log", "launch": True}
    w.show_result(res)
    pump(app, 0.4)
    w.grab().save(os.path.join(out, "04_result_windows.png"))
    res = {"ok": True, "target": "linux32", "folder": "/home/deck/emerald-deck/build/dist", "launcher": "/home/deck/emerald-deck/emerald-launch.sh",
           "savedir": "/home/deck/emerald-deck/userdata/saves", "log": "/home/deck/emerald-deck/builder-logs/build.log", "launch": True}
    w.show_result(res)
    pump(app, 0.4)
    w.grab().save(os.path.join(out, "05_result_linux.png"))
    print("shots in", out)
    w.save = lambda: None
    w.close()


if __name__ == "__main__":
    main()
