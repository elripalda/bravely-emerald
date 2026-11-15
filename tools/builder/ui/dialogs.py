"""Settings and Help dialogs (classic Windows look: plain widgets, etched group boxes, sunken fields)."""
import os
import threading

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFrame, QGridLayout, QGroupBox, QHBoxLayout,
                               QLabel, QLineEdit, QPlainTextEdit, QPushButton, QTextBrowser, QVBoxLayout)

from core.source import help_file
from . import classic

CPU_TEXT = [("low", "Low (recommended): half of the processor threads, low priority. The computer stays usable."),
            ("normal", "Normal: three quarters of the processor threads."),
            ("high", "High: all processor threads. The computer may be slow while it builds.")]


def sunken(widget):
    widget.setFrameShape(QFrame.StyledPanel)
    widget.setFrameShadow(QFrame.Sunken)
    return widget


class SettingsDialog(QDialog):
    checks_done = Signal(list)

    def __init__(self, main):
        super().__init__(main)
        self.main = main
        self.setWindowTitle("Settings")
        self.setMinimumWidth(620)
        lay = QVBoxLayout(self)

        g = QGroupBox("Install folder")
        gl = QHBoxLayout(g)
        self.dir_edit = QLineEdit(main.install_dir())
        self.dir_edit.setAccessibleName("Install folder")
        b = QPushButton("Browse...")
        b.clicked.connect(self.pick_dir)
        gl.addWidget(self.dir_edit, 1)
        gl.addWidget(b)
        lay.addWidget(g)

        g = QGroupBox("CPU usage")
        gl = QVBoxLayout(g)
        self.cpu = QComboBox()
        for key, text in CPU_TEXT:
            self.cpu.addItem(text, key)
        self.cpu.setCurrentIndex(max(0, [k for k, _ in CPU_TEXT].index(main.cpu_level())))
        self.cpu.setAccessibleName("CPU usage")
        gl.addWidget(self.cpu)
        lay.addWidget(g)

        g = QGroupBox("Source files")
        gl = QGridLayout(g)
        self.src_lbl = QLabel(main.source.describe())
        self.src_lbl.setWordWrap(True)
        self.src_lbl.setTextInteractionFlags(Qt.TextSelectableByMouse)
        gl.addWidget(self.src_lbl, 0, 0, 1, 3)
        gl.addWidget(QLabel("Work folder:"), 1, 0)
        self.work_edit = QLineEdit(main.source.work)
        self.work_edit.setReadOnly(True)
        gl.addWidget(self.work_edit, 1, 1)
        wb = QPushButton("Change...")
        wb.clicked.connect(lambda: (main.choose_work(), self.refresh_source()))
        gl.addWidget(wb, 1, 2)
        own = QPushButton("Use my own checkout...")
        own.clicked.connect(lambda: (main.choose_own(), self.refresh_source()))
        self.bundled = QPushButton("Use the bundled source")
        self.bundled.clicked.connect(lambda: (main.use_bundled(), self.refresh_source()))
        re = QPushButton("Re-extract bundled source")
        re.clicked.connect(lambda: (main.prepare_source(force=True), self.refresh_source()))
        row = QHBoxLayout()
        row.addWidget(own)
        row.addWidget(self.bundled)
        row.addWidget(re)
        gl.addLayout(row, 2, 0, 1, 3)
        note = QLabel("The program unpacks its own copy of the project into the work folder and builds there. "
                      "A checkout of yours is only read, never changed.")
        note.setWordWrap(True)
        gl.addWidget(note, 3, 0, 1, 3)
        lay.addWidget(g)

        self.linux_box = None
        if main.builder.host["kind"] in ("steamos", "linux"):
            g = QGroupBox("Linux options")
            gl = QGridLayout(g)
            self.container = QLineEdit(main.setting("container", "emerald-build"))
            self.run_test = QCheckBox("Run the 12 second start-up test at the end (needs the desktop)")
            self.run_test.setChecked(bool(main.setting("run_test", False)))
            gl.addWidget(QLabel("Build container name:"), 0, 0)
            gl.addWidget(self.container, 0, 1)
            gl.addWidget(self.run_test, 1, 0, 1, 2)
            lay.addWidget(g)
            self.linux_box = g

        g = QGroupBox("This computer")
        gl = QVBoxLayout(g)
        self.check_out = QPlainTextEdit()
        self.check_out.setReadOnly(True)
        self.check_out.setFont(classic.mono_font())
        self.check_out.setMaximumHeight(130)
        self.check_btn = QPushButton("Check now")
        self.check_btn.clicked.connect(self.run_check)
        self.checks_done.connect(self.show_checks)
        gl.addWidget(self.check_out)
        gl.addWidget(self.check_btn, 0, Qt.AlignLeft)
        gl.addWidget(QLabel("The build log of every build is saved in:"))
        self.log_where = QLineEdit(main.log_folder())
        self.log_where.setReadOnly(True)
        gl.addWidget(self.log_where)
        lay.addWidget(g)

        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        lay.addWidget(bb)
        self.refresh_source()
        if main.builder.last_checks:
            self.show_checks(main.builder.last_checks)

    def refresh_source(self):
        m = self.main
        self.src_lbl.setText(m.source.describe())
        self.work_edit.setText(m.source.work)
        self.bundled.setEnabled(bool(m.source.own) and m.source.manifest is not None)

    def pick_dir(self):
        d = QFileDialog.getExistingDirectory(self, "Install folder", self.dir_edit.text() or os.path.expanduser("~"))
        if d:
            self.dir_edit.setText(os.path.normpath(d))

    def run_check(self):
        if not self.main.builder.repo:
            self.check_out.setPlainText("The source is not ready yet.")
            return
        self.check_btn.setEnabled(False)
        self.check_out.setPlainText("Checking this computer...")
        threading.Thread(target=lambda: self.checks_done.emit(self.main.builder.checks()), daemon=True).start()

    def show_checks(self, items):
        mark = {"ok": "[ok]   ", "warn": "[todo] ", "bad": "[STOP] ", "info": "[info] "}
        self.check_out.setPlainText("\n".join(mark.get(i["status"], "") + i["label"] + ": " + i["detail"] for i in items))
        self.check_btn.setEnabled(True)

    def values(self):
        v = {"dir": self.dir_edit.text().strip(), "cpu": self.cpu.currentData()}
        if self.linux_box is not None:
            v["container"] = self.container.text().strip() or "emerald-build"
            v["run_test"] = self.run_test.isChecked()
        return v


class HelpDialog(QDialog):
    def __init__(self, parent, find=None):
        super().__init__(parent)
        self.setWindowTitle("Bravely Emerald Builder Help")
        self.resize(680, 560)
        lay = QVBoxLayout(self)
        self.view = QTextBrowser()
        self.view.setOpenLinks(False)
        path = help_file()
        if path:
            with open(path, encoding="utf-8") as f:
                self.view.setMarkdown(f.read())
        else:
            self.view.setPlainText("The help file is missing from this copy of the program.")
        lay.addWidget(self.view)
        close = QPushButton("Close")
        close.setDefault(True)
        close.clicked.connect(self.accept)
        row = QHBoxLayout()
        row.addStretch(1)
        row.addWidget(close)
        lay.addLayout(row)
        if find:
            self.view.find(find)
