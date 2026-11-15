"""Tests of the bundled source: extraction, hash check, traversal, updates, and which repository is chosen."""
import hashlib
import io
import json
import os
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-src-"))

from core import source as S                      # noqa: E402
from core.repo import is_repo                      # noqa: E402

BASE = {"fetch_assets.sh": b"#!/bin/bash\n", "tools/deck/build_deck.sh": b"x", "tools/windows/build.sh": b"y",
        "src/a.c": b"int a;\n", "assets/voxel/buildings.bin": b"\0" * 100}


def make_bundle(files, commit="c" * 40, extra=None):
    """Write bundle/source.tar.xz + manifest in a temp folder. `extra(tar)` can add odd members."""
    d = tempfile.mkdtemp(prefix="bb-bundle-")
    arc = os.path.join(d, S.ARCHIVE_NAME)
    with tarfile.open(arc, "w:xz") as t:
        for name, data in files.items():
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            t.addfile(ti, io.BytesIO(data))
        if extra:
            extra(t)
    with open(arc, "rb") as f:
        h = hashlib.sha256(f.read()).hexdigest()
    with open(os.path.join(d, S.MANIFEST_NAME), "w") as f:
        json.dump({"commit": commit, "date": "2026-11-15T00:00:00+00:00", "files": len(files), "sha256": h, "archive": S.ARCHIVE_NAME}, f)
    return d


class ExtractTests(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="bb-work-")
        self.work = os.path.join(self.root, "source")

    def read(self, *parts):
        with open(os.path.join(self.work, *parts), "rb") as f:
            return f.read()

    def test_fresh_extract_then_reuse(self):
        b = make_bundle(BASE)
        self.assertTrue(S.ensure_extracted(b, self.work))
        self.assertTrue(is_repo(self.work))
        self.assertEqual(self.read("src", "a.c"), b"int a;\n")
        self.assertTrue(S.read_marker(self.work)["complete"])
        self.assertFalse(S.ensure_extracted(b, self.work))          # already current: nothing is done
        self.assertEqual([n for n in os.listdir(self.root) if n.startswith(".bravely-stage")], [])

    def test_hash_mismatch_writes_nothing(self):
        b = make_bundle(BASE)
        mp = os.path.join(b, S.MANIFEST_NAME)
        with open(mp) as f:
            m = json.load(f)
        m["sha256"] = "0" * 64
        with open(mp, "w") as f:
            json.dump(m, f)
        with self.assertRaises(S.SourceError) as cm:
            S.ensure_extracted(b, self.work)
        self.assertIn("does not match", str(cm.exception))
        self.assertFalse(os.path.exists(self.work))

    def test_tampered_archive_detected(self):
        b = make_bundle(BASE)
        with open(os.path.join(b, S.ARCHIVE_NAME), "ab") as f:
            f.write(b"x")
        with self.assertRaises(S.SourceError):
            S.ensure_extracted(b, self.work)

    def test_traversal_refused(self):
        for bad in ("../evil.txt", "a/../../evil.txt", "/abs/evil.txt", "C:/evil.txt", "..\\evil.txt"):
            files = dict(BASE)
            files[bad] = b"evil"
            with self.assertRaises(S.SourceError, msg=bad):
                S.ensure_extracted(make_bundle(files), self.work)
            self.assertFalse(os.path.exists(os.path.join(self.root, "evil.txt")))
            self.assertFalse(os.path.exists(self.work), bad)

    def test_symlink_refused(self):
        def add_link(t):
            ti = tarfile.TarInfo("link")
            ti.type = tarfile.SYMTYPE
            ti.linkname = "/etc/passwd"
            t.addfile(ti)
        with self.assertRaises(S.SourceError):
            S.ensure_extracted(make_bundle(BASE, extra=add_link), self.work)

    def test_newer_version_overlays_and_keeps_build_state(self):
        S.ensure_extracted(make_bundle(BASE, "a" * 40), self.work)
        for rel in ("build-win/deps/lib/libSDL2.a", ".assets_ok"):                     # what a build leaves behind
            p = os.path.join(self.work, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w") as f:
                f.write("built")
        t0 = os.path.getmtime(os.path.join(self.work, "tools", "windows", "build.sh"))
        v2 = dict(BASE)
        v2["src/a.c"] = b"int a2;\n"
        v2["src/new.c"] = b"new"
        del v2["tools/deck/build_deck.sh"]
        v2["tools/deck/build_deck2.sh"] = b"z"
        self.assertTrue(S.ensure_extracted(make_bundle(v2, "b" * 40), self.work))
        self.assertEqual(self.read("src", "a.c"), b"int a2;\n")
        self.assertTrue(os.path.isfile(os.path.join(self.work, "src", "new.c")))
        self.assertFalse(os.path.exists(os.path.join(self.work, "tools", "deck", "build_deck.sh")))   # dropped from the snapshot
        self.assertTrue(os.path.isfile(os.path.join(self.work, "build-win", "deps", "lib", "libSDL2.a")))
        self.assertTrue(os.path.isfile(os.path.join(self.work, ".assets_ok")))
        self.assertEqual(os.path.getmtime(os.path.join(self.work, "tools", "windows", "build.sh")), t0)   # unchanged: not rewritten
        self.assertEqual(S.read_marker(self.work)["commit"], "b" * 40)

    def test_interrupted_update_is_repeated(self):
        b = make_bundle(BASE)
        S.ensure_extracted(b, self.work)
        S._write_marker(self.work, S.read_manifest(b), False)        # an update that did not finish
        os.remove(os.path.join(self.work, "src", "a.c"))
        self.assertTrue(S.ensure_extracted(b, self.work))
        self.assertTrue(os.path.isfile(os.path.join(self.work, "src", "a.c")))

    def test_foreign_folder_is_not_overwritten(self):
        os.makedirs(self.work)
        with open(os.path.join(self.work, "mine.txt"), "w") as f:
            f.write("keep me")
        with self.assertRaises(S.SourceError):
            S.ensure_extracted(make_bundle(BASE), self.work)
        self.assertEqual(self.read("mine.txt"), b"keep me")

    def test_force_reextract(self):
        b = make_bundle(BASE)
        S.ensure_extracted(b, self.work)
        os.remove(os.path.join(self.work, "src", "a.c"))
        self.assertTrue(S.ensure_extracted(b, self.work, force=True))
        self.assertTrue(os.path.isfile(os.path.join(self.work, "src", "a.c")))


class ExcludeListTests(unittest.TestCase):
    def test_lists(self):
        for gone in ("docs/screens/x.png", "docs/BUILD.md", "design/stitch/hub/index.html", "tools/builder/app.py", "README.md", ".github/x.md"):
            self.assertTrue(S.excluded(gone), gone)
        for kept in ("assets/voxel/buildings.bin", "src/main.c", "CMakeLists.txt", "fetch_assets.sh", "tools/windows/build.sh",
                     "tools/deck/build_deck.sh", "design/stitch/_tools/export_assets.py", "docs/licenses/Cinzel-OFL.txt", "vita/assets.mk",
                     "LICENSE", "data/x.json"):
            self.assertFalse(S.excluded(kept), kept)


class ChoiceTests(unittest.TestCase):
    """Which repository the app uses."""
    REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

    def test_bundled_by_default_when_present(self):
        b = make_bundle(BASE)
        with mock.patch.object(S, "bundle_dir", return_value=b):
            s = S.Source(own_checkout="", work_dir=os.path.join(tempfile.mkdtemp(), "w"))
            self.assertEqual(s.mode, "bundled")
            self.assertIn("Bundled source: cccccccccc", s.describe())
            self.assertTrue(is_repo(s.prepare()))

    def test_advanced_own_checkout_wins_and_is_not_modified(self):
        b = make_bundle(BASE)
        before = sorted(os.listdir(self.REPO))
        with mock.patch.object(S, "bundle_dir", return_value=b):
            s = S.Source(own_checkout=self.REPO, work_dir=os.path.join(tempfile.mkdtemp(), "w"))
            self.assertEqual(s.mode, "own")
            self.assertEqual(s.prepare(), self.REPO)
            self.assertFalse(os.path.exists(s.work))                # nothing was extracted
        self.assertEqual(sorted(os.listdir(self.REPO)), before)

    def test_repo_argument_wins(self):
        b = make_bundle(BASE)
        with mock.patch.object(S, "bundle_dir", return_value=b):
            self.assertEqual(S.Source(own_checkout=self.REPO, force_repo=self.REPO).mode, "repo-arg")

    def test_invalid_own_checkout_falls_back(self):
        b = make_bundle(BASE)
        with mock.patch.object(S, "bundle_dir", return_value=b):
            self.assertEqual(S.Source(own_checkout=tempfile.mkdtemp()).mode, "bundled")

    def test_source_checkout_without_bundle(self):
        with mock.patch.object(S, "bundle_dir", return_value=None):
            s = S.Source(own_checkout="")
            self.assertEqual(s.mode, "checkout")                    # running from a checkout: it is the repository
            self.assertEqual(os.path.normcase(s.repo_path()), os.path.normcase(self.REPO))

    def test_nothing_available(self):
        with mock.patch.object(S, "bundle_dir", return_value=None), mock.patch.object(S, "guess_repo", return_value=""):
            s = S.Source(own_checkout="")
            self.assertEqual(s.mode, "none")
            with self.assertRaises(S.SourceError):
                s.prepare()

    @unittest.skipUnless(os.path.isfile(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "bundle", S.ARCHIVE_NAME)),
                         "no real bundle (run package.py)")
    def test_real_bundle_extracts(self):
        s = S.Source(own_checkout="", work_dir=os.path.join(tempfile.mkdtemp(), "w"))
        path = s.prepare()
        self.assertTrue(is_repo(path))
        self.assertTrue(os.path.isfile(os.path.join(path, "assets", "voxel", "buildings.bin")))
        self.assertTrue(os.path.isfile(os.path.join(path, "design", "stitch", "_tools", "export_assets.py")))
        self.assertFalse(os.path.exists(os.path.join(path, ".git")))


if __name__ == "__main__":
    unittest.main()
