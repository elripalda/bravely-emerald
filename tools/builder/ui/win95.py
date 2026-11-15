"""Windows 95 look, built from the 'Windows Standard' scheme of Windows 95 (the "Windows Classic" column of
https://www.brethorsting.com/blog/2007/12/windows-system-colors/ ), not from the later Windows 2000 / XP classic scheme.

Colour table (docs/BUILDER.md, 'Look'):
  face / 3D face            #C0C0C0     3D highlight (ButtonHilight) #FFFFFF    3D light (ButtonLight)   #C0C0C0
  3D shadow (ButtonShadow)  #808080     3D dark shadow               #000000    text                     #000000
  window (text fields)      #FFFFFF     active caption / selection   #000080    caption text             #FFFFFF
  inactive caption          #808080     inactive caption text        #C0C0C0    tooltip                  #FFFFE1
  disabled text (GrayText)  #808080

Edges are built like GDI DrawEdge:
  raised  : outer top-left HILIGHT, outer bottom-right DKSHADOW, inner top-left LIGHT, inner bottom-right SHADOW
  sunken  : outer top-left SHADOW,  outer bottom-right HILIGHT,  inner top-left DKSHADOW, inner bottom-right LIGHT
  etched  : 1 px SHADOW over 1 px HILIGHT
All painting is integer pixels with no antialiasing; the whole UI is scaled by an integer factor (QT_SCALE_FACTOR) only.
"""
import os

from PySide6.QtCore import QPoint, QRect, Qt
from PySide6.QtGui import QColor, QFont, QFontDatabase, QIcon, QPainter, QPalette, QPen, QPixmap, QPolygon
from PySide6.QtWidgets import QProxyStyle, QStyle, QStyleOptionButton

FACE = QColor("#C0C0C0")
HILIGHT = QColor("#FFFFFF")
LIGHT = QColor("#C0C0C0")        # ButtonLight of Windows 95 (the same as the face)
SHADOW = QColor("#808080")
DKSHADOW = QColor("#000000")
WINDOW = QColor("#FFFFFF")
TEXT = QColor("#000000")
NAVY = QColor("#000080")
GRAY_TEXT = QColor("#808080")
INACTIVE_TEXT = QColor("#C0C0C0")
TOOLTIP = QColor("#FFFFE1")

BASE_PX = 16                     # W95FA is drawn on a 16 pixel grid: one font pixel = one screen pixel at 1x
FONT_FILE = "W95FA.otf"
_family = None


# ---------------------------------------------------------------------------------------------- fonts
def font_path():
    here = os.path.dirname(os.path.abspath(__file__))
    import sys
    cands = []
    if getattr(sys, "frozen", False):
        cands.append(os.path.join(getattr(sys, "_MEIPASS", os.path.dirname(sys.executable)), "fonts", FONT_FILE))
    cands.append(os.path.join(os.path.dirname(here), "fonts", FONT_FILE))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def load_font():
    """Register the bundled W95FA (SIL OFL 1.1) and return the family name; falls back to Microsoft Sans Serif, Tahoma, DejaVu Sans."""
    global _family
    if _family:
        return _family
    if not os.environ.get("BRAVELY_FONT") == "system":
        path = font_path()
        if path:
            fid = QFontDatabase.addApplicationFont(path)
            fams = QFontDatabase.applicationFontFamilies(fid) if fid >= 0 else []
            if fams:
                _family = fams[0]
                return _family
    have = set(QFontDatabase.families())
    for fam in ("MS Sans Serif", "Microsoft Sans Serif", "Tahoma", "DejaVu Sans", "Arial"):
        if fam in have:
            _family = fam
            return _family
    _family = QFont().family()
    return _family


def uses_pixel_font():
    return load_font() == "W95FA"


def ui_font(scale=1, bold=False):
    """The UI font: W95FA at 16 px (an integer multiple for big labels), crisp: no antialiasing, full hinting."""
    f = QFont(load_font())
    if uses_pixel_font():
        f.setPixelSize(BASE_PX * scale)
    else:
        f.setPixelSize(11 * scale)                 # MS Sans Serif / Tahoma 8 pt
    f.setBold(bold)
    f.setStyleStrategy(QFont.NoAntialias)
    f.setHintingPreference(QFont.PreferFullHinting)
    return f


def mono_font():
    have = set(QFontDatabase.families())
    for fam in ("Courier New", "Lucida Console", "DejaVu Sans Mono", "Courier"):
        if fam in have:
            f = QFont(fam)
            break
    else:
        f = QFont()
        f.setStyleHint(QFont.Monospace)
    f.setPixelSize(13)
    f.setStyleStrategy(QFont.NoAntialias)
    return f


# ---------------------------------------------------------------------------------------------- edge drawing
def _hline(p, x, y, w, c):
    if w > 0:
        p.fillRect(x, y, w, 1, c)


def _vline(p, x, y, h, c):
    if h > 0:
        p.fillRect(x, y, 1, h, c)


def edge(p, r, tl_outer, br_outer, tl_inner, br_inner, face=None):
    """Two pixel edge in the DrawEdge way. r is the QRect of the whole element."""
    x, y, w, h = r.x(), r.y(), r.width(), r.height()
    if face is not None:
        p.fillRect(r, face)
    # outer ring: top-left lines stop one pixel short, bottom-right lines own the corners
    _hline(p, x, y, w - 1, tl_outer)
    _vline(p, x, y, h - 1, tl_outer)
    _hline(p, x, y + h - 1, w, br_outer)
    _vline(p, x + w - 1, y, h, br_outer)
    # inner ring
    _hline(p, x + 1, y + 1, w - 3, tl_inner)
    _vline(p, x + 1, y + 1, h - 3, tl_inner)
    _hline(p, x + 1, y + h - 2, w - 2, br_inner)
    _vline(p, x + w - 2, y + 1, h - 2, br_inner)


def raised(p, r, face=FACE):
    edge(p, r, HILIGHT, DKSHADOW, LIGHT, SHADOW, face)


def sunken(p, r, face=None):
    edge(p, r, SHADOW, HILIGHT, DKSHADOW, LIGHT, face)


def pressed(p, r, face=FACE):
    """A pushed button: the raised edge inverted."""
    edge(p, r, DKSHADOW, HILIGHT, SHADOW, LIGHT, face)


def window_frame(p, r):
    """The 2 px raised window border: light gray over white at the top left, black over dark gray at the bottom right."""
    edge(p, r, LIGHT, DKSHADOW, HILIGHT, SHADOW)


def etched_h(p, x, y, w):
    _hline(p, x, y, w, SHADOW)
    _hline(p, x, y + 1, w, HILIGHT)


def draw_bitmap(p, x, y, rows, color, scale=1):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "X":
                p.fillRect(x + i * scale, y + j * scale, scale, scale, color)


CHECK = ("......X", ".....XX", "X...XXX", "XX.XXX.", "XXXXX..", ".XXX...", "..X....")
GLYPH_HELP = ("..XXXX..", ".XX..XX.", ".XX..XX.", "....XX..", "...XX...", "...XX...", "........", "...XX...", "...XX...")
GLYPH_CLOSE = ("XX....XX", ".XX..XX.", "..XXXX..", "...XX...", "..XXXX..", ".XX..XX.", "XX....XX")


# ---------------------------------------------------------------------------------------------- the style
class Win95Style(QProxyStyle):
    def __init__(self):
        super().__init__("Windows")

    def pixelMetric(self, metric, option=None, widget=None):
        if metric in (QStyle.PM_IndicatorWidth, QStyle.PM_IndicatorHeight, QStyle.PM_ExclusiveIndicatorWidth, QStyle.PM_ExclusiveIndicatorHeight):
            return 13
        if metric == QStyle.PM_DefaultFrameWidth:
            return 2
        if metric == QStyle.PM_ProgressBarChunkWidth:
            return 10
        if metric in (QStyle.PM_ButtonShiftHorizontal, QStyle.PM_ButtonShiftVertical):
            return 1
        if metric == QStyle.PM_ButtonMargin:
            return 8
        return super().pixelMetric(metric, option, widget)

    def drawPrimitive(self, element, option, painter, widget=None):
        p = painter
        if element == QStyle.PE_PanelButtonCommand:
            r = QRect(option.rect)
            is_default = bool(isinstance(option, QStyleOptionButton) and option.features & QStyleOptionButton.DefaultButton)
            down = bool(option.state & (QStyle.State_Sunken | QStyle.State_On))
            p.save()
            if is_default:
                p.fillRect(r, DKSHADOW)
                r = r.adjusted(1, 1, -1, -1)
            if down:
                pressed(p, r)
            else:
                raised(p, r)
            p.restore()
            return
        if element == QStyle.PE_FrameDefaultButton:
            return
        if element in (QStyle.PE_FrameLineEdit, QStyle.PE_FrameTabWidget) or (element == QStyle.PE_Frame and option.state & QStyle.State_Sunken):
            p.save()
            sunken(p, QRect(option.rect))
            p.restore()
            return
        if element == QStyle.PE_PanelLineEdit:
            p.fillRect(option.rect, option.palette.brush(QPalette.Base))
            if getattr(option, "lineWidth", 1) > 0:
                self.drawPrimitive(QStyle.PE_FrameLineEdit, option, p, widget)
            return
        if element == QStyle.PE_IndicatorCheckBox:
            r = QRect(option.rect.x(), option.rect.y(), 13, 13)
            p.save()
            enabled = bool(option.state & QStyle.State_Enabled)
            sunken(p, r, WINDOW if enabled else FACE)
            if option.state & QStyle.State_On:
                draw_bitmap(p, r.x() + 3, r.y() + 3, CHECK, TEXT if enabled else GRAY_TEXT)
            p.restore()
            return
        if element == QStyle.PE_FrameFocusRect:
            p.save()
            pen = QPen(DKSHADOW, 1, Qt.DotLine)
            pen.setCosmetic(True)
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            p.drawRect(option.rect.adjusted(0, 0, -1, -1))
            p.restore()
            return
        super().drawPrimitive(element, option, painter, widget)

    def drawControl(self, element, option, painter, widget=None):
        p = painter
        if element == QStyle.CE_PushButtonBevel:
            self.drawPrimitive(QStyle.PE_PanelButtonCommand, option, p, widget)
            return
        if element == QStyle.CE_ProgressBarGroove:
            p.save()
            sunken(p, QRect(option.rect), FACE)
            p.restore()
            return
        if element == QStyle.CE_ProgressBarContents:
            r = QRect(option.rect)
            span = max(1, option.maximum - option.minimum)
            frac = max(0.0, min(1.0, (option.progress - option.minimum) / span))
            p.save()
            x0, y0 = r.x() + 1, r.y() + 1
            avail = r.width() - 2
            blocks = int(avail * frac / 10 + 0.0001)
            for i in range(blocks):
                p.fillRect(x0 + i * 10, y0, 8, r.height() - 2, NAVY)
            p.restore()
            return
        if element == QStyle.CE_ProgressBarLabel:
            return
        super().drawControl(element, option, painter, widget)

    def subElementRect(self, element, option, widget=None):
        if element == QStyle.SE_ProgressBarContents:
            return QRect(option.rect).adjusted(2, 2, -2, -2)
        if element == QStyle.SE_ProgressBarGroove:
            return QRect(option.rect)
        return super().subElementRect(element, option, widget)


def apply(app):
    app.setStyle(Win95Style())
    p = QPalette()
    for role, col in ((QPalette.Window, FACE), (QPalette.WindowText, TEXT), (QPalette.Base, WINDOW), (QPalette.AlternateBase, WINDOW),
                      (QPalette.Text, TEXT), (QPalette.Button, FACE), (QPalette.ButtonText, TEXT), (QPalette.BrightText, HILIGHT),
                      (QPalette.Light, HILIGHT), (QPalette.Midlight, LIGHT), (QPalette.Mid, SHADOW), (QPalette.Dark, SHADOW),
                      (QPalette.Shadow, DKSHADOW), (QPalette.Highlight, NAVY), (QPalette.HighlightedText, HILIGHT),
                      (QPalette.ToolTipBase, TOOLTIP), (QPalette.ToolTipText, TEXT), (QPalette.Link, QColor("#0000FF")),
                      (QPalette.LinkVisited, QColor("#800080"))):
        p.setColor(role, col)
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        p.setColor(QPalette.Disabled, role, GRAY_TEXT)
    p.setColor(QPalette.Disabled, QPalette.Highlight, SHADOW)
    app.setPalette(p)
    app.setFont(ui_font())


# ---------------------------------------------------------------------------------------------- icons
def gem_pixmap(size=16):
    """The emerald gem, flat, 16-colour palette, drawn on a 16 pixel grid."""
    pm = QPixmap(16, 16)
    pm.fill(Qt.transparent)
    q = QPainter(pm)
    q.setRenderHint(QPainter.Antialiasing, False)

    def poly(pts, fill):
        q.setPen(QPen(QColor("#000000"), 1))
        q.setBrush(QColor(fill))
        q.drawPolygon(QPolygon([QPoint(x, y) for x, y in pts]))
    poly([(4, 2), (11, 2), (14, 6), (8, 14), (1, 6)], "#008000")
    poly([(4, 2), (11, 2), (10, 6), (5, 6)], "#00FF00")
    poly([(1, 6), (5, 6), (8, 14)], "#008080")
    q.setPen(QPen(QColor("#FFFFFF"), 1))
    q.drawPoint(5, 3)
    q.drawPoint(6, 3)
    q.end()
    return pm if size == 16 else pm.scaled(size, size, Qt.IgnoreAspectRatio, Qt.FastTransformation)


def gem_icon():
    ic = QIcon()
    for s in (16, 32, 48):
        ic.addPixmap(gem_pixmap(s))
    return ic


def flat_icon(kind, size=32):
    """Flat message-box icons: question, info, warning, error, tick, cross."""
    pm = QPixmap(32, 32)
    pm.fill(Qt.transparent)
    q = QPainter(pm)
    q.setRenderHint(QPainter.Antialiasing, False)
    f = ui_font(1, True)
    f.setPixelSize(20)
    q.setFont(f)

    def poly(pts, fill):
        q.setPen(QPen(DKSHADOW, 1))
        q.setBrush(QColor(fill))
        q.drawPolygon(QPolygon([QPoint(x, y) for x, y in pts]))
    if kind in ("question", "info"):
        q.setPen(QPen(DKSHADOW, 1))
        q.setBrush(WINDOW)
        q.drawEllipse(QRect(2, 2, 27, 27))
        q.setPen(NAVY)
        q.drawText(QRect(2, 2, 28, 28), Qt.AlignCenter, "?" if kind == "question" else "i")
    elif kind == "warning":
        poly([(16, 2), (30, 29), (2, 29)], "#FFFF00")
        q.setPen(DKSHADOW)
        q.drawText(QRect(2, 8, 28, 22), Qt.AlignCenter, "!")
    elif kind == "error":
        q.setPen(QPen(DKSHADOW, 1))
        q.setBrush(QColor("#FF0000"))
        q.drawEllipse(QRect(2, 2, 27, 27))
        q.setPen(QPen(HILIGHT, 3))
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
