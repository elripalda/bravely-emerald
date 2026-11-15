"""Tests of the UI-agnostic core. Run: python -m unittest discover -s tools/builder/tests -v
No ROM, network or build tools are needed (a synthetic ROM header is made in a temp folder)."""
import os
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-settings-"))

from core import Builder, explain, validate_rom, clean_path, load_settings, save_settings    # noqa: E402
from core.host import detect_host                                                           # noqa: E402
from core.repo import is_repo, guess_repo                                                   # noqa: E402
from core.tasks import Step                                                                 # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))


def make_rom(path, title=b"POKEMON EMER", code=b"BPEE", size=16 * 1024 * 1024, fix_sum=True):
    buf = bytearray(size)
    buf[0xA0:0xA0 + len(title)] = title
    buf[0xAC:0xB0] = code
    if fix_sum and size > 0xBD:
        buf[0xBD] = (0 - (sum(buf[0xA0:0xBD]) + 25)) & 255
    with open(path, "wb") as f:
        f.write(buf)


class RomTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="bb-rom-")

    def p(self, name):
        return os.path.join(self.d, name)

    def test_good(self):
        make_rom(self.p("a.gba"))
        r = validate_rom(self.p("a.gba"))
        self.assertTrue(r["ok"], r)
        self.assertEqual(r["code"], "BPEE")
        self.assertFalse(r["sha1_match"])

    def test_other_language_code(self):
        make_rom(self.p("a.gba"), code=b"BPED")
        self.assertTrue(validate_rom(self.p("a.gba"))["ok"])

    def test_truncated(self):
        make_rom(self.p("a.gba"), size=1024 * 1024)
        r = validate_rom(self.p("a.gba"))
        self.assertFalse(r["ok"])
        self.assertIn("size", r["message"])

    def test_wrong_game(self):
        make_rom(self.p("a.gba"), title=b"POKEMON RUBY", code=b"AXVE")
        self.assertIn("title", validate_rom(self.p("a.gba"))["message"])

    def test_wrong_code(self):
        make_rom(self.p("a.gba"), code=b"AXVE")
        self.assertIn("game code", validate_rom(self.p("a.gba"))["message"])

    def test_modified_header(self):
        make_rom(self.p("a.gba"), fix_sum=False)
        self.assertIn("checksum", validate_rom(self.p("a.gba"))["message"])

    def test_missing_and_relative(self):
        self.assertFalse(validate_rom(self.p("nope.gba"))["ok"])
        self.assertFalse(validate_rom("rom.gba")["ok"])

    def test_not_a_rom_text_file(self):
        with open(self.p("t.gba"), "w") as f:
            f.write("hello")
        self.assertFalse(validate_rom(self.p("t.gba"))["ok"])

    @unittest.skipUnless(os.environ.get("BRAVELY_TEST_ROM"), "set BRAVELY_TEST_ROM to a real Emerald dump")
    def test_real_rom(self):
        self.assertTrue(validate_rom(os.environ["BRAVELY_TEST_ROM"])["ok"])


class ValidationTests(unittest.TestCase):
    def test_paths(self):
        root = os.path.abspath(os.sep)
        good = os.path.join(root, "a", "b")
        self.assertEqual(clean_path(good), os.path.normpath(good))
        for bad in ("", "relative/dir", os.path.join(root, "a b"), os.path.join(root, "a;rm"), os.path.join(root, "a$(x)"),
                    os.path.join(root, "a\nb"), os.path.join(root, "a`x`"), "x" * 500, None, 5):
            with self.assertRaises(ValueError, msg=repr(bad)):
                clean_path(bad)
        self.assertTrue(clean_path(os.path.join(root, "a b"), no_space=False))


class HostTests(unittest.TestCase):
    def test_fake_hosts(self):
        for kind in ("windows", "steamos", "linux"):
            os.environ["BRAVELY_FAKE_HOST"] = kind
            try:
                self.assertEqual(detect_host()["kind"], kind)
            finally:
                del os.environ["BRAVELY_FAKE_HOST"]

    def test_real_host_known(self):
        self.assertIn(detect_host()["kind"], ("windows", "linux", "steamos", "other"))


class ErrorTableTests(unittest.TestCase):
    CASES = {
        "crun: open /x/containers/overlay/y/merged: Permission denied: OCI permission denied": "storage on the SD card",
        "Installing basic packages... Error: An error occurred": "mirror timed out",
        "No protocol specified": "X display",
        "ERROR: Python 3 with Pillow is needed": "Pillow",
        "fatal: png.h: No such file": "libpng",
        "ERROR: not a Pokemon Emerald dump (size 5 bytes, expected 16 MB)": "ROM was rejected",
        "ERROR: no internet (github.com unreachable)": "download failed",
        "ERROR: mingw-w64-i686-toolchain missing: pacman -S mingw-w64-i686-toolchain": "toolchain packages",
        "ERROR: the repository path contains a space; move it": "contains a space",
        "ERROR: less than 4000 MB free in /x": "disk space",
        "fatal: could not read Username for 'https://github.com'": "private",
        "sudo: a password is required": "password",
    }

    def test_each_known(self):
        for line, expect in self.CASES.items():
            hits = explain(["noise", line])
            self.assertTrue(any(expect.lower() in (h["title"] + h["fix"]).lower() for h in hits), (line, hits))

    def test_unknown_is_empty(self):
        self.assertEqual(explain(["all fine", "done"]), [])


class SettingsTests(unittest.TestCase):
    def test_roundtrip_and_whitelist(self):
        save_settings({"jobs": 3, "rom": "/x/y.gba", "evil": "x", "low_priority": True, "container": 5})
        s = load_settings()
        self.assertEqual(s["jobs"], 3)
        self.assertEqual(s["rom"], "/x/y.gba")
        self.assertNotIn("evil", s)
        self.assertNotIn("container", s)        # wrong type is dropped
        save_settings({"jobs": True})            # bool is not an int
        self.assertEqual(load_settings()["jobs"], 3)


class RepoTests(unittest.TestCase):
    def test_this_checkout_is_a_repo(self):
        self.assertTrue(is_repo(REPO))
        self.assertEqual(os.path.normcase(guess_repo()), os.path.normcase(REPO))

    def test_empty_dir_is_not(self):
        self.assertFalse(is_repo(tempfile.mkdtemp()))


class PlanTests(unittest.TestCase):
    """Command construction in dry-run mode, for each simulated host."""

    def _builder(self, kind):
        os.environ["BRAVELY_FAKE_HOST"] = kind
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)
        return Builder(REPO, dry_run=True)

    def test_targets_per_host(self):
        for kind, win, lin in (("windows", True, False), ("steamos", False, True), ("linux", False, True)):
            t = {x["id"]: x["available"] for x in self._builder(kind).targets()}
            self.assertEqual((t["windows"], t["linux32"]), (win, lin), kind)

    def test_cross_target_refused(self):
        with self.assertRaises(ValueError):
            self._builder("steamos").plan_build({"targets": ["windows"], "jobs": 2, "rom": ""})

    def test_one_target_only(self):
        with self.assertRaises(ValueError):
            self._builder("steamos").plan_build({"targets": ["windows", "linux32"], "jobs": 2})

    def test_linux_command(self):
        b = self._builder("steamos")
        if b.have_assets():
            self.skipTest("this checkout already has game files")
        d = tempfile.mkdtemp()
        rom = os.path.join(d, "e.gba")
        make_rom(rom)
        label, stages, steps, meta = b.plan_build({"targets": ["linux32"], "jobs": 2, "rom": rom, "dir": os.path.join(d, "out"),
                                                   "container": "ui-test", "run_test": False})
        argv = steps[0].argv
        self.assertEqual(argv[0], "bash")
        self.assertTrue(argv[1].replace("\\", "/").endswith("tools/deck/build_deck.sh"))
        for flag in ("--rom", "--dir", "--jobs", "--container", "--no-run"):
            self.assertIn(flag, argv)
        self.assertEqual(argv[argv.index("--jobs") + 1], "2")
        self.assertEqual(argv[argv.index("--container") + 1], "ui-test")
        self.assertEqual(stages[0], "prereq")

    def test_linux_rejects_bad_input(self):
        b = self._builder("linux")
        base = {"targets": ["linux32"], "jobs": 2, "rom": "", "dir": os.path.abspath(os.sep) + "x"}
        for patch in ({"jobs": 0}, {"jobs": "4"}, {"jobs": True}, {"container": "a b"}, {"container": "x;rm -rf /"}, {"dir": "rel"}):
            with self.assertRaises(ValueError, msg=str(patch)):
                b.plan_build(dict(base, **patch))

    def test_windows_steps(self):
        b = self._builder("windows")
        if b.have_assets():
            steps = b.plan_build({"targets": ["windows"], "jobs": 3})[2]
            self.assertEqual([s.stage for s in steps], ["tools", "package", "package"])
        else:
            rom = os.path.join(tempfile.mkdtemp(), "e.gba")
            make_rom(rom)
            steps = b.plan_build({"targets": ["windows"], "jobs": 3, "rom": rom})[2]
            self.assertEqual([s.stage for s in steps], ["files", "tools", "assets", "files", "tools", "package", "package"])
            self.assertIn("FETCH_NO_CONVERT=1", steps[0].argv[2])
            self.assertEqual(steps[0].env["MSYSTEM"], "MINGW64")
            self.assertIn(rom, steps[0].argv)               # the ROM path is its own argument ...
            self.assertNotIn(rom, steps[0].argv[2])         # ... never spliced into the shell text
            tol = steps[0].tolerate
            self.assertTrue(tol(["=== Copying 5 files ===", "=== Generating the port's files ==="]))
            self.assertFalse(tol(["ERROR: not a Pokemon Emerald dump"]))
        self.assertIn("OMP_NUM_THREADS", steps[-3].argv[2])
        self.assertTrue(steps[-1].func)

    def test_dry_run_task_finishes(self):
        b = self._builder("steamos")
        rom = os.path.join(tempfile.mkdtemp(), "e.gba")
        make_rom(rom)
        opts = {"targets": ["linux32"], "jobs": 2, "rom": rom, "dir": os.path.join(tempfile.mkdtemp(), "o"), "container": "t"}
        label, stages, steps, meta = b.plan_build(opts)
        t = b.start(label, stages, steps, meta)
        for _ in range(100):
            if t.state != "running":
                break
            time.sleep(0.05)
        self.assertEqual(t.state, "done")
        self.assertTrue(any("[dry run]" in e.get("text", "") for e in t.events))
        self.assertIsNone(t.logfile)                       # a dry run writes no files

    def test_install_rules(self):
        with self.assertRaises(ValueError):
            self._builder("steamos").plan_install("deps")
        with self.assertRaises(ValueError):
            self._builder("windows").plan_install("msys2", consent=False)
        self.assertEqual(len(self._builder("windows").plan_install("msys2", consent=True)), 4)
        b = self._builder("windows")
        if os.path.isfile(b.msys_bash()):
            for what in ("packages", "deps"):
                label, stages, steps, meta = b.plan_install(what, True)
                self.assertTrue(steps and "logdir" in meta)


class RunnerTests(unittest.TestCase):
    """Real subprocess handling with Python as the 'script'."""

    def _run(self, code, stage_rules=None, tolerate=None, cancel_after=None):
        b = Builder(REPO)
        step = Step("py", "compile", [sys.executable, "-u", "-c", code], stage_rules=stage_rules or [], tolerate=tolerate)
        t = b.start("test", ["compile", "link"], [step], {"logdir": tempfile.mkdtemp(), "low": True}, "test")
        if cancel_after:
            time.sleep(cancel_after)
            b.cancel()
        for _ in range(300):
            if t.state != "running":
                break
            time.sleep(0.1)
        return t

    def test_success_stage_and_progress(self):
        t = self._run("print('[ 50%] Building x.o'); print('Linking C executable pokeemerald')",
                      stage_rules=[(r"Linking (C|CXX) executable", "link")])
        self.assertEqual(t.state, "done")
        types = [(e["type"], e.get("stage") or e.get("pct")) for e in t.events]
        self.assertIn(("progress", 50), types)
        self.assertIn(("stage", "link"), types)
        self.assertTrue(os.path.getsize(t.logfile) > 0)            # log saved to a file

    def test_failure_has_hint(self):
        t = self._run("import sys; print('Error: png.h: No such file'); sys.exit(3)")
        self.assertEqual(t.state, "failed")
        self.assertTrue(t.hints)

    def test_tolerated_failure(self):
        self.assertEqual(self._run("import sys; print('x'); sys.exit(2)", tolerate=lambda lines: True).state, "done")

    def test_cancel_kills_tree(self):
        child = ("import subprocess,sys,time\n"
                 "p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'])\n"
                 "print('child',p.pid,flush=True)\ntime.sleep(60)\n")
        t = self._run(child, cancel_after=2.5)
        self.assertEqual(t.state, "cancelled")
        pid = None
        for e in t.events:
            if e["type"] == "log" and e["text"].startswith("child "):
                pid = int(e["text"].split()[1])
        self.assertIsNotNone(pid)
        time.sleep(0.5)
        if os.name == "nt":
            alive = str(pid) in subprocess.run(["tasklist", "/FI", "PID eq %d" % pid], capture_output=True, text=True).stdout
        else:
            try:
                os.kill(pid, 0)
                alive = True
            except OSError:
                alive = False
        self.assertFalse(alive, "grandchild survived the cancel")


if __name__ == "__main__":
    unittest.main()
