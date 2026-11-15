"""Dark theme with an emerald accent. Pure Qt style sheet: no external fonts or images, works offline."""
from PySide6.QtGui import QColor, QFont, QFontDatabase, QPalette
from PySide6.QtWidgets import QApplication

ACCENT = "#2fcf8a"
ACCENT_DARK = "#1f9d68"
BG = "#14181c"
PANEL = "#1c2228"
PANEL2 = "#242c33"
TEXT = "#e6edf3"
MUTED = "#9aa7b2"
OK = "#2fcf8a"
WARN = "#e5b84b"
BAD = "#ff6b6b"

STYLE = f"""
* {{ font-size: 10pt; }}
QMainWindow, QWidget#root, QDialog {{ background: {BG}; color: {TEXT}; }}
QWidget {{ color: {TEXT}; }}
QLabel#title {{ font-size: 20pt; font-weight: 700; color: {ACCENT}; }}
QLabel#subtitle {{ color: {MUTED}; }}
QLabel#muted, QLabel[muted="true"] {{ color: {MUTED}; }}
QLabel#h2 {{ font-size: 12pt; font-weight: 600; color: {TEXT}; }}
QFrame#card, QGroupBox {{ background: {PANEL}; border: 1px solid #2c353d; border-radius: 8px; }}
QGroupBox {{ margin-top: 14px; padding: 12px 10px 8px 10px; font-weight: 600; }}
QGroupBox::title {{ subcontrol-origin: margin; left: 12px; padding: 0 6px; color: {ACCENT}; }}
QLineEdit, QSpinBox, QPlainTextEdit, QTextBrowser, QComboBox {{
    background: #10151a; border: 1px solid #33404a; border-radius: 6px; padding: 5px 7px; color: {TEXT};
    selection-background-color: {ACCENT_DARK}; }}
QPlainTextEdit {{ font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 9pt; }}
QLineEdit:focus, QSpinBox:focus, QPlainTextEdit:focus {{ border: 1px solid {ACCENT}; }}
QLineEdit:disabled, QSpinBox:disabled {{ color: #66727c; }}
QPushButton {{ background: {PANEL2}; border: 1px solid #3a4651; border-radius: 6px; padding: 6px 14px; }}
QPushButton:hover {{ border-color: {ACCENT}; }}
QPushButton:focus {{ border: 2px solid {ACCENT}; padding: 5px 13px; }}
QPushButton:disabled {{ color: #66727c; background: #1a2025; border-color: #2a3238; }}
QPushButton#primary {{ background: {ACCENT_DARK}; border-color: {ACCENT}; color: white; font-weight: 700; padding: 8px 22px; }}
QPushButton#primary:hover {{ background: {ACCENT}; color: #06140d; }}
QPushButton#primary:disabled {{ background: #1f3a30; color: #6d8a7c; border-color: #2a4a3d; }}
QPushButton#danger {{ border-color: {BAD}; color: {BAD}; }}
QCheckBox, QRadioButton {{ spacing: 8px; }}
QCheckBox:focus, QRadioButton:focus {{ color: {ACCENT}; }}
QCheckBox::indicator, QRadioButton::indicator {{ width: 16px; height: 16px; }}
QTabWidget::pane {{ border: 0; }}
QTabBar::tab {{ background: transparent; color: {MUTED}; padding: 9px 20px; border-bottom: 3px solid transparent; font-weight: 600; }}
QTabBar::tab:selected {{ color: {ACCENT}; border-bottom: 3px solid {ACCENT}; }}
QTabBar::tab:disabled {{ color: #4a555e; }}
QTabBar::tab:focus {{ color: white; }}
QProgressBar {{ background: #10151a; border: 1px solid #33404a; border-radius: 6px; height: 16px; text-align: center; color: {TEXT}; }}
QProgressBar::chunk {{ background: {ACCENT_DARK}; border-radius: 5px; }}
QScrollArea {{ border: 0; background: transparent; }}
QScrollBar:vertical {{ background: {BG}; width: 12px; }}
QScrollBar::handle:vertical {{ background: #3a4651; border-radius: 5px; min-height: 24px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
QToolTip {{ background: {PANEL2}; color: {TEXT}; border: 1px solid {ACCENT}; }}
"""


def apply(app: QApplication):
    app.setStyle("Fusion")
    have = set(QFontDatabase.families())
    for fam in ("Segoe UI", "Noto Sans", "DejaVu Sans", "Ubuntu", "Arial", "Verdana"):
        if fam in have:
            app.setFont(QFont(fam, 10))
            break
    p = QPalette()
    p.setColor(QPalette.Window, QColor(BG))
    p.setColor(QPalette.WindowText, QColor(TEXT))
    p.setColor(QPalette.Base, QColor("#10151a"))
    p.setColor(QPalette.AlternateBase, QColor(PANEL))
    p.setColor(QPalette.Text, QColor(TEXT))
    p.setColor(QPalette.Button, QColor(PANEL2))
    p.setColor(QPalette.ButtonText, QColor(TEXT))
    p.setColor(QPalette.Highlight, QColor(ACCENT_DARK))
    p.setColor(QPalette.HighlightedText, QColor("white"))
    p.setColor(QPalette.PlaceholderText, QColor("#66727c"))
    app.setPalette(p)
    app.setStyleSheet(STYLE)
