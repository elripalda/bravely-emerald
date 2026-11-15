"""Regression test for issue 3: tools/windows/lib_cache.sh must not end a script silently on a first build (no CMakeCache.txt)
and must still drop a build folder that was configured for another source folder. Runs the real file under bash with
"set -eo pipefail"; skipped when there is no bash."""
import os
import shutil
import subprocess
import tempfile
import unittest
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "builder"))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
LIB = os.path.join(REPO, "tools", "windows", "lib_cache.sh")


def find_bash():
    for p in (r"C:\msys64\usr\bin\bash.exe", r"C:\Program Files\Git\bin\bash.exe", shutil.which("bash")):
        if p and os.path.isfile(p):
            return p
    return None


BASH = find_bash()


def posix(path):
    """Path in the form that bash understands (MSYS/Git bash on Windows: /c/x)."""
    path = os.path.abspath(path)
    if os.name == "nt":
        return "/" + path[0].lower() + path[2:].replace("\\", "/")
    return path


@unittest.skipUnless(BASH, "no bash available")
class DropForeignBuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="bb-cache-")
        self.root = os.path.join(self.tmp, "Source")
        self.build = os.path.join(self.root, "build-win", "out")
        os.makedirs(self.root)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_lib(self, root_arg):
        script = ('set -eo pipefail; . "$1"; drop_foreign_build "$2" "$3"; echo "after=$?"')
        return subprocess.run([BASH, "-c", script, "t", posix(LIB), posix(self.build), root_arg],
                              capture_output=True, text=True, timeout=60)

    def write_cache(self, home):
        os.makedirs(self.build)
        with open(os.path.join(self.build, "CMakeCache.txt"), "w") as f:
            f.write("# cache\nCMAKE_BUILD_TYPE:STRING=Release\nCMAKE_HOME_DIRECTORY:INTERNAL=%s\n" % home)
        with open(os.path.join(self.build, "marker"), "w") as f:
            f.write("x")

    def test_first_build_no_cache(self):
        r = self.run_lib(posix(self.root))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("after=0", r.stdout)
        self.assertFalse(os.path.exists(self.build))

    def test_cache_for_another_folder_is_removed(self):
        self.write_cache("C:/Somewhere/Else")
        r = self.run_lib(posix(self.root))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("after=0", r.stdout)
        self.assertFalse(os.path.exists(self.build))

    def test_cache_for_same_folder_is_kept(self):
        mixed = os.path.abspath(self.root).replace("\\", "/")
        for home in (posix(self.root), mixed, mixed.upper(), mixed.lower()):
            with self.subTest(home=home):
                shutil.rmtree(self.build, ignore_errors=True)
                self.write_cache(home)
                r = self.run_lib(posix(self.root))
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertTrue(os.path.exists(os.path.join(self.build, "marker")))

    def test_cache_without_home_line_is_removed(self):
        os.makedirs(self.build)
        with open(os.path.join(self.build, "CMakeCache.txt"), "w") as f:
            f.write("# empty\n")
        r = self.run_lib(posix(self.root))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertFalse(os.path.exists(self.build))


@unittest.skipUnless(BASH, "no bash available")
class LibHelpersTests(unittest.TestCase):
    """deps_state, the ERR trap, die and run_logged from tools/windows/lib_cache.sh."""

    def sh(self, body):
        script = 'set -eEo pipefail; . "$1"; ' + body
        return subprocess.run([BASH, "-c", script, "t", posix(LIB)], capture_output=True, text=True, timeout=60)

    def test_deps_state_matches_python(self):
        from core.probe import deps_state as py_state
        d = tempfile.mkdtemp(prefix="bb-deps-sh-")
        self.addCleanup(shutil.rmtree, d, True)
        lib = os.path.join(d, "build-win", "deps", "lib")
        os.makedirs(os.path.join(lib, "pkgconfig"))

        def both():
            r = self.sh('deps_state "%s"' % posix(os.path.join(d, "build-win", "deps")))
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout.strip(), py_state(d))
            return r.stdout.strip()
        self.assertEqual(both(), "missing")
        open(os.path.join(lib, "libSDL2.a"), "w").close()
        self.assertEqual(both(), "outdated")
        open(os.path.join(lib, "libSDL2_image.a"), "w").close()
        self.assertEqual(both(), "outdated")
        for f, v in (("sdl2.pc", "2.32.10"), ("SDL2_image.pc", "2.8.12")):
            with open(os.path.join(lib, "pkgconfig", f), "w", newline="\n") as fh:
                fh.write("Name: x\nVersion: %s\n" % v)
        self.assertEqual(both(), "present")

    def test_failure_is_never_silent(self):
        r = self.sh('bravely_trap demo.sh; false')
        self.assertEqual(r.returncode, 1)
        self.assertIn("demo.sh stopped at line", r.stderr)
        r = self.sh('bravely_trap demo.sh; x="$(sed -n p /no/such/file | tr a b)"; echo not-reached')
        self.assertEqual(r.returncode, 2)                      # the issue 3 pattern, now with a message
        self.assertIn("demo.sh stopped at line", r.stderr)
        self.assertNotIn("not-reached", r.stdout)

    def test_die_message(self):
        r = self.sh('bravely_trap demo.sh; die "plain message"')
        self.assertEqual(r.returncode, 1)
        self.assertIn("ERROR: plain message", r.stderr)

    def test_run_logged_keeps_code_and_tail(self):
        d = tempfile.mkdtemp(prefix="bb-log-")
        self.addCleanup(shutil.rmtree, d, True)
        log = posix(os.path.join(d, "x.log"))
        r = self.sh('run_logged "%s" bash -c "seq 1 50; exit 7" || echo "code=$?"' % log)
        self.assertIn("code=7", r.stdout)
        self.assertIn("last 30 lines", r.stdout)
        with open(os.path.join(d, "x.log")) as f:
            self.assertEqual(len(f.read().split()), 50)
        r = self.sh('run_logged "%s" echo fine; echo "code=$?"' % log)
        self.assertIn("code=0", r.stdout)


if __name__ == "__main__":
    unittest.main()
