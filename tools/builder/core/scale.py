"""Whole-number UI scale. No Qt imports: the probe runs Qt in a child process, the rule is a pure function (tested).

The window is designed at 1x (WINDOW_SIZE, a normal Windows 95 dialog size) and only ever enlarged by 1, 2 or 3:
  natural = what the OS gives (its scale factor rounded down), and 2 on screens 2560 pixels wide or more that have no OS scaling
  cap     = the largest whole number that keeps the window within 55% of the screen width and of the screen height
  scale   = the smaller of the two; BRAVELY_UI_SCALE=1|2|3 overrides it.
"""
import json
import os
import subprocess
import sys

WINDOW_SIZE = (480, 360)
CAP_FRACTION = 0.55
MAX_SCALE = 3


def choose_scale(screen_w, screen_h, os_scale=1.0, window=WINDOW_SIZE, override=None):
    """Returns (scale, natural, cap). screen_w/h are physical pixels."""
    if override in (1, 2, 3):
        return override, override, override
    natural = max(1, int(os_scale))
    if screen_w >= 2560 and natural < 2:
        natural = 2
    cap = int(min(CAP_FRACTION * screen_w // window[0], CAP_FRACTION * screen_h // window[1]))
    cap = max(1, min(MAX_SCALE, cap))
    return min(natural, cap), natural, cap


PROBE_CODE = ("import json; from PySide6.QtGui import QGuiApplication; a = QGuiApplication([]); s = a.primaryScreen(); "
              "g = s.geometry(); d = s.devicePixelRatio(); print(json.dumps([int(g.width() * d), int(g.height() * d), d]))")


def probe_screen():
    """(physical width, physical height, OS scale) of the primary screen, found in a child process so that Qt in this process can
    still be configured afterwards. None when it cannot be found."""
    env = dict(os.environ)
    env.pop("QT_SCALE_FACTOR", None)
    env["QT_SCALE_FACTOR_ROUNDING_POLICY"] = "Floor"
    cmd = [sys.executable, "--probe-screen"] if getattr(sys, "frozen", False) else [sys.executable, "-c", PROBE_CODE]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20, env=env).stdout.strip().splitlines()
        w, h, d = json.loads(out[-1])
        return int(w), int(h), float(d)
    except Exception:
        return None


def configure(window=WINDOW_SIZE):
    """Set QT_SCALE_FACTOR (before Qt starts) so that the total scale is the chosen whole number. Returns a log line."""
    if os.environ.get("QT_SCALE_FACTOR"):
        return "UI scale from QT_SCALE_FACTOR=%s (set by the caller)" % os.environ["QT_SCALE_FACTOR"]
    override = os.environ.get("BRAVELY_UI_SCALE") or os.environ.get("BRAVELY_SCALE")
    override = int(override) if override and override.isdigit() else None
    if os.environ.get("QT_QPA_PLATFORM") == "offscreen" and not override:
        return "UI scale 1 (offscreen platform)"
    info = probe_screen()
    os.environ["QT_SCALE_FACTOR_ROUNDING_POLICY"] = "Floor"
    if info is None:
        scale, natural, cap = (override or 1), 1, 1
        screen = "unknown screen"
        os_scale = 1
    else:
        w, h, os_scale = info
        scale, natural, cap = choose_scale(w, h, os_scale, window, override)
        screen = "screen %dx%d, OS scale %g" % (w, h, os_scale)
    factor = scale / max(1, int(os_scale))                    # Qt already applies the OS scale; add only the difference
    if factor != 1:
        os.environ["QT_SCALE_FACTOR"] = repr(factor)
    return "UI scale %d (%s, natural %d, cap %d%s): window %dx%d px" % (scale, screen, natural, cap, ", override" if override else "",
                                                                          window[0] * scale, window[1] * scale)
