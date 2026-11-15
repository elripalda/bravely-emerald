"""Renders every screen of the window to PNG files with QWidget.grab() (works with QT_QPA_PLATFORM=offscreen).

    python tools/builder/tests/shots.py OUTDIR [--repo DIR]

Uses a synthetic ROM header and a fake task, so nothing is built. On Windows set QT_QPA_FONTDIR=C:\\Windows\\Fonts
(the offscreen platform has no fonts of its own).
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
from ui import classic                           # noqa: E402
from ui.dialogs import HelpDialog, SettingsDialog   # noqa: E402
from ui.main_window import MainWindow            # noqa: E402

CODE = ("import time\nfor i in range(1,101,5):\n print('[%3d%%] Building C object x%d.o' % (i,i),flush=True); time.sleep(0.02)\n"
        "SLEEP\nprint('png.h: No such file or directory' if FAIL else 'done',flush=True)\nimport sys\nsys.exit(1 if FAIL else 0)\n")
STAGES = ["prereq", "install", "files", "tools", "assets", "deps", "compile", "link", "package"]


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
    classic.apply(app)
    w = MainWindow(repo)
    w.no_modal = True
    w.show()
    rom = os.path.join(tempfile.mkdtemp(), "emerald.gba")
    make_rom(rom)
    w.rom_edit.setText(rom)
    w.check_rom()
    t0 = time.time()
    while (w.source_busy or not w.builder.last_checks) and time.time() - t0 < 90:
        pump(app, 0.2)
    pump(app, 0.5)
    print("source+checks ready after %.1fs, repo=%s" % (time.time() - t0, w.builder.repo))
    w.grab().save(os.path.join(out, "01_idle.png"))

    box = w.make_confirm_box()
    box.show()
    pump(app, 0.4)
    box.grab().save(os.path.join(out, "02_confirm.png"))
    box.close()

    bad = os.path.join(tempfile.mkdtemp(), "short.gba")
    make_rom(bad, size=2 * 1024 * 1024)
    w.rom_edit.setText(bad)
    w.check_rom()
    w.grab().save(os.path.join(out, "01b_idle_bad_rom.png"))
    w.rom_edit.setText(rom)
    w.check_rom()

    for name, fail, sleep in (("03_building", False, True), ("05_failed", True, False)):
        code = CODE.replace("FAIL", str(fail)).replace("SLEEP", "time.sleep(60)" if sleep else "pass")
        step = Step("fake build", "compile", [sys.executable, "-u", "-c", code])
        w.on_plan(("Windows build", STAGES, [step], {"logdir": tempfile.mkdtemp(), "low": False}), "")
        pump(app, 3.5)
        w.grab().save(os.path.join(out, name + ".png"))
        if sleep:
            w.details_btn.setChecked(True)
            pump(app, 0.5)
            w.grab().save(os.path.join(out, "04_building_details.png"))
            w.details_btn.setChecked(False)
            w.builder.cancel()
            pump(app, 2)
    if w.last_box:
        w.last_box.show()
        pump(app, 0.4)
        w.last_box.grab().save(os.path.join(out, "05b_failed_dialog.png"))
        w.last_box.close()
    res = {"ok": True, "target": "windows", "folder": r"C:\Users\You\BravelyEmerald\game", "launch": True}
    w.show_ready(res)
    pump(app, 0.4)
    w.grab().save(os.path.join(out, "06_ready.png"))
    res = {"ok": True, "target": "linux32", "folder": "/home/deck/emerald-deck/build/dist", "launch": True}
    w.show_ready(res)
    pump(app, 0.4)
    w.grab().save(os.path.join(out, "06b_ready_linux.png"))

    dlg = SettingsDialog(w)
    dlg.show()
    pump(app, 0.5)
    dlg.grab().save(os.path.join(out, "07_settings.png"))
    dlg.close()
    h = HelpDialog(w)
    h.show()
    pump(app, 0.5)
    h.grab().save(os.path.join(out, "08_help.png"))
    h.close()
    print("shots in", out)
    w.close()


if __name__ == "__main__":
    main()
