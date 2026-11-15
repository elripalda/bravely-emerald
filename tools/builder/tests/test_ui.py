"""Window tests with the offscreen Qt platform (skipped when PySide6 is not installed). Dry run: nothing is built."""
import os
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-ui-"))

try:
    from PySide6.QtWidgets import QApplication, QMessageBox, QMenuBar
    HAVE_QT = True
except ImportError:
    HAVE_QT = False

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


@unittest.skipUnless(HAVE_QT, "PySide6 not installed")
class WindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from ui import classic
        cls.app = QApplication.instance() or QApplication([])
        classic.apply(cls.app)

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
        save_settings({"rom": ""})
        w = MainWindow(REPO, dry_run=dry)
        w.no_modal = True
        w.save_noop = True
        w.show()
        for _ in range(100):
            self.pump(0.1)
            if not w.source_busy:
                break
        return w

    def test_classic_style(self):
        from ui.main_window import MainWindow
        self.assertEqual(self.app.style().objectName().lower(), "windows")
        self.assertEqual(self.app.styleSheet(), "")             # no style sheet: the real classic style draws everything
        w = self.window()
        self.assertEqual([a.text() for a in w.menuBar().actions()], ["&File", "&Help"])
        self.assertIsNotNone(w.statusBar())
        w.close()

    def test_main_screen_has_only_the_essentials(self):
        w = self.window()
        self.assertEqual(w.stack.currentIndex(), 0)
        self.assertEqual(w.build_btn.text(), "&Build Bravely Emerald")
        self.assertFalse(w.build_btn.isEnabled())               # no ROM yet and no game files
        txt = w.notice.text()
        for must in ("internet connection", "30 to 60 minutes", "15 GB", "never copied or uploaded", "own use only", "pret/pokeemerald"):
            self.assertIn(must, txt)
        w.close()

    def test_rom_message_and_button(self):
        from tests.test_core import make_rom
        w = self.window()
        d = tempfile.mkdtemp()
        good, bad = os.path.join(d, "g.gba"), os.path.join(d, "b.gba")
        make_rom(good)
        make_rom(bad, size=1000)
        w.rom_edit.setText(bad)
        w.check_rom()
        self.assertFalse(w.rom_ok)
        self.assertIn("size", w.rom_text.text())
        w.rom_edit.setText(good)
        w.check_rom()
        self.assertEqual(w.rom_text.text(), "Pokemon Emerald (USA) found.")
        if not w.builder.have_assets():
            for _ in range(100):                                # wait for the background checks
                self.pump(0.1)
                if w.builder.last_checks is not None:
                    break
            self.assertTrue(w.build_btn.isEnabled() or w.blockers, w.problem_text.text())
        w.close()

    def test_confirm_text_and_dry_run_reaches_ready(self):
        from tests.test_core import make_rom
        w = self.window()
        box = w.make_confirm_box()
        self.assertIn("internet connection", box.text())
        self.assertEqual(box.button(QMessageBox.Ok).text(), "Continue")
        d = tempfile.mkdtemp()
        rom = os.path.join(d, "e.gba")
        make_rom(rom)
        w.rom_edit.setText(rom)
        w.check_rom()
        from core import settings as st
        st.save_settings({"dir": os.path.join(d, "out")})
        w.start_build()
        for _ in range(150):
            self.pump(0.1)
            if w.task and w.task.state != "running":
                break
        self.pump(0.4)
        self.assertEqual(w.task.state, "done")
        self.assertIn("[dry run]", w.log.toPlainText())
        self.assertEqual(w.stack.currentIndex(), 2)             # the Ready screen
        self.assertIn("Dry run", w.ready_title.text())
        self.assertFalse(w.log.isVisible())                     # details stay hidden until asked for
        w.details_btn.setChecked(True)
        self.assertTrue(w.details_btn.text().startswith("<<"))
        w.close()

    def test_failure_shows_hint_and_retry(self):
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
        self.assertIsNotNone(w.last_box)                        # the classic Retry / Cancel box
        w.close()

    def test_settings_and_help(self):
        from ui.dialogs import HelpDialog, SettingsDialog
        w = self.window()
        d = SettingsDialog(w)
        v = d.values()
        self.assertEqual(v["cpu"], "low")                       # the default works untouched
        d.close()
        h = HelpDialog(w)
        self.assertIn("Add it to Steam", h.view.toPlainText())
        h.close()
        w.close()


if __name__ == "__main__":
    unittest.main()
