"""Plans for fake host states: which install and build steps run for each state of the machine, so nothing that is already
there is installed, downloaded or built again."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-plan-"))

from core import Builder                          # noqa: E402
from core.constants import MSYS_PACKAGES          # noqa: E402
from cli import write_synthetic_rom               # noqa: E402


class FakeProbe:
    def __init__(self, msys=True, missing=(), deps="present", tools=True, assets=True, build_dir="present"):
        self.s = dict(msys=msys, missing=list(missing), deps=deps, tools=tools, assets=assets, build_dir=build_dir)

    def msys(self):
        return ("C:\\msys64", "default folder") if self.s["msys"] else ("C:\\msys64", None)

    def packages_missing(self):
        return self.s["missing"], None

    def deps(self):
        return self.s["deps"]

    def tools(self):
        return self.s["tools"]

    def assets(self):
        return self.s["assets"]

    def build_dir(self):
        return self.s["build_dir"]


# state -> (names the steps that run must start with, rows that must be reported present)
LAST3 = ["tools/windows/build.sh", "tools/windows/package.sh", "Copy the game"]
CASES = {
    "nothing installed": (dict(msys=False, deps="missing", tools=False, assets=False, build_dir="missing"),
                          ["Install MSYS2", "Update MSYS2", "Install the missing build packages", "Game files, pass 1", "Host tools",
                           "Converting", "Game files, pass 2"] + LAST3, []),
    "everything installed": (dict(), LAST3, ["msys2", "packages", "deps", "tools", "assets", "build_dir"]),
    "MSYS2 without packages": (dict(missing=MSYS_PACKAGES, deps="missing", tools=False, assets=False, build_dir="missing"),
                               ["Update MSYS2", "Install the missing build packages", "Game files, pass 1", "Host tools", "Converting",
                                "Game files, pass 2"] + LAST3, ["msys2"]),
    "packages but no SDL": (dict(deps="missing"), LAST3, ["msys2", "packages", "tools", "assets"]),
    "half built SDL": (dict(deps="outdated"), LAST3, ["msys2", "packages"]),
    "assets done, no compile": (dict(build_dir="missing"), LAST3, ["msys2", "packages", "deps", "tools", "assets"]),
    "one package missing": (dict(missing=["mingw-w64-i686-ninja"]), ["Update MSYS2", "Install the missing build packages"] + LAST3,
                            ["msys2", "deps", "assets"]),
    "no assets, rest installed": (dict(assets=False), ["Game files, pass 1", "Converting", "Game files, pass 2"] + LAST3,
                                  ["msys2", "packages", "deps", "tools"]),
}


class WindowsPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="bb-plan-repo-")
        for rel in ("fetch_assets.sh", "tools/deck/build_deck.sh", "tools/windows/build.sh"):
            os.makedirs(os.path.dirname(os.path.join(cls.tmp, rel)), exist_ok=True)
            open(os.path.join(cls.tmp, rel), "w").close()
        cls.rom = os.path.join(cls.tmp, "e.gba")
        write_synthetic_rom(cls.rom)
        cls.out = os.path.join(tempfile.mkdtemp(prefix="bb-plan-out-"), "game")

    def builder(self, probe):
        os.environ["BRAVELY_FAKE_HOST"] = "windows"
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)
        b = Builder(self.tmp, dry_run=True)
        b.probe = probe
        return b

    def test_cases(self):
        for name, (state, must_run, must_skip) in CASES.items():
            with self.subTest(name):
                b = self.builder(FakeProbe(**state))
                steps = b.plan_build({"targets": ["windows"], "jobs": 2, "rom": self.rom, "dir": self.out})[2]
                run = [s.name for s in steps if s.stage != "prereq"]
                self.assertEqual(len(run), len(must_run), run)
                for got, want in zip(run, must_run):
                    self.assertTrue(got.startswith(want), "%s: %r does not start with %r" % (name, got, want))
                rows = {r["id"]: r for r in b.plan("windows")}
                for rid in must_skip:
                    self.assertEqual(rows[rid]["state"], "present", (name, rid))
                    self.assertEqual(rows[rid]["mb"], 0)
                self.assertTrue(all(s.func for s in steps if s.stage == "prereq"))

    def test_skip_lines_are_logged(self):
        b = self.builder(FakeProbe())
        steps = b.plan_build({"targets": ["windows"], "jobs": 2, "rom": self.rom, "dir": self.out})[2]
        lines = []

        class T:
            def log(self, text):
                lines.append(text)
        for s in steps:
            if s.stage == "prereq":
                s.func(T())
        self.assertEqual(len(lines), 2)
        self.assertTrue(all("already installed" in x for x in lines))

    def test_plan_text_totals(self):
        b = self.builder(FakeProbe())
        self.assertIn("nothing to install", b.plan_text("windows"))
        b = self.builder(FakeProbe(msys=False, deps="missing", tools=False, assets=False, build_dir="missing"))
        t = b.plan_text("windows")
        self.assertIn("MISSING", t)
        self.assertNotIn("nothing to install", t)

    def test_deps_state_files(self):
        from core.probe import deps_state
        d = tempfile.mkdtemp(prefix="bb-deps-")
        self.assertEqual(deps_state(d), "missing")
        lib = os.path.join(d, "build-win", "deps", "lib")
        os.makedirs(os.path.join(lib, "pkgconfig"))
        self.assertEqual(deps_state(d), "missing")              # an empty prefix folder
        open(os.path.join(lib, "libSDL2.a"), "w").close()
        self.assertEqual(deps_state(d), "outdated")             # half built: one library only
        open(os.path.join(lib, "libSDL2_image.a"), "w").close()
        self.assertEqual(deps_state(d), "outdated")             # libraries without a matching version
        for f, v in (("sdl2.pc", "2.32.10"), ("SDL2_image.pc", "2.8.12")):
            with open(os.path.join(lib, "pkgconfig", f), "w") as fh:
                fh.write("Name: x\nVersion: %s\n" % v)
        self.assertEqual(deps_state(d), "present")
        with open(os.path.join(lib, "pkgconfig", "sdl2.pc"), "w") as fh:
            fh.write("Version: 2.30.0\n")
        self.assertEqual(deps_state(d), "outdated")


class MsysRootTests(unittest.TestCase):
    def test_env_override_wins(self):
        from core.probe import find_msys_root
        d = tempfile.mkdtemp(prefix="bb-msys-")
        self.assertEqual(find_msys_root(None, {"BRAVELY_MSYS2_ROOT": d}), (d, None))        # named but not installed yet
        os.makedirs(os.path.join(d, "usr", "bin"))
        open(os.path.join(d, "usr", "bin", "bash.exe"), "w").close()
        self.assertEqual(find_msys_root(None, {"BRAVELY_MSYS2_ROOT": d}), (d, "BRAVELY_MSYS2_ROOT"))


class LinuxPlanTests(unittest.TestCase):
    def test_rows_for_missing_and_present_tools(self):
        os.environ["BRAVELY_FAKE_HOST"] = "steamos"
        self.addCleanup(os.environ.pop, "BRAVELY_FAKE_HOST", None)
        b = Builder(tempfile.mkdtemp(prefix="bb-lin-"), dry_run=True)

        class P:
            def which(self, t): return None
            def container(self, n): return False
            def linux_deps(self, d): return False
            def assets(self): return False
            def linux_build_dir(self, d): return False
        b.probe = P()
        rows = {r["id"]: r for r in b.plan("linux32")}
        self.assertEqual(rows["podman"]["state"], "missing")
        self.assertEqual(rows["container"]["state"], "missing")
        self.assertGreater(sum(r["mb"] for r in rows.values()), 0)

        class Q(P):
            def which(self, t): return "/usr/bin/" + t
            def container(self, n): return True
            def linux_deps(self, d): return True
            def assets(self): return True
            def linux_build_dir(self, d): return True
        b.probe = Q()
        rows = b.plan("linux32")
        self.assertTrue(all(r["state"] == "present" and r["mb"] == 0 for r in rows))


if __name__ == "__main__":
    unittest.main()
