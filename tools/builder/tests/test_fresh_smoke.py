"""How to check a fresh install without installing anything (docs/BUILDER.md). No network, no installs, under two minutes:

  python -I -m unittest discover -s tools/builder/tests -t . -p test_fresh_smoke.py

1. stub pacman: the real core code (pacman -Suy loop, retries, lock file, keyring, errors) runs against a fake pacman that
   replays scripted answers: first sync asks for a restart, mirror timeouts, lock file, keyring error.
2. the real shell scripts run with stand-in tools (BRAVELY_STUBS) from an empty folder, a folder with spaces, one with
   an accent, and a Windows style user path: right step order, and never a silent exit.
3. the planner and the command line (also the packaged exe when BRAVELY_PACKAGED_EXE or dist-builder holds one) against the
   real host and against an empty MSYS2 folder, with --plan and --build --dry-run.
"""
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
BUILDER = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(BUILDER))
sys.path.insert(0, BUILDER)
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-fresh-"))

from core import Builder                                   # noqa: E402
from core.constants import MSYS_PACKAGES                   # noqa: E402
from core.tasks import Step                                # noqa: E402
from cli import write_synthetic_rom                        # noqa: E402


def find_bash():
    for p in (r"C:\msys64\usr\bin\bash.exe", r"C:\Program Files\Git\bin\bash.exe", shutil.which("bash")):
        if p and os.path.isfile(p):
            return p
    return None


BASH = find_bash()


def posix(path):
    path = os.path.abspath(path)
    if os.name == "nt":
        return "/" + path[0].lower() + path[2:].replace("\\", "/")
    return path


_real_sleep = time.sleep


def wait(task, secs=60):
    for _ in range(int(secs * 10)):
        if task.state != "running":
            return
        _real_sleep(0.1)


def log_text(task):
    return "\n".join(e["text"] for e in task.events if e["type"] == "log")


# ------------------------------------------------------------------------------------------------ 1. stub pacman
STUB_PACMAN = r'''
import json, os, sys
d, script = sys.argv[1], sys.argv[2]
kind = "keyring" if "pacman-key" in script else "sync" if "-Suy" in script else "install" if "--needed" in script else "other"
n_file = os.path.join(d, kind + ".n")
n = int(open(n_file).read()) if os.path.exists(n_file) else 0
open(n_file, "w").write(str(n + 1))
with open(os.path.join(d, "calls.log"), "a") as f:
    f.write("%s %s\n" % (kind, " ".join(sys.argv[3:])))
plan_file = os.path.join(d, kind + ".json")
answers = json.load(open(plan_file)) if os.path.exists(plan_file) else []
a = answers[min(n, len(answers) - 1)] if answers else {"out": "there is nothing to do", "rc": 0}
print(a["out"])
sys.exit(a["rc"])
'''


class StubPacmanTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="bb-stubpac-")
        self.addCleanup(shutil.rmtree, self.d, True)
        with open(os.path.join(self.d, "stub.py"), "w") as f:
            f.write(STUB_PACMAN)
        self.root = os.path.join(self.d, "msys64")
        os.makedirs(os.path.join(self.root, "var", "lib", "pacman"))
        os.environ["BRAVELY_FAKE_HOST"] = "windows"
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)
        self.b = Builder(REPO)
        self.b.retry_wait = 0
        self.b.msys_root = self.root
        self.b.last_checks = [{"blocking": False}]
        self.b.probe = mock.Mock(msys=lambda: (self.root, "stub"), packages_missing=lambda: (["make", "cmake"], None), deps=lambda: "present",
                                 tools=lambda: True, assets=lambda: True, build_dir=lambda: "present")
        stub = os.path.join(self.d, "stub.py")

        def msys_cmd(script, args=(), msystem="MSYS"):
            return [sys.executable, stub, self.d, script] + [str(a) for a in args], {}
        self.b.msys_cmd = msys_cmd

    def answers(self, kind, items):
        with open(os.path.join(self.d, kind + ".json"), "w") as f:
            json.dump(items, f)

    def calls(self):
        p = os.path.join(self.d, "calls.log")
        return [ln.split()[0] for ln in open(p).read().splitlines()] if os.path.exists(p) else []

    def run_install(self):
        steps = self.b.plan_build({"targets": ["windows"], "jobs": 2, "dir": os.path.join(self.d, "out")})[2]
        install = [s for s in steps if s.stage == "install"]
        self.assertEqual(len(install), 2, [s.name for s in steps])
        t = self.b.start("stub install", ["install"], install, {"logdir": self.d, "low": False}, "test")
        wait(t)
        return t

    def test_restart_needed_first_sync_runs_a_second_round(self):
        self.answers("sync", [{"out": ":: Starting core system upgrade...\nterminate MSYS2 without returning to the shell and check for updates again", "rc": 1},
                              {"out": ":: Starting full system upgrade...\n there is nothing to do", "rc": 0}])
        t = self.run_install()
        self.assertEqual(t.state, "done", log_text(t))
        self.assertEqual(self.calls(), ["keyring", "sync", "sync", "install"])
        self.assertIn("system is up to date", log_text(t))

    def test_up_to_date_system_syncs_once(self):
        self.answers("sync", [{"out": ":: Starting full system upgrade...\n there is nothing to do", "rc": 0}])
        t = self.run_install()
        self.assertEqual(t.state, "done", log_text(t))
        self.assertEqual(self.calls(), ["keyring", "sync", "install"])

    def test_install_installs_only_the_given_packages_with_needed(self):
        t = self.run_install()
        self.assertEqual(t.state, "done", log_text(t))
        line = [ln for ln in open(os.path.join(self.d, "calls.log")).read().splitlines() if ln.startswith("install")][0]
        self.assertEqual(line.split()[1:], MSYS_PACKAGES)

    def test_mirror_timeout_is_retried_then_works(self):
        err = "error: failed retrieving file 'x.pkg.tar.zst' from mirror : Connection timed out after 10000 milliseconds"
        self.answers("install", [{"out": err, "rc": 1}, {"out": err, "rc": 1}, {"out": "installed", "rc": 0}])
        t = self.run_install()
        self.assertEqual(t.state, "done", log_text(t))
        self.assertEqual(self.calls().count("install"), 3)
        self.assertIn("Trying again, attempt 2 of 4", log_text(t))

    def test_mirror_down_for_good_fails_with_footer_and_hint(self):
        err = "error: failed retrieving file 'x.pkg.tar.zst' from mirror : Connection timed out after 10000 milliseconds"
        self.answers("install", [{"out": err, "rc": 1}])
        t = self.run_install()
        self.assertEqual(t.state, "failed")
        self.assertEqual(self.calls().count("install"), 4)                  # 1 try + 3 retries
        text = log_text(t)
        self.assertIn("FAILED: Install the missing build packages", text)
        self.assertIn("Probable cause: A download failed", text)

    def test_stale_lock_file_is_removed(self):
        lock = os.path.join(self.root, "var", "lib", "pacman", "db.lck")
        open(lock, "w").close()
        with mock.patch.object(Builder, "_pacman_running", staticmethod(lambda: False)):
            t = self.run_install()
        self.assertEqual(t.state, "done", log_text(t))
        self.assertFalse(os.path.exists(lock))
        self.assertIn("old lock file was removed", log_text(t))

    def test_running_pacman_blocks_with_a_plain_message(self):
        open(os.path.join(self.root, "var", "lib", "pacman", "db.lck"), "w").close()
        with mock.patch.object(Builder, "_pacman_running", staticmethod(lambda: True)), mock.patch("core.builder.time.sleep", lambda s: None):
            t = self.run_install()
        self.assertEqual(t.state, "failed")
        self.assertIn("Another pacman keeps running", log_text(t))
        self.assertEqual(self.calls(), [])

    def test_keyring_error_stops_with_a_plain_message(self):
        self.answers("keyring", [{"out": "gpg: error reading key", "rc": 2}])
        t = self.run_install()
        self.assertEqual(t.state, "failed")
        self.assertIn("could not set up its package keyring", log_text(t))
        self.assertNotIn("sync", self.calls())

    def test_resume_after_the_install_asks_pacman_for_nothing(self):
        self.b.probe.packages_missing = lambda: ([], None)
        steps = self.b.plan_build({"targets": ["windows"], "jobs": 2, "dir": os.path.join(self.d, "out")})[2]
        self.assertEqual([s for s in steps if s.stage == "install"], [])


# ------------------------------------------------------------------------------------------------ 2. real scripts, stub tools
STUBS = {
    "i686-w64-mingw32-gcc": 'echo "gcc $*" >> "$STUB_LOG"\n[ "$1" = "-dumpmachine" ] && echo i686-w64-mingw32\nexit 0\n',
    "cmake": 'echo "cmake $*" >> "$STUB_LOG"\n[ "$STUB_FAIL" = "cmake" ] && { echo "CMake Error: stub failure"; exit 3; }\n'
             'echo "-- Configuring done"\nexit 0\n',
    "make": 'echo "make $*" >> "$STUB_LOG"\nexit 0\n',
    "python": 'echo "python $*" >> "$STUB_LOG"\nif [ "$1" = "-m" ] && [ "$2" = "zipfile" ]; then : > "$4"; fi\nexit 0\n',
    "curl": 'echo "curl $*" >> "$STUB_LOG"\nwhile [ $# -gt 0 ]; do [ "$1" = "-o" ] && out="$2"; shift; done\n: > "$out"\nexit 0\n',
    "tar": 'echo "tar $*" >> "$STUB_LOG"\ncase "$1" in tzf) exit 0;; xf) mkdir -p "${2%.tar.gz}";; esac\nexit 0\n',
    "strip": 'echo "strip $*" >> "$STUB_LOG"\nexit 0\n',
    "objdump": 'echo "objdump $*" >> "$STUB_LOG"\necho "DLL Name: KERNEL32.dll"\nexit 0\n',
}

WORK_NAMES = {
    "plain": "work",
    "with space": "my work folder",
    "accent": "caf\u00e9-\u00f1",
    "windows user path": os.path.join("Users", "Player", "AppData", "Local", "BravelyEmerald", "source"),
}


def make_stubs(d):
    os.makedirs(d, exist_ok=True)
    for name, body in STUBS.items():
        p = os.path.join(d, name)
        with open(p, "w", newline="\n") as f:
            f.write("#!/bin/bash\n" + body)
        os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)


def make_workspace(root, populated):
    """Scripts of this repo in `root` (the way the bundled source is laid out), plus fake game files when `populated`."""
    os.makedirs(os.path.join(root, "tools", "windows"))
    for fn in os.listdir(os.path.join(REPO, "tools", "windows")):
        if fn.endswith(".sh") or fn == "README_WINDOWS.txt" or fn == "run.bat":
            shutil.copy(os.path.join(REPO, "tools", "windows", fn), os.path.join(root, "tools", "windows", fn))
    if not populated:
        return

    def touch(rel, exe=False):
        p = os.path.join(root, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").close()
        if exe:
            os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)
    for rel in (".assets_ok", "graphics/pokemon/bulbasaur/front.png", "assets/ui2/ui_atlas.bin", "assets/ui/intro_margins.bin",
                "build-win/deps/lib/libSDL2.a", "build-win/deps/lib/libSDL2_image.a"):
        touch(rel)
    touch("tools/preproc/preproc.exe", True)
    touch("tools/gbagfx/gbagfx.exe", True)
    for fn, v in (("sdl2.pc", "2.32.10"), ("SDL2_image.pc", "2.8.12")):
        p = os.path.join(root, "build-win", "deps", "lib", "pkgconfig", fn)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", newline="\n") as f:
            f.write("Name: x\nVersion: %s\n" % v)


@unittest.skipUnless(BASH, "no bash available")
class ScriptFirstRunTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="bb-scripts-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.stubs = os.path.join(self.tmp, "stubs")
        make_stubs(self.stubs)
        self.log = os.path.join(self.tmp, "calls.log")

    def run_script(self, root, script, args=(), fail=None, stubs_first_on_path=False):
        env = dict(os.environ, BRAVELY_STUBS=posix(self.stubs), STUB_LOG=posix(self.log), STUB_FAIL=fail or "")
        if stubs_first_on_path:
            env["PATH"] = self.stubs + os.pathsep + env["PATH"]
        env["MSYSTEM"] = "MSYS"
        p = subprocess.run([BASH, posix(os.path.join(root, "tools", "windows", script))] + list(args), capture_output=True,
                           text=True, encoding="utf-8", errors="replace", timeout=120, env=env, cwd=self.tmp)
        return p

    def calls(self):
        return open(self.log, encoding="utf-8").read().splitlines() if os.path.exists(self.log) else []

    def in_order(self, text, needles):
        pos = -1
        for n in needles:
            i = text.find(n, pos + 1)
            self.assertGreater(i, pos, "%r not found after position %d in:\n%s" % (n, pos, text))
            pos = i

    def test_empty_folder_stops_with_a_plain_message(self):
        for label, rel in WORK_NAMES.items():
            with self.subTest(label):
                root = os.path.join(self.tmp, label.replace(" ", "_"), rel)
                make_workspace(root, populated=False)
                p = self.run_script(root, "build.sh")
                self.assertEqual(p.returncode, 1, p.stdout + p.stderr)
                self.assertIn("ERROR: the game files are missing", p.stderr)
                self.assertIn("(build.sh, line", p.stderr)

    def test_first_build_reaches_every_step_in_order(self):
        for label, rel in WORK_NAMES.items():
            with self.subTest(label):
                if os.path.exists(self.log):
                    os.remove(self.log)
                root = os.path.join(self.tmp, "p_" + label.replace(" ", "_"), rel)
                make_workspace(root, populated=True)
                p = self.run_script(root, "build.sh")
                self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
                self.in_order(p.stdout, ["already done: game files", "already done: host tools", "=== Converting the game files",
                                         "already done: intro margins", "already done: SDL2 2.32.10 and SDL2_image 2.8.12",
                                         "=== Configuring CMake in build-win/out", "no CMake build folder yet", "-- Configuring done",
                                         "=== Building pokeemerald.exe", "=== Built:"])
                calls = self.calls()
                self.assertTrue(any(c.startswith("cmake -S . -B build-win/out") for c in calls), calls)
                self.assertTrue(any(c.startswith("cmake --build build-win/out") for c in calls), calls)

    def test_second_run_reuses_the_build_folder(self):
        root = os.path.join(self.tmp, "second")
        make_workspace(root, populated=True)
        os.makedirs(os.path.join(root, "build-win", "out"))
        with open(os.path.join(root, "build-win", "out", "CMakeCache.txt"), "w") as f:
            f.write("CMAKE_HOME_DIRECTORY:INTERNAL=%s\n" % os.path.abspath(root).replace("\\", "/"))
        p = self.run_script(root, "build.sh")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("already configured for this folder: reusing it", p.stdout)

    def test_missing_or_old_sdl_is_rebuilt_a_good_one_is_not(self):
        root = os.path.join(self.tmp, "sdl")
        make_workspace(root, populated=True)
        pc = os.path.join(root, "build-win", "deps", "lib", "pkgconfig", "sdl2.pc")
        with open(pc, "w", newline="\n") as f:
            f.write("Version: 2.30.0\n")                                  # an older SDL2 is not good enough
        p = self.run_script(root, "build.sh")
        self.assertIn("=== SDL2 and SDL2_image (static, 32-bit): outdated", p.stdout)
        self.assertIn("the SDL2 build in", p.stdout)                           # build_deps.sh started it again ...
        self.assertTrue(any(c.startswith("curl") for c in self.calls()))        # ... and only the stand-in curl ran: no network
        self.assertEqual(p.returncode, 1)
        self.assertIn("the SDL2 build finished but", p.stderr)

    def test_failure_in_a_tool_is_never_silent(self):
        root = os.path.join(self.tmp, "fail")
        make_workspace(root, populated=True)
        p = self.run_script(root, "build.sh", fail="cmake")
        self.assertEqual(p.returncode, 3, p.stdout + p.stderr)
        self.assertIn("build.sh stopped at line", p.stderr)
        self.assertIn("last 30 lines", p.stdout)
        self.assertIn("CMake Error: stub failure", p.stdout)

    def test_build_deps_first_run_downloads_then_builds_in_order(self):
        root = os.path.join(self.tmp, "deps")
        make_workspace(root, populated=False)
        p = self.run_script(root, "build_deps.sh", stubs_first_on_path=True)
        calls = [c.split()[0] + " " + c.split()[1] for c in self.calls()]
        self.assertEqual(p.returncode, 1, p.stdout + p.stderr)                  # the stub cmake installs nothing: it must say so
        self.assertIn("the SDL2 build finished but", p.stderr)
        self.assertEqual([c for c in calls if c.startswith("curl")], ["curl -fsSL", "curl -fsSL"])
        self.in_order(" | ".join(self.calls()), ["curl", "SDL2-2.32.10.tar.gz.part", "SDL2_image-2.8.12.tar.gz.part", "cmake -S SDL2-2.32.10",
                                                 "cmake --build", "cmake --install", "cmake -S SDL2_image-2.8.12"])

    def test_package_first_run(self):
        root = os.path.join(self.tmp, "pack")
        make_workspace(root, populated=False)
        dist = os.path.join(root, "build-win", "out", "dist")
        os.makedirs(os.path.join(dist, "app0"))
        os.makedirs(os.path.join(dist, "userdata"))
        for rel in ("pokeemerald.exe", "run.bat", "userdata/settings.txt"):
            open(os.path.join(dist, *rel.split("/")), "w").close()
        p = self.run_script(root, "package.sh")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertTrue(os.path.isfile(os.path.join(root, "pokeemerald-win.zip")))
        self.in_order(p.stdout, ["DLL imports of pokeemerald.exe:", "KERNEL32.dll"])

    def test_package_without_a_build_is_a_plain_error(self):
        root = os.path.join(self.tmp, "nobuild")
        make_workspace(root, populated=False)
        p = self.run_script(root, "package.sh")
        self.assertEqual(p.returncode, 1)
        self.assertIn("build first", p.stderr)

    def test_every_script_has_valid_syntax(self):
        scripts = [os.path.join(REPO, "tools", "windows", f) for f in os.listdir(os.path.join(REPO, "tools", "windows")) if f.endswith(".sh")]
        scripts += [os.path.join(REPO, "tools", "deck", "build_deck.sh"), os.path.join(REPO, "fetch_assets.sh")]
        for s in scripts:
            with self.subTest(os.path.basename(s)):
                p = subprocess.run([BASH, "-n", posix(s)], capture_output=True, text=True, timeout=60)
                self.assertEqual(p.returncode, 0, p.stderr)


# ------------------------------------------------------------------------------------------------ 3. planner and command line
def packaged_exe():
    env = os.environ.get("BRAVELY_PACKAGED_EXE")
    if env and os.path.isfile(env):
        return env
    name = "BravelyEmeraldBuilder.exe" if os.name == "nt" else "BravelyEmeraldBuilder"
    p = os.path.join(BUILDER, "dist-builder", "BravelyEmeraldBuilder", name)
    return p if os.path.isfile(p) else None


class CommandLineTests(unittest.TestCase):
    """The same checks on the source (always) and on the packaged app (when there is one)."""

    def runner(self):
        yield "source", [sys.executable, "-I", os.path.join(BUILDER, "cli.py")]
        exe = packaged_exe()
        if exe:
            yield "packaged", [exe]

    @classmethod
    def setUpClass(cls):
        cls.homes = {}
        # a tiny stand-in repository (just the build scripts): `plan` and `build --dry-run` then need no unpacking of the bundled source
        cls.repo = tempfile.mkdtemp(prefix="bb-cli-repo-")
        make_workspace(cls.repo, populated=False)
        for rel in ("fetch_assets.sh", "tools/deck/build_deck.sh"):
            p = os.path.join(cls.repo, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w").close()

    @classmethod
    def tearDownClass(cls):
        for h in list(cls.homes.values()) + [cls.repo]:
            shutil.rmtree(h, True)

    def run_cli(self, base, args, env_extra=None, timeout=120):
        home = self.homes.setdefault(base[0], tempfile.mkdtemp(prefix="bb-cli-home-"))
        env = dict(os.environ, BRAVELY_BUILDER_HOME=home, QT_QPA_PLATFORM="offscreen")
        env.pop("BRAVELY_FAKE_HOST", None)
        env.update(env_extra or {})
        if base[0] != sys.executable:                      # the packaged app takes the command as a flag
            args = ["--" + args[0]] + list(args[1:])
        p = subprocess.run(base + args, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, env=env)
        return p, home

    @unittest.skipUnless(os.name == "nt", "the Windows planner")
    def test_plan_on_the_real_host_lists_only_what_is_missing(self):
        for name, base in self.runner():
            with self.subTest(name):
                p, _ = self.run_cli(base, ["plan", "--repo", self.repo])
                self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
                self.assertIn("component  state", p.stdout)
                for row in p.stdout.splitlines():
                    if row.split()[:1] in (["msys2"], ["packages"]) and "PRESENT" in row:
                        self.assertIn("already installed, skipped", row)
                    if "PRESENT" in row:
                        self.assertNotIn(" MB ", row)              # nothing to download for what is there

    @unittest.skipUnless(os.name == "nt", "the Windows planner")
    def test_plan_with_an_empty_msys2_folder_lists_the_install(self):
        empty = tempfile.mkdtemp(prefix="bb-nomsys-")
        self.addCleanup(shutil.rmtree, empty, True)
        for name, base in self.runner():
            with self.subTest(name):
                p, _ = self.run_cli(base, ["plan", "--repo", self.repo], {"BRAVELY_MSYS2_ROOT": empty})
                self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
                rows = {ln.split()[0]: ln for ln in p.stdout.splitlines() if ln.split() and ln.split()[0] in ("msys2", "packages")}
                self.assertIn("MISSING", rows["msys2"])
                self.assertIn("85 MB", rows["msys2"])
                self.assertIn("MISSING", rows["packages"])
                self.assertRegex(p.stdout, r"Total download: about \d+ MB")

    @unittest.skipUnless(os.name == "nt", "the Windows build")
    def test_build_dry_run_prints_the_commands_and_installs_nothing(self):
        empty = tempfile.mkdtemp(prefix="bb-nomsys-")
        self.addCleanup(shutil.rmtree, empty, True)
        rom = os.path.join(tempfile.mkdtemp(prefix="bb-rom-"), "synthetic.gba")
        write_synthetic_rom(rom)
        self.addCleanup(shutil.rmtree, os.path.dirname(rom), True)
        for name, base in self.runner():
            for label, extra in (("this PC", {}), ("empty MSYS2 folder", {"BRAVELY_MSYS2_ROOT": empty})):
                with self.subTest("%s, %s" % (name, label)):
                    p, home = self.run_cli(base, ["build", "--dry-run", "--repo", self.repo, "--rom", rom, "--jobs", "2", "--out", os.path.join(home_tmp(), "out")],
                                           extra, timeout=240)
                    self.assertEqual(p.returncode, 0, p.stdout[-2000:] + p.stderr)
                    self.assertIn("[dry run] not executed", p.stdout)
                    self.assertIn("tools/windows/build.sh", p.stdout)
                    self.assertIn("@@ final: done", p.stdout)
                    if extra:
                        self.assertIn("Install MSYS2", p.stdout)
                        self.assertFalse(os.listdir(empty), "a dry run must not install anything")


def find_window(pid):
    """(hwnd, title, (width, height)) of a visible top-level window of the process, or (0, "", (0, 0))."""
    import ctypes
    from ctypes import wintypes
    u = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        owner = wintypes.DWORD()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and u.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(256)
            u.GetWindowTextW(hwnd, buf, 256)
            r = wintypes.RECT()
            u.GetWindowRect(hwnd, ctypes.byref(r))
            if buf.value:
                found.append((hwnd, buf.value, (r.right - r.left, r.bottom - r.top)))
        return True
    u.EnumWindows(each, 0)
    return found[0] if found else (0, "", (0, 0))


def save_window_bmp(hwnd, path):
    """PrintWindow of the window into a 24 bit BMP (readable by any viewer; no extra packages)."""
    import ctypes
    import struct
    from ctypes import wintypes
    u, g = ctypes.windll.user32, ctypes.windll.gdi32
    r = wintypes.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    dc = u.GetWindowDC(hwnd)
    mdc = g.CreateCompatibleDC(dc)
    bm = g.CreateCompatibleBitmap(dc, w, h)
    g.SelectObject(mdc, bm)
    u.PrintWindow(hwnd, mdc, 2)                     # PW_RENDERFULLCONTENT
    hdr = struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, 0, 0, 0, 0, 0)
    stride = (w * 3 + 3) & ~3
    buf = ctypes.create_string_buffer(stride * h)
    g.GetDIBits(mdc, bm, 0, h, buf, ctypes.create_string_buffer(hdr), 0)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", 14 + 40 + len(buf), 0, 0, 54) + hdr + buf.raw)
    g.DeleteObject(bm)
    g.DeleteDC(mdc)
    u.ReleaseDC(hwnd, dc)
    return True


def home_tmp():
    d = tempfile.mkdtemp(prefix="bb-out-")
    return d


@unittest.skipUnless(packaged_exe(), "no packaged app found (build it with tools/builder/package.py)")
class PackagedAppTests(unittest.TestCase):
    def run_exe(self, args, timeout=90):
        home = tempfile.mkdtemp(prefix="bb-pkg-home-")
        self.addCleanup(shutil.rmtree, home, True)
        env = dict(os.environ, BRAVELY_BUILDER_HOME=home, QT_QPA_PLATFORM="offscreen")
        p = subprocess.run([packaged_exe()] + args, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, env=env)
        return p, home

    def test_smoke(self):
        p, home = self.run_exe(["--smoke"])
        out = p.stdout
        if not out.strip() and os.path.exists(os.path.join(home, "smoke.txt")):
            out = open(os.path.join(home, "smoke.txt")).read()
        self.assertIn("SMOKE OK", out + p.stderr)

    def test_imports(self):
        p, _ = self.run_exe(["--imports"])
        self.assertIn("IMPORTS OK", p.stdout, p.stdout[-1500:] + p.stderr)
        for needed in ("core.crash", "core.instance", "core.probe", "core.report", "ui.main_window"):
            self.assertIn("OK   " + needed, p.stdout)

    @unittest.skipUnless(os.name == "nt", "window check uses the Win32 API")
    def test_launch_like_a_double_click(self):
        """Start the exe with no arguments, wait, find its window, take a picture of it (BRAVELY_LAUNCH_BMP names the file), close it."""
        home = tempfile.mkdtemp(prefix="bb-launch-home-")
        self.addCleanup(shutil.rmtree, home, True)
        env = dict(os.environ, BRAVELY_BUILDER_HOME=home)
        env.pop("QT_QPA_PLATFORM", None)                    # a real window, not the offscreen test platform
        proc = subprocess.Popen([packaged_exe()], env=env, cwd=tempfile.gettempdir())
        try:
            time.sleep(6)
            self.assertIsNone(proc.poll(), "the app exited by itself (exit code %s); crash.log: %s" % (
                proc.poll(), open(os.path.join(home, "crash.log")).read() if os.path.exists(os.path.join(home, "crash.log")) else "none"))
            hwnd, title, size = find_window(proc.pid)
            self.assertTrue(hwnd, "no visible window")
            self.assertIn("Bravely", title)
            self.assertGreater(size[0], 300)
            bmp = os.environ.get("BRAVELY_LAUNCH_BMP") or os.path.join(home, "window.bmp")
            self.assertTrue(save_window_bmp(hwnd, bmp))
            self.assertGreater(os.path.getsize(bmp), 10000)
        finally:
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True)

    def test_selftest(self):
        p, home = self.run_exe(["--selftest"], timeout=180)
        self.assertIn("SELFTEST OK", p.stdout, p.stdout[-1500:] + p.stderr)
        mf = os.path.join(home, "source", ".bravely-source.json")
        self.assertTrue(os.path.isfile(mf))
        guard = open(os.path.join(home, "source", "tools", "windows", "lib_cache.sh"), encoding="utf-8").read()
        self.assertIn("drop_foreign_build", guard)


if __name__ == "__main__":
    unittest.main()
