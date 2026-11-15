"""Renders every screen of the window to PNG files with QWidget.grab() (works with QT_QPA_PLATFORM=offscreen).

    python tools/builder/tests/shots.py OUTDIR [--repo DIR] [--zoom]

QT_SCALE_FACTOR=2 gives the 2x screens. --zoom also writes 4x nearest-neighbour crops of the title bar, a button, a text field,
the checkbox and the progress bar (for checking the pixel construction). Uses a synthetic ROM header and a fake task, so nothing is
built. On Windows set QT_QPA_FONTDIR=C:\\Windows\\Fonts (the offscreen platform has no system fonts; the app font is bundled).
"""
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-shots-"))

from PySide6.QtCore import QRect, Qt                # noqa: E402
from PySide6.QtWidgets import QApplication          # noqa: E402
from core.tasks import Step                          # noqa: E402
from tests.test_core import make_rom                 # noqa: E402
from ui import win95                                 # noqa: E402
from ui.dialogs import HelpDialog, SettingsDialog    # noqa: E402
from ui.main_window import MainWindow                # noqa: E402

CODE = ("import time\nfor i in range(1,101,5):\n print('[%3d%%] Building C object x%d.o' % (i,i),flush=True); time.sleep(0.02)\n"
        "SLEEP\nprint('png.h: No such file or directory' if FAIL else 'done',flush=True)\nimport sys\nsys.exit(1 if FAIL else 0)\n")
STAGES = ["prereq", "install", "files", "tools", "assets", "deps", "compile", "link", "package"]


def pump(app, secs):
    end = time.time() + secs
    while time.time() < end:
        app.processEvents()
        time.sleep(0.02)


def zoom(widget, rect, path, factor=4):
    """Crop a region (logical pixels) of the widget at the device resolution and enlarge it with no smoothing."""
    pm = widget.grab()
    dpr = pm.devicePixelRatio()
    crop = pm.copy(QRect(int(rect.x() * dpr), int(rect.y() * dpr), int(rect.width() * dpr), int(rect.height() * dpr)))
    crop.scaled(crop.width() * factor, crop.height() * factor, Qt.IgnoreAspectRatio, Qt.FastTransformation).save(path)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    repo = sys.argv[sys.argv.index("--repo") + 1] if "--repo" in sys.argv else None
    app = QApplication([])
    win95.apply(app)
    w = MainWindow(repo)
    w.no_modal = True
    w.show()
    t0 = time.time()
    while (w.source_busy or not w.builder.last_checks) and time.time() - t0 < 90:
        pump(app, 0.2)
    pump(app, 0.5)
    print("source+checks ready after %.1fs, repo=%s, font=%s, scale=%s" % (time.time() - t0, w.builder.repo, win95.load_font(),
                                                                         os.environ.get("QT_SCALE_FACTOR", "1")))
    w.rom_edit.setText("")
    w.consent.setChecked(False)
    w.check_rom()
    w.grab().save(os.path.join(out, "01_idle.png"))

    rom = os.path.join(tempfile.mkdtemp(), "emerald.gba")
    make_rom(rom)
    w.rom_edit.setText(rom)
    w.check_rom()
    w.consent.setChecked(True)
    pump(app, 0.3)
    w.grab().save(os.path.join(out, "02_ready_to_build.png"))
    if "--zoom" in sys.argv:
        zoom(w, QRect(0, 0, 200, 26), os.path.join(out, "z_title.png"))
        zoom(w, QRect(w.width() - 120, 0, 120, 26), os.path.join(out, "z_caption_buttons.png"))
        zoom(w, QRect(8, 40, 260, 70), os.path.join(out, "z_label_field.png"))
        zoom(w, QRect(w.rom_edit.mapTo(w, w.rom_edit.rect().topRight()).x() - 20, 40, 150, 90), os.path.join(out, "z_browse.png"))
        cp = w.consent.mapTo(w, w.consent.rect().topLeft())
        zoom(w, QRect(cp.x() - 4, cp.y() - 4, 90, 36), os.path.join(out, "z_checkbox.png"))
        bp = w.build_btn.mapTo(w, w.build_btn.rect().topLeft())
        zoom(w, QRect(bp.x() - 6, bp.y() - 6, 90, 50), os.path.join(out, "z_build_button.png"))

    bad = os.path.join(tempfile.mkdtemp(), "short.gba")
    make_rom(bad, size=2 * 1024 * 1024)
    w.rom_edit.setText(bad)
    w.check_rom()
    w.grab().save(os.path.join(out, "02b_bad_rom.png"))
    w.rom_edit.setText(rom)
    w.check_rom()

    box = w.make_footnote_box()
    box.show()
    pump(app, 0.4)
    box.grab().save(os.path.join(out, "02c_footnote.png"))
    box.close()

    for name, fail, sleep in (("03_building", False, True), ("05_failed", True, False)):
        code = CODE.replace("FAIL", str(fail)).replace("SLEEP", "time.sleep(60)" if sleep else "pass")
        step = Step("fake build", "compile", [sys.executable, "-u", "-c", code])
        w.on_plan(("Windows build", STAGES, [step], {"logdir": tempfile.mkdtemp(), "low": False}), "")
        pump(app, 3.5)
        w.grab().save(os.path.join(out, name + ".png"))
        if sleep:
            if "--zoom" in sys.argv:
                bp = w.bar.mapTo(w, w.bar.rect().topLeft())
                zoom(w, QRect(bp.x() - 4, bp.y() - 4, 150, w.bar.height() + 8), os.path.join(out, "z_progress.png"))
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
    w.show_ready({"ok": True, "target": "windows", "folder": r"C:\Users\You\Documents\Bravely Emerald", "launch": True})
    pump(app, 0.4)
    w.grab().save(os.path.join(out, "06_done.png"))

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
