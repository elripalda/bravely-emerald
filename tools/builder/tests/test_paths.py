"""Folders the build scripts cannot work in are refused with a plain message before anything starts; short user names and
plain paths such as the one in issue 3 are accepted."""
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-paths-"))

from core.validate import source_path_problem     # noqa: E402
from core import source                           # noqa: E402


class PathProblemTests(unittest.TestCase):
    def test_accepted(self):
        for p in ("C:\\Users\\Adame\\AppData\\Local\\BravelyEmerald\\source", "F:\\fresh-test\\home\\source", "C:\\BravelyEmerald\\source",
                  "/home/deck/emerald-deck"):
            self.assertIsNone(source_path_problem(p), p)

    def test_refused_with_a_plain_message(self):
        cases = {"C:\\Users\\Ana Maria\\AppData\\Local\\x": "space", "C:\\Users\\Jos\u00e9\\AppData\\Local\\x": "accent",
                 "\\\\server\\share\\x": "network", "C:\\a'b": "symbols", "C:\\" + "x" * 120: "long"}
        for p, word in cases.items():
            msg = source_path_problem(p)
            self.assertTrue(msg and "BravelyEmerald" in msg or word == "symbols" or word == "long", (p, msg))
            self.assertIsNotNone(msg, p)

    def test_default_folder_falls_back_for_accents_and_spaces(self):
        with mock.patch.object(source.os, "name", "nt"), \
             mock.patch.dict(os.environ, {"LOCALAPPDATA": "C:\\Users\\Jos\u00e9\\AppData\\Local", "SystemDrive": "C:"}, clear=False):
            os.environ.pop("BRAVELY_BUILDER_HOME", None)
            try:
                p = source.default_work_dir()
            finally:
                os.environ["BRAVELY_BUILDER_HOME"] = tempfile.mkdtemp(prefix="bb-paths-")
        self.assertTrue(p.replace("/", "\\").lower().endswith("c:\\bravelyemerald\\source"), p)


if __name__ == "__main__":
    unittest.main()
