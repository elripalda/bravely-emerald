"""Windows 95 look: Qt's classic 'Windows' style, the classic 16-colour palette, a small UI font, flat icons.
No style sheets, no gradients, no rounded corners. The 'Windows' style is built into Qt on every platform."""
from PySide6.QtCore import QPoint, QRect, Qt
from PySide6.QtGui import QColor, QFont, QFontDatabase, QIcon, QPainter, QPalette, QPen, QPixmap, QPolygon

FACE = "#C0C0C0"
NAVY = "#000080"


def apply(app):
    app.setStyle("Windows")
    p = QPalette()
    c = QColor
    p.setColor(QPalette.Window, c(FACE))
    p.setColor(QPalette.WindowText, c("#000000"))
    p.setColor(QPalette.Base, c("#FFFFFF"))
    p.setColor(QPalette.AlternateBase, c("#FFFFFF"))
    p.setColor(QPalette.Text, c("#000000"))
    p.setColor(QPalette.Button, c(FACE))
    p.setColor(QPalette.ButtonText, c("#000000"))
    p.setColor(QPalette.BrightText, c("#FFFFFF"))
    p.setColor(QPalette.Light, c("#FFFFFF"))        # button highlight
    p.setColor(QPalette.Midlight, c("#DFDFDF"))
    p.setColor(QPalette.Mid, c("#808080"))
    p.setColor(QPalette.Dark, c("#808080"))         # button shadow
    p.setColor(QPalette.Shadow, c("#000000"))       # dark shadow
    p.setColor(QPalette.Highlight, c(NAVY))
    p.setColor(QPalette.HighlightedText, c("#FFFFFF"))
    p.setColor(QPalette.ToolTipBase, c("#FFFFE1"))
    p.setColor(QPalette.ToolTipText, c("#000000"))
    p.setColor(QPalette.Link, c("#0000FF"))
    p.setColor(QPalette.LinkVisited, c("#800080"))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        p.setColor(QPalette.Disabled, role, c("#808080"))
    p.setColor(QPalette.Disabled, QPalette.Highlight, c("#808080"))
    app.setPalette(p)
    have = set(QFontDatabase.families())
    for fam in ("MS Sans Serif", "Microsoft Sans Serif", "Tahoma", "DejaVu Sans", "Arial"):
        if fam in have:
            f = QFont(fam, 9)
            f.setHintingPreference(QFont.PreferFullHinting)
            f.setStyleStrategy(QFont.NoAntialias)
            app.setFont(f)
            break


def mono_font():
    have = set(QFontDatabase.families())
    for fam in ("Courier New", "Lucida Console", "DejaVu Sans Mono", "Courier"):
        if fam in have:
            f = QFont(fam, 9)
            break
    else:
        f = QFont()
        f.setStyleHint(QFont.Monospace)
        f.setPointSize(9)
    f.setStyleStrategy(QFont.NoAntialias)
    return f


def _canvas(size):
    pm = QPixmap(size, size)
    pm.fill(Qt.transparent)
    q = QPainter(pm)
    q.setRenderHint(QPainter.Antialiasing, False)
    return pm, q


def _poly(q, pts, fill, edge="#000000"):
    q.setPen(QPen(QColor(edge), 1))
    q.setBrush(QColor(fill))
    q.drawPolygon(QPolygon([QPoint(x, y) for x, y in pts]))


def gem_icon():
    """A flat emerald gem, 32x32, 16-colour palette."""
    out = QIcon()
    for size in (32, 16):
        pm, q = _canvas(32)
        _poly(q, [(9, 4), (22, 4), (29, 12), (16, 29), (3, 12)], "#008000")
        _poly(q, [(9, 4), (22, 4), (19, 12), (12, 12)], "#00FF00")
        _poly(q, [(3, 12), (12, 12), (16, 29)], "#008080")
        _poly(q, [(29, 12), (19, 12), (16, 29)], "#004000" if False else "#008000")
        q.setPen(QPen(QColor("#FFFFFF"), 1))
        q.drawLine(10, 6, 13, 6)
        q.end()
        out.addPixmap(pm if size == 32 else pm.scaled(16, 16, Qt.IgnoreAspectRatio, Qt.FastTransformation))
    return out


def flat_icon(kind, size=32):
    """Flat message-box icons: question, info, warning, error, tick, cross."""
    pm, q = _canvas(32)
    f = QFont()
    f.setBold(True)
    f.setPixelSize(22)
    f.setStyleStrategy(QFont.NoAntialias)
    q.setFont(f)
    if kind in ("question", "info"):
        q.setPen(QPen(QColor("#000000"), 1))
        q.setBrush(QColor("#FFFFFF"))
        q.drawEllipse(QRect(2, 2, 27, 27))
        q.setPen(QColor(NAVY))
        q.drawText(QRect(2, 2, 28, 28), Qt.AlignCenter, "?" if kind == "question" else "i")
    elif kind == "warning":
        _poly(q, [(16, 2), (30, 29), (2, 29)], "#FFFF00")
        q.setPen(QColor("#000000"))
        q.drawText(QRect(2, 8, 28, 22), Qt.AlignCenter, "!")
    elif kind == "error":
        q.setPen(QPen(QColor("#000000"), 1))
        q.setBrush(QColor("#FF0000"))
        q.drawEllipse(QRect(2, 2, 27, 27))
        q.setPen(QPen(QColor("#FFFFFF"), 3))
        q.drawLine(10, 10, 21, 21)
        q.drawLine(21, 10, 10, 21)
    elif kind == "tick":
        q.setPen(QPen(QColor("#008000"), 4))
        q.drawLine(5, 17, 12, 25)
        q.drawLine(12, 25, 27, 7)
    elif kind == "cross":
        q.setPen(QPen(QColor("#800000"), 4))
        q.drawLine(6, 6, 26, 26)
        q.drawLine(26, 6, 6, 26)
    q.end()
    return pm if size == 32 else pm.scaled(size, size, Qt.IgnoreAspectRatio, Qt.FastTransformation)
