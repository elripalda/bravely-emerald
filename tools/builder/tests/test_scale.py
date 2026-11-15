"""The UI scale rule (core/scale.py: window about 42% of the screen height in 0.25 steps, cap 55%, OS scale respected, override with
decimals) and the font rule (ui/win95.py: scalable system sans serif, classic pixel font only when asked)."""
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core import scale as S      # noqa: E402


class ScaleTests(unittest.TestCase):
    W, H = S.WINDOW_SIZE

    def check(self, sw, sh, os_scale, expect):
        s, target, cap = S.choose_scale(sw, sh, os_scale)
        self.assertEqual(s, expect, (sw, sh, os_scale))
        self.assertGreaterEqual(s, 1.0)
        self.assertLessEqual(self.H * s, max(0.55 * sh, self.H), (sw, sh))        # the cap (a tiny screen still gets 1.0)
        return s

    def test_window_is_a_normal_win95_dialog(self):
        self.assertEqual(S.WINDOW_SIZE, (480, 360))

    def test_table_of_real_screens(self):
        table = [  # (width, height, OS scale) -> scale
            (1280, 800, 1.0, 1.0),        # Steam Deck: 45%
            (1366, 768, 1.0, 1.0),
            (1920, 1080, 1.0, 1.25),      # 450 px high: 42%
            (2560, 1080, 1.0, 1.25),      # Marcos' ultrawide: 600x450, was 480x360 (33%)
            (1920, 1200, 1.0, 1.5),       # 540 px: 45%
            (2560, 1440, 1.0, 1.75),      # 630 px: 44%
            (3840, 2160, 1.0, 2.5),       # 900 px: 42%
        ]
        for sw, sh, osc, expect in table:
            with self.subTest(screen="%dx%d os %g" % (sw, sh, osc)):
                s = self.check(sw, sh, osc, expect)
                share = self.H * s / sh
                self.assertTrue(0.40 <= share <= 0.55, share)

    def test_window_share_of_height_is_40_to_45_percent_where_the_screen_allows(self):
        for sh in (1080, 1200, 1440, 1600, 2160):
            s, _, _ = S.choose_scale(sh * 16 // 9, sh)
            self.assertTrue(0.38 <= self.H * s / sh <= 0.46, (sh, s))      # 0.25 steps allow a little either way

    def test_os_scaling_is_respected_and_fractional(self):
        self.check(1920, 1080, 1.25, 1.25)           # Windows 125%: the target is the same, text stays crisp (PassThrough)
        self.check(1920, 1080, 1.5, 1.5)             # 150%: 540 px = 50%, allowed
        self.check(1920, 1080, 2.0, 1.5)             # 2x would be 67% of the height: the 55% cap wins (1.5)
        self.check(3840, 2160, 2.0, 2.5)             # the target is larger than the OS scale
        self.check(3840, 2160, 3.0, 3.0)             # 3x is 1080 px: 50%

    def test_never_below_1_or_above_4(self):
        self.assertEqual(S.choose_scale(800, 600, 1.0)[0], 1.0)
        self.assertEqual(S.choose_scale(7680, 4320, 1.0)[0], 4.0)

    def test_override_accepts_decimals(self):
        self.assertEqual(S.choose_scale(1920, 1080, 1.0, override=1.75)[0], 1.75)
        self.assertEqual(S.parse_override("1,5"), 1.5)
        self.assertEqual(S.parse_override("2"), 2.0)
        self.assertIsNone(S.parse_override("abc"))
        self.assertIsNone(S.parse_override("99"))

    def test_configure_sets_qt_factor_policy_and_reports(self):
        env = {k: v for k, v in os.environ.items() if k not in ("QT_SCALE_FACTOR", "BRAVELY_UI_SCALE", "BRAVELY_SCALE", "QT_QPA_PLATFORM")}
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(S, "probe_screen", return_value=(2560, 1080, 1.0)):
            line = S.configure()
            self.assertEqual(os.environ["QT_SCALE_FACTOR"], "1.25")
            self.assertEqual(os.environ["QT_SCALE_FACTOR_ROUNDING_POLICY"], "PassThrough")
            self.assertIn("UI scale 1.25", line)
            self.assertIn("600x450", line)
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(S, "probe_screen", return_value=(1280, 800, 1.0)):
            line = S.configure()
            self.assertNotIn("QT_SCALE_FACTOR", os.environ)          # 1.0: nothing to add
            self.assertIn("480x360", line)
        with mock.patch.dict(os.environ, dict(env, BRAVELY_UI_SCALE="1.75"), clear=True), mock.patch.object(S, "probe_screen", return_value=(1920, 1080, 1.0)):
            line = S.configure()
            self.assertEqual(os.environ["QT_SCALE_FACTOR"], "1.75")
            self.assertIn("override", line)
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(S, "probe_screen", return_value=(1920, 1080, 1.5)):
            S.configure()
            self.assertNotIn("QT_SCALE_FACTOR", os.environ)          # the OS already gives 150%: not multiplied again
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(S, "probe_screen", return_value=(2560, 1440, 1.0)):
            S.configure(whole_numbers=True)                           # classic pixel font: whole multiples only
            self.assertEqual(float(os.environ.get("QT_SCALE_FACTOR", "1")), 1.0)


class FontRuleTests(unittest.TestCase):
    def setUp(self):
        from ui import win95
        self.w = win95

    def test_windows_prefers_microsoft_sans_serif_then_tahoma(self):
        pick = self.w.pick_family
        self.assertEqual(pick(["Tahoma", "Microsoft Sans Serif", "Arial"], True), "Microsoft Sans Serif")
        self.assertEqual(pick(["Tahoma", "Arial"], True), "Tahoma")
        self.assertEqual(pick(["Comic Sans MS"], True), None)

    def test_linux_uses_liberation_dejavu_noto(self):
        pick = self.w.pick_family
        self.assertEqual(pick(["DejaVu Sans", "Noto Sans", "Liberation Sans"], False), "Liberation Sans")
        self.assertEqual(pick(["Noto Sans", "DejaVu Sans"], False), "DejaVu Sans")
        self.assertEqual(pick(["Noto Sans"], False), "Noto Sans")
        self.assertEqual(pick(["Consolas", "Courier New"], True, mono=True), "Consolas")
        self.assertEqual(pick(["DejaVu Sans Mono"], False, mono=True), "DejaVu Sans Mono")

    def test_point_sizes_not_pixel_sizes(self):
        from PySide6.QtGui import QFont
        from PySide6.QtWidgets import QApplication
        QApplication.instance() or QApplication([])
        with mock.patch.dict(os.environ, {"BRAVELY_FONT": "system"}):
            self.w._family = None
            body, label = self.w.ui_font(), self.w.ui_font(1, True)
            self.assertEqual(body.pointSizeF(), 9.0)
            self.assertEqual(label.pointSizeF(), 10.0)
            self.assertTrue(label.bold())
            self.assertEqual(body.styleStrategy() & QFont.PreferAntialias, QFont.PreferAntialias)
            self.assertFalse(self.w.uses_pixel_font())
            self.w._family = None

    def test_classic_font_only_when_asked(self):
        with mock.patch.dict(os.environ, {"BRAVELY_FONT": "classic"}):
            self.assertTrue(self.w.classic_font_wanted())
        with mock.patch.dict(os.environ, {"BRAVELY_FONT": "system"}):
            self.assertFalse(self.w.classic_font_wanted())
        env = {k: v for k, v in os.environ.items() if k != "BRAVELY_FONT"}
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.dict(os.environ, {"BRAVELY_BUILDER_HOME": os.path.join(os.environ.get("TEMP", "."), "bb-font-none")}):
            self.assertFalse(self.w.classic_font_wanted())            # the default is off


if __name__ == "__main__":
    unittest.main()
