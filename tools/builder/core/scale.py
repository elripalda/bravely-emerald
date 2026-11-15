"""UI scale. No Qt imports: the probe runs Qt in a child process, the rule is a pure function (tested).

The window is designed at 1x (WINDOW_SIZE, a normal Windows 95 dialog size). The text is a vector font and the bevels are drawn in
device pixels, so the scale can be fractional (1.0, 1.25, 1.5, 1.75, 2.0 ...). The rule:
  target  = the scale that makes the window about 42% of the screen height (the middle of 40 to 45%), in steps of 0.25
  os      = the scale the operating system gives (Windows 125% or 150%, a 2x display), used when it is larger than the target
  cap     = the largest step that keeps the window within 55% of the screen width and height
  scale   = max(os, target), but never above the cap, never below 1.0; BRAVELY_UI_SCALE=1.25 (decimals) overrides it.
Qt applies the OS scale itself (rounding policy PassThrough: 125% stays 125%), so QT_SCALE_FACTOR only carries scale / os.
"""
import json
import math
import os
import subprocess
import sys

WINDOW_SIZE = (480, 360)
TARGET_FRACTION = 0.42        # of the screen height; 0.40 to 0.45 is the aim
CAP_FRACTION = 0.55
STEP = 0.25
MIN_SCALE = 1.0
MAX_SCALE = 4.0


def _floor_step(x):
    return math.floor(x / STEP + 1e-9) * STEP


def _round_step(x):
    return round(x / STEP) * STEP


def choose_scale(screen_w, screen_h, os_scale=1.0, window=WINDOW_SIZE, override=None):
    """Returns (scale, target, cap). screen_w/h are physical pixels, os_scale is the OS device pixel ratio (may be fractional)."""
    if override:
        v = max(0.5, min(MAX_SCALE, float(override)))
        return v, v, v
    target = _round_step(TARGET_FRACTION * screen_h / window[1])
    cap = _floor_step(min(CAP_FRACTION * screen_w / window[0], CAP_FRACTION * screen_h / window[1]))
    cap = max(MIN_SCALE, min(MAX_SCALE, cap))
    wanted = max(MIN_SCALE, target, _round_step(os_scale) if os_scale > 1 else MIN_SCALE)
    return min(wanted, cap), max(MIN_SCALE, target), cap


PROBE_CODE = ("import json; from PySide6.QtGui import QGuiApplication; a = QGuiApplication([]); s = a.primaryScreen(); "
              "g = s.geometry(); d = s.devicePixelRatio(); print(json.dumps([int(g.width() * d), int(g.height() * d), d]))")


def probe_screen():
    """(physical width, physical height, OS scale) of the primary screen, found in a child process so that Qt in this process can
    still be configured afterwards. None when it cannot be found."""
    env = dict(os.environ)
    env.pop("QT_SCALE_FACTOR", None)
    env["QT_SCALE_FACTOR_ROUNDING_POLICY"] = "PassThrough"
    cmd = [sys.executable, "--probe-screen"] if getattr(sys, "frozen", False) else [sys.executable, "-c", PROBE_CODE]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20, env=env).stdout.strip().splitlines()
        w, h, d = json.loads(out[-1])
        return int(w), int(h), float(d)
    except Exception:
        return None


def parse_override(text):
    try:
        v = float((text or "").replace(",", "."))
        return v if 0.5 <= v <= MAX_SCALE else None
    except ValueError:
        return None


def configure(window=WINDOW_SIZE, whole_numbers=False):
    """Set QT_SCALE_FACTOR (before Qt starts) so that the total scale is the chosen one. Returns a log line.
    whole_numbers: the classic pixel font only looks right at whole multiples of its 16 pixel grid."""
    os.environ["QT_SCALE_FACTOR_ROUNDING_POLICY"] = "PassThrough"
    if os.environ.get("QT_SCALE_FACTOR"):
        return "UI scale from QT_SCALE_FACTOR=%s (set by the caller)" % os.environ["QT_SCALE_FACTOR"]
    override = parse_override(os.environ.get("BRAVELY_UI_SCALE") or os.environ.get("BRAVELY_SCALE"))
    if os.environ.get("QT_QPA_PLATFORM") == "offscreen" and not override:
        return "UI scale 1 (offscreen platform)"
    info = probe_screen()
    if info is None:
        scale, target, cap = (override or 1.0), 1.0, 1.0
        screen = "unknown screen"
        os_scale = 1.0
    else:
        w, h, os_scale = info
        scale, target, cap = choose_scale(w, h, os_scale, window, override)
        screen = "screen %dx%d, OS scale %g" % (w, h, os_scale)
    if whole_numbers and not override:
        scale = max(1, int(scale))
    factor = scale / max(1.0, os_scale)                       # Qt already applies the OS scale; add only the difference
    if abs(factor - 1) > 1e-6:
        os.environ["QT_SCALE_FACTOR"] = repr(round(factor, 4))
    return "UI scale %g (%s, target %g, cap %g%s): window %dx%d px" % (scale, screen, target, cap, ", override" if override else "",
                                                                         round(window[0] * scale), round(window[1] * scale))
