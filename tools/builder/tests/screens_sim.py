"""Renders the window on simulated desktops (1280x800 Steam Deck, 1920x1080, 3840x2160 at 100% and 200% OS scaling) at the scale
the app would choose, and reports the window size in pixels next to the 55% limit.

    python tools/builder/tests/screens_sim.py OUTDIR

Each case runs in its own process because the scale is fixed before Qt starts (QT_SCALE_FACTOR).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

CASES = [("deck_1280x800", 1280, 800, 1.0), ("fullhd_1920x1080", 1920, 1080, 1.0), ("qhd_2560x1440", 2560, 1440, 1.0),
         ("uhd_3840x2160_os100", 3840, 2160, 1.0), ("uhd_3840x2160_os200", 3840, 2160, 2.0)]


def child(name, sw, sh, scale, out):
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    os.environ["QT_SCALE_FACTOR"] = str(scale)
    import tempfile
    os.environ.setdefault("BRAVELY_BUILDER_HOME", tempfile.mkdtemp(prefix="bb-sim-"))
    from PySide6.QtCore import QPoint, QRect
    from PySide6.QtGui import QColor, QImage, QPainter
    from PySide6.QtWidgets import QApplication
    from ui import win95
    from ui.main_window import MainWindow
    app = QApplication([])
    win95.apply(app)
    w = MainWindow(None)
    w.no_modal = True
    w.show()
    import time
    t0 = time.time()
    while time.time() - t0 < 3:
        app.processEvents()
        time.sleep(0.02)
    pm = w.grab()
    img = pm.toImage()
    img.setDevicePixelRatio(1)
    desk = QImage(sw, sh, QImage.Format_RGB32)
    desk.fill(QColor("#008080"))                              # the Windows 95 desktop colour
    p = QPainter(desk)
    tb = 28 * scale if scale <= 2 else 28 * 2
    p.fillRect(QRect(0, sh - tb, sw, tb), QColor("#C0C0C0"))
    p.fillRect(QRect(0, sh - tb, sw, 2), QColor("#FFFFFF"))
    p.drawImage(QPoint((sw - img.width()) // 2, (sh - tb - img.height()) // 2), img)
    p.end()
    desk.save(os.path.join(out, name + ".png"))
    img.save(os.path.join(out, name + "_window.png"))
    print("%d %d" % (img.width(), img.height()))
    w.save = None
    w.close()


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    from core.scale import WINDOW_SIZE, choose_scale
    rows = []
    for name, sw, sh, os_scale in CASES:
        scale, natural, cap = choose_scale(sw, sh, os_scale)
        # QT_SCALE_FACTOR is the total here: the offscreen platform has no OS scaling of its own
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--child", name, str(sw), str(sh), str(scale), out],
                           capture_output=True, text=True, env=dict(os.environ, QT_QPA_FONTDIR=os.environ.get("QT_QPA_FONTDIR", "C:\\Windows\\Fonts")))
        wh = r.stdout.strip().splitlines()[-1].split() if r.stdout.strip() else ["?", "?"]
        ww, wh_ = int(wh[0]), int(wh[1])
        ok = ww <= 0.55 * sw and wh_ <= 0.55 * sh
        rows.append((name, sw, sh, os_scale, scale, ww, wh_, 100.0 * ww / sw, 100.0 * wh_ / sh, ok))
    print("%-22s %-11s %-4s %-6s %-12s %-9s %s" % ("case", "screen", "OS", "scale", "window px", "% w / h", "<=55%"))
    for n, sw, sh, o, s, ww, wh, pw, ph, ok in rows:
        print("%-22s %4dx%-6d %-4g %-6d %4dx%-7d %2.0f / %2.0f   %s" % (n, sw, sh, o, s, ww, wh, pw, ph, "yes" if ok else "NO"))
    return 0 if all(r[-1] for r in rows) else 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--child":
        child(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])
    else:
        sys.exit(main())
