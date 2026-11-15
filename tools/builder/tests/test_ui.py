"""Window tests with the offscreen Qt platform (skipped when PySide6 is not installed). Dry run: nothing is built."""
import os
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-ui-"))

try:
    from PySide6.QtCore import QRect, Qt
    from PySide6.QtGui import QImage, QPainter
    from PySide6.QtWidgets import QApplication, QMessageBox
    HAVE_QT = True
except ImportError:
    HAVE_QT = False

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


def px(img, x, y):
    return img.pixelColor(x, y).name().upper()


@unittest.skipUnless(HAVE_QT, "PySide6 not installed")
class Win95PaintTests(unittest.TestCase):
    """The pixel construction of the edges, checked on a bare image (colour table in docs/BUILDER.md, Look)."""

    def paint(self, fn, w=20, h=16):
        from ui import win95
        img = QImage(w, h, QImage.Format_RGB32)
        img.fill(win95.FACE)
        p = QPainter(img)
        fn(p, QRect(0, 0, w, h))
        p.end()
        return img

    def test_raised_edge(self):
        from ui import win95
        img = self.paint(win95.raised)
        self.assertEqual(px(img, 0, 5), "#FFFFFF")          # outer top-left: highlight
        self.assertEqual(px(img, 5, 0), "#FFFFFF")
        self.assertEqual(px(img, 19, 5), "#000000")         # outer bottom-right: dark shadow
        self.assertEqual(px(img, 5, 15), "#000000")
        self.assertEqual(px(img, 18, 5), "#808080")         # inner bottom-right: shadow
        self.assertEqual(px(img, 5, 14), "#808080")
        self.assertEqual(px(img, 1, 5), "#C0C0C0")          # inner top-left: light (the same as the face in Windows 95)
        self.assertEqual(px(img, 10, 8), "#C0C0C0")         # face

    def test_sunken_edge(self):
        from ui import win95
        img = self.paint(win95.sunken)
        self.assertEqual(px(img, 0, 5), "#808080")          # outer top-left: shadow
        self.assertEqual(px(img, 19, 5), "#FFFFFF")         # outer bottom-right: highlight
        self.assertEqual(px(img, 1, 5), "#000000")          # inner top-left: dark shadow
        self.assertEqual(px(img, 18, 5), "#C0C0C0")         # inner bottom-right: light

    def test_etched_line_and_window_frame(self):
        from ui import win95
        img = self.paint(lambda p, r: win95.etched_h(p, 0, 3, 20))
        self.assertEqual((px(img, 5, 3), px(img, 5, 4)), ("#808080", "#FFFFFF"))
        img = self.paint(win95.window_frame)
        self.assertEqual((px(img, 0, 5), px(img, 1, 5)), ("#C0C0C0", "#FFFFFF"))      # top-left: light, then highlight
        self.assertEqual((px(img, 19, 5), px(img, 18, 5)), ("#000000", "#808080"))    # bottom-right: dark shadow, then shadow

    def test_colour_table(self):
        from ui import win95
        self.assertEqual([c.name().upper() for c in (win95.FACE, win95.HILIGHT, win95.LIGHT, win95.SHADOW, win95.DKSHADOW, win95.NAVY, win95.TOOLTIP)],
                         ["#C0C0C0", "#FFFFFF", "#C0C0C0", "#808080", "#000000", "#000080", "#FFFFE1"])

    def test_font_is_bundled_and_licensed(self):
        from ui import win95
        self.assertTrue(win95.font_path() and os.path.isfile(win95.font_path()))
        lic = os.path.join(REPO, "docs", "licenses", "W95FA-OFL.txt")
        with open(lic, encoding="utf-8") as f:
            self.assertIn("SIL OPEN FONT LICENSE Version 1.1", f.read())


@unittest.skipUnless(HAVE_QT, "PySide6 not installed")
class WindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from ui import win95
        cls.app = QApplication.instance() or QApplication([])
        win95.apply(cls.app)

    def setUp(self):
        os.environ["BRAVELY_FAKE_HOST"] = "steamos"     # the Linux command path, on any machine (needs dry run)
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            self.app.processEvents()
            time.sleep(0.02)

    def window(self, dry=True):
        from ui.main_window import MainWindow
        from core.settings import save_settings
        save_settings({"rom": "", "dir": ""})
        w = MainWindow(REPO, dry_run=dry)
        w.no_modal = True
        w.show()
        for _ in range(100):
            self.pump(0.1)
            if not w.source_busy:
                break
        return w

    def test_style_and_frame(self):
        from ui import win95
        from ui.main_window import TITLE, WIDTH, HEIGHT
        self.assertIsInstance(self.app.style(), win95.Win95Style)
        self.assertEqual(self.app.styleSheet(), "")             # no style sheet: the style class draws every edge
        self.assertEqual(win95.load_font(), "W95FA")
        w = self.window()
        self.assertEqual(TITLE, "Pokemon Bravely Emerald Builder")
        self.assertEqual((w.width(), w.height()), (WIDTH, HEIGHT))      # fixed size
        self.assertEqual(w.minimumSize(), w.maximumSize())
        self.assertTrue(w.windowFlags() & Qt.FramelessWindowHint)
        self.assertEqual(w.titlebar.help_btn.toolTip(), "Help")
        self.assertEqual(w.titlebar.close_btn.toolTip(), "Close")
        w.close()

    def test_form_has_only_the_mockup_elements(self):
        from ui.main_window import CONSENT
        w = self.window()
        self.assertEqual(CONSENT, "Yeah, download the nerdy stuff needed to build (requires internet and it is safe). *")
        self.assertEqual(w.build_btn.text(), "Build")
        self.assertTrue(w.build_btn.isDefault())
        self.assertFalse(w.build_btn.isEnabled())               # no ROM, no consent
        self.assertEqual(w.dir_edit.text(), w.builder.default_dir())
        w.close()

    def test_build_needs_rom_folder_and_consent(self):
        from tests.test_core import make_rom
        w = self.window()
        d = tempfile.mkdtemp()
        good = os.path.join(d, "g.gba")
        make_rom(good)
        w.rom_edit.setText(good)
        w.check_rom()
        self.assertEqual(w.rom_text.text(), "Pokemon Emerald found (USA).")
        for _ in range(100):
            self.pump(0.1)
            if w.builder.last_checks is not None:
                break
        if not w.blockers:
            self.assertFalse(w.build_btn.isEnabled())           # the consent box is not ticked
            w.consent.setChecked(True)
            self.assertTrue(w.build_btn.isEnabled())
            w.dir_edit.setText("relative\\folder")
            w.check_dir()
            self.assertFalse(w.build_btn.isEnabled())           # no valid folder
            self.assertTrue(w.dir_text.text())
            w.dir_edit.setText(os.path.join(d, "out"))
            w.check_dir()
            self.assertTrue(w.build_btn.isEnabled())
            w.consent.setChecked(False)
            self.assertFalse(w.build_btn.isEnabled())
        w.close()

    def test_bad_rom_message(self):
        from tests.test_core import make_rom
        w = self.window()
        bad = os.path.join(tempfile.mkdtemp(), "b.gba")
        make_rom(bad, size=1000)
        w.rom_edit.setText(bad)
        w.check_rom()
        self.assertFalse(w.rom_ok)
        self.assertIn("size", w.rom_text.text())
        w.close()

    def test_footnote_and_dry_run_reaches_done(self):
        from tests.test_core import make_rom
        w = self.window()
        self.assertIn("never copied or uploaded", w.make_footnote_box().text())
        self.assertIn("pret/pokeemerald", w.make_footnote_box().text())
        d = tempfile.mkdtemp()
        rom = os.path.join(d, "e.gba")
        make_rom(rom)
        w.rom_edit.setText(rom)
        w.check_rom()
        w.dir_edit.setText(os.path.join(d, "out"))
        w.start_build()
        for _ in range(150):
            self.pump(0.1)
            if w.task and w.task.state != "running":
                break
        self.pump(0.4)
        self.assertEqual(w.task.state, "done")
        self.assertIn("[dry run]", w.log.toPlainText())
        self.assertEqual(w.stack.currentIndex(), 2)             # the done screen
        self.assertIn("Dry run", w.ready_title.text())
        self.assertFalse(w.log.isVisible())                     # details stay hidden until asked for
        w.details_btn.setChecked(True)
        self.assertTrue(w.details_btn.text().startswith("<<"))
        w.close()

    def test_failure_shows_hint_and_retry_box(self):
        from core.tasks import Step
        w = self.window(dry=False)
        code = "import sys; print('Error: png.h: No such file'); sys.exit(2)"
        w.on_plan(("Test", ["prereq", "compile"], [Step("x", "compile", [sys.executable, "-c", code])], {"logdir": tempfile.mkdtemp(), "low": False}), "")
        for _ in range(100):
            self.pump(0.1)
            if w.task.state != "running":
                break
        self.pump(0.4)
        self.assertEqual(w.task.state, "failed")
        self.assertIn("libpng", w.fail_text.text())
        self.assertTrue(w.retry_btn.isVisibleTo(w))
        self.assertTrue(w.last_box.standardButtons() & QMessageBox.Retry)      # the classic Retry / Cancel box
        w.close()

    def test_drag_fallbacks(self):
        w = self.window()
        ev = mock.Mock()
        ev.globalPosition.return_value.toPoint.return_value = w.frameGeometry().topLeft()
        handle = mock.Mock()
        handle.startSystemMove.return_value = True
        with mock.patch.object(w, "windowHandle", return_value=handle):
            w.begin_drag(ev)
            self.assertEqual(w.drag_mode, "system")
            handle.startSystemMove.return_value = False
            w.begin_drag(ev)
            self.assertEqual(w.drag_mode, "manual")             # falls back to moving by mouse, and says so
        self.assertTrue(any("startSystemMove" in l for l in w.log_buf))
        with mock.patch("ui.main_window.QGuiApplication.platformName", return_value="wayland"), mock.patch.object(w, "use_native_frame") as native:
            w.begin_drag(ev)
            native.assert_called_once()                          # no way to move a window: native frame
        w.close()

    def test_settings_and_help(self):
        from ui.dialogs import HelpDialog, SettingsDialog
        w = self.window()
        d = SettingsDialog(w)
        self.assertEqual(d.values()["cpu"], "low")              # the default works untouched
        d.close()
        h = HelpDialog(w)
        self.assertIn("Add it to Steam", h.view.toPlainText())
        h.close()
        w.close()


if __name__ == "__main__":
    unittest.main()
