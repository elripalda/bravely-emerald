"""The frozen app must hold every module of core/ and ui/: the packaging collects them automatically, lists them in the bundle,
and refuses to produce a package whose exe cannot import them (the build that Marcos launched failed on `core.crash`)."""
import os
import subprocess
import sys
import tempfile
import unittest

BUILDER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BUILDER)


class PackagingTests(unittest.TestCase):
    def test_pyinstaller_collects_the_packages_not_a_list(self):
        src = open(os.path.join(BUILDER, "package.py"), encoding="utf-8").read()
        self.assertIn('"--collect-submodules", "core", "--collect-submodules", "ui"', src)
        self.assertNotIn("--hidden-import", src)
        self.assertEqual([ln.strip() for ln in src.splitlines()].count("verify_frozen(exe)"), 2)                # Windows and Linux packaging both verify the exe

    def test_module_list_matches_the_files(self):
        import package
        names = set(package.module_names())
        for pkg in ("core", "ui"):
            for fn in os.listdir(os.path.join(BUILDER, pkg)):
                if fn.endswith(".py") and fn != "__init__.py":
                    self.assertIn("%s.%s" % (pkg, fn[:-3]), names, fn)
        for needed in ("core.crash", "core.instance", "core.probe", "core.report", "ui.main_window"):
            self.assertIn(needed, names)

    def test_imports_check_passes_on_the_source(self):
        env = dict(os.environ, BRAVELY_BUILDER_HOME=tempfile.mkdtemp(prefix="bb-imp-"), QT_QPA_PLATFORM="offscreen")
        p = subprocess.run([sys.executable, "-I", os.path.join(BUILDER, "app.py"), "--imports"], capture_output=True, text=True, timeout=120, env=env)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("IMPORTS OK", p.stdout)
        self.assertIn("OK   core.crash", p.stdout)

    def test_import_error_at_start_gives_a_message_not_a_traceback_dialog(self):
        import app
        shown = []
        old = app.fatal_box
        app.fatal_box = lambda title, text: shown.append(text)
        try:
            home = tempfile.mkdtemp(prefix="bb-fatal-")
            os.environ["BRAVELY_BUILDER_HOME"] = home
            app.start_failed(ImportError("cannot import name 'crash' from 'core'"))
        finally:
            app.fatal_box = old
        self.assertTrue(shown and "could not start" in shown[0] and "download the package again" in shown[0])
        self.assertIn("cannot import name", open(os.path.join(home, "crash.log"), encoding="utf-8").read())


if __name__ == "__main__":
    unittest.main()
