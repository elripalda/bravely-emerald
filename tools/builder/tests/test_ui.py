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
    from PySide6.QtWidgets import QApplication
    HAVE_QT = True
except ImportError:
    HAVE_QT = False

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


@unittest.skipUnless(HAVE_QT, "PySide6 not installed")
class WindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from ui import theme
        cls.app = QApplication.instance() or QApplication([])
        theme.apply(cls.app)

    def setUp(self):
        os.environ["BRAVELY_FAKE_HOST"] = "steamos"     # the Linux command path, on any machine (needs dry run)
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            self.app.processEvents()
            time.sleep(0.02)

    def test_dry_run_build_reaches_result(self):
        from ui.main_window import MainWindow
        from tests.test_core import make_rom
        w = MainWindow(REPO, dry_run=True)
        w.show()
        self.assertEqual(w.current_target(), "linux32")                 # the simulated SteamOS host offers only Linux
        self.assertFalse(w.target_radios["windows"].isEnabled())
        d = tempfile.mkdtemp()
        rom = os.path.join(d, "e.gba")
        make_rom(rom)
        w.rom_edit.setText(rom)
        w.check_rom()
        self.assertTrue(w.rom_ok)
        w.dir_edit.setText(os.path.join(d, "out"))
        w.start_build()
        for _ in range(100):
            self.pump(0.1)
            if w.task and w.task.state != "running":
                break
        self.pump(0.3)
        self.assertEqual(w.task.state, "done")
        self.assertIn("[dry run]", w.log.toPlainText())
        self.assertEqual(w.tabs.currentIndex(), 2)                      # jumped to the result page
        self.assertIn("Dry run", w.res_title.text())
        w.save = lambda: None
        w.close()

    def test_bad_rom_is_shown_and_blocks(self):
        from ui.main_window import MainWindow
        from tests.test_core import make_rom
        w = MainWindow(REPO, dry_run=True)
        w.show()
        bad = os.path.join(tempfile.mkdtemp(), "x.gba")
        make_rom(bad, size=1000)
        w.rom_edit.setText(bad)
        w.check_rom()
        self.assertFalse(w.rom_ok)
        self.assertIn("REJECTED", w.rom_status.text())
        if not w.builder.have_assets():
            self.assertFalse(w.start_btn.isEnabled())
        w.save = lambda: None
        w.close()


if __name__ == "__main__":
    unittest.main()
