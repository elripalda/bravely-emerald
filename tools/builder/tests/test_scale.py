"""The whole-number UI scale rule (core/scale.py): OS scale rounded down, then the 55% cap, override with BRAVELY_UI_SCALE."""
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core import scale as S      # noqa: E402


class ScaleTests(unittest.TestCase):
    W, H = S.WINDOW_SIZE

    def check(self, sw, sh, os_scale, expect):
        s, natural, cap = S.choose_scale(sw, sh, os_scale)
        self.assertEqual(s, expect, (sw, sh, os_scale))
        self.assertLessEqual(self.W * s, 0.55 * sw)
        self.assertLessEqual(self.H * s, 0.55 * sh)
        return s

    def test_window_is_a_normal_win95_dialog(self):
        self.assertEqual(S.WINDOW_SIZE, (480, 360))

    def test_small_and_full_hd_screens_get_1x(self):
        for sw, sh in ((1280, 800), (1366, 768), (1920, 1080), (1920, 1200), (2560, 1080)):
            self.check(sw, sh, 1.0, 1 if sw < 2560 or sh < 1440 else 2)

    def test_large_screens_get_2x(self):
        self.check(2560, 1440, 1.0, 2)
        self.check(3840, 2160, 1.0, 2)
        self.check(3840, 2160, 2.0, 2)

    def test_os_scale_is_respected_not_multiplied(self):
        self.check(1920, 1080, 1.5, 1)              # 150% rounds down to 1
        self.check(1920, 1080, 2.0, 1)              # 2x would be 72% of the height: the cap wins
        self.check(3840, 2160, 3.0, 3)              # 3x is 1080 high: 50%, allowed

    def test_never_more_than_3(self):
        s, _, _ = S.choose_scale(7680, 4320, 4.0)
        self.assertEqual(s, 3)

    def test_override(self):
        self.assertEqual(S.choose_scale(1920, 1080, 1.0, override=2)[0], 2)
        self.assertEqual(S.choose_scale(1920, 1080, 1.0, override=5)[0], 1)    # only 1, 2 or 3 are accepted

    def test_configure_sets_qt_factor_and_reports(self):
        env = {k: v for k, v in os.environ.items() if k not in ("QT_SCALE_FACTOR", "BRAVELY_UI_SCALE", "BRAVELY_SCALE", "QT_QPA_PLATFORM")}
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(S, "probe_screen", return_value=(2560, 1440, 1.0)):
            line = S.configure()
            self.assertEqual(os.environ["QT_SCALE_FACTOR"], "2.0")
            self.assertEqual(os.environ["QT_SCALE_FACTOR_ROUNDING_POLICY"], "Floor")
            self.assertIn("UI scale 2", line)
            self.assertIn("960x720", line)
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(S, "probe_screen", return_value=(1920, 1080, 1.0)):
            line = S.configure()
            self.assertNotIn("QT_SCALE_FACTOR", os.environ)          # 1x: nothing to add
            self.assertIn("window 480x360", line)
        with mock.patch.dict(os.environ, dict(env, BRAVELY_UI_SCALE="3"), clear=True), mock.patch.object(S, "probe_screen", return_value=(1920, 1080, 1.0)):
            line = S.configure()
            self.assertEqual(os.environ["QT_SCALE_FACTOR"], "3.0")
            self.assertIn("override", line)
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(S, "probe_screen", return_value=(3840, 2160, 2.0)):
            S.configure()
            self.assertNotIn("QT_SCALE_FACTOR", os.environ)          # the OS already gives 2x: not multiplied again


if __name__ == "__main__":
    unittest.main()
