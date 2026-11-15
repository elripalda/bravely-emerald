"""The one-screen window: choose the ROM, read the notice, press Build. Then a progress view and a result view.
Windows 95 look (see classic.py). Everything else is in the Settings dialog and the Help page."""
import os
import threading
import time

from PySide6.QtCore import QObject, Qt, QTimer, Signal
from PySide6.QtGui import QAction, QGuiApplication, QKeySequence, QTextCursor
from PySide6.QtWidgets import (QFileDialog, QGroupBox, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit,
                               QProgressBar, QPushButton, QStackedWidget, QVBoxLayout, QWidget)

from core import Builder, LEGAL_NOTE, load_settings, save_settings, validate_rom
from core.repo import is_repo
from core.source import Source, SourceError
from core.validate import cpu_profile, fmt_secs
from . import classic
from .dialogs import HelpDialog, SettingsDialog, sunken

TITLE = "Bravely Emerald Builder"
LANG = {"E": "USA", "D": "German", "F": "French", "I": "Italian", "S": "Spanish", "J": "Japan", "P": "Europe"}

# what the user reads while a stage runs
STEP_TEXT = {"prereq": "Checking this computer...", "install": "Please wait while the build tools are installed...",
             "container": "Please wait while the build environment is prepared...", "files": "Getting the game files...",
             "tools": "Preparing the build tools...", "assets": "Converting the game data...",
             "deps": "Building the game libraries...", "compile": "Compiling the game...", "link": "Compiling the game (last step)...",
             "package": "Packaging the game...", "test": "Testing that the game starts..."}
STEP_SHORT = {"prereq": "Checking this computer", "install": "Installing build tools", "container": "Preparing the build environment",
              "files": "Getting game files", "tools": "Preparing build tools", "assets": "Converting game data",
              "deps": "Building libraries", "compile": "Compiling", "link": "Compiling", "package": "Packaging", "test": "Testing"}
WEIGHT = {"prereq": 1, "install": 8, "container": 8, "files": 8, "tools": 3, "assets": 12, "deps": 8, "compile": 50, "link": 3,
          "package": 3, "test": 1}


class Bridge(QObject):
    """Carries results from worker threads to the GUI thread (queued signals)."""
    event = Signal(dict)
    checks_done = Signal(list)
    source_ready = Signal(str, str)         # repository path, error text
    plan_ready = Signal(object, str)        # (label, stages, steps, meta) or None, error text


def notice_html(kind):
    if kind == "windows":
        tools = ("the build tools (MSYS2, about 85 MB, and the compiler and library packages, several hundred MB, if they are not "
                 "installed yet), the SDL2 libraries (a few MB), and the game files from the public pret/pokeemerald project")
    else:
        tools = ("a build environment (a container of about 400 MB, with the compiler packages), the SDL2 libraries (a few MB), and "
                 "the game files from the public pret/pokeemerald project")
    return ("<ul>"
            "<li>You need an <b>internet connection</b>. The build downloads %s.</li>"
            "<li>The first build takes about <b>30 to 60 minutes</b>.</li>"
            "<li>About <b>15 GB</b> of free disk space are needed.</li>"
            "<li>Your ROM is only <b>read to check it</b>. It is never copied or uploaded.</li>"
            "<li>The finished build is <b>for your own use only</b>. Please do not share it.</li>"
            "</ul>" % tools)


def info_box(parent, icon, title, text, buttons, default=None):
    box = QMessageBox(parent)
    box.setWindowTitle(title)
    box.setIconPixmap(classic.flat_icon(icon))
    box.setText(text)
    box.setStandardButtons(buttons)
    if default is not None:
        box.setDefaultButton(default)
    return box


def bold(label, points):
    f = label.font()
    f.setBold(True)
    f.setPointSize(points)
    label.setFont(f)


class MainWindow(QMainWindow):
    def __init__(self, repo=None, dry_run=False):
        super().__init__()
        self.setWindowTitle(TITLE)
        self.setWindowIcon(classic.gem_icon())
        self.bridge = Bridge()
        self.bridge.event.connect(self.on_event)
        self.bridge.checks_done.connect(self.on_checks)
        self.bridge.source_ready.connect(self.on_source_ready)
        self.bridge.plan_ready.connect(self.on_plan)
        self.settings = load_settings()
        self.source = Source(force_repo=repo)
        self.source_busy = True
        self.blockers = []
        self.source_error = ""
        self.builder = Builder("", dry_run)
        self.task = None
        self.res = {}
        self.rom_ok = False
        self.stage_order = []
        self.stage_done = set()
        self.cur_stage = None
        self.pct = 0
        self.planning = False
        self.log_buf = []
        self.last_opts = None
        self.no_modal = False          # tests: do not block in message boxes
        self.last_box = None

        self._menus()
        self.stack = QStackedWidget()
        self.setCentralWidget(self.stack)
        self.stack.addWidget(self._idle_view())
        self.stack.addWidget(self._progress_view())
        self.stack.addWidget(self._ready_view())
        self.step_panel = QLabel("Ready")
        sunken(self.step_panel)
        self.statusBar().addWidget(self.step_panel, 1)
        self.resize(780, 580)
        self.log_timer = QTimer(self)
        self.log_timer.timeout.connect(self.flush_log)
        self.log_timer.start(150)
        self.clock = QTimer(self)
        self.clock.timeout.connect(self.tick)
        self.clock.start(1000)
        self.rom_edit.setText(self.settings.get("rom", ""))
        if self.rom_edit.text():
            self.check_rom()
        self.update_state()
        QTimer.singleShot(0, self.prepare_source)

    # ------------------------------------------------------------------ menus
    def _menus(self):
        mb = self.menuBar()
        fm = mb.addMenu("&File")
        a = QAction("&Settings...", self)
        a.triggered.connect(self.open_settings)
        fm.addAction(a)
        fm.addSeparator()
        a = QAction("E&xit", self)
        a.triggered.connect(self.close)
        fm.addAction(a)
        hm = mb.addMenu("&Help")
        a = QAction("&Contents", self)
        a.setShortcut(QKeySequence.HelpContents)
        a.triggered.connect(lambda: self.open_help())
        hm.addAction(a)
        hm.addSeparator()
        a = QAction("&About", self)
        a.triggered.connect(self.about)
        hm.addAction(a)

    # ------------------------------------------------------------------ views
    def _idle_view(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(14, 12, 14, 10)
        lay.setSpacing(10)
        head = QHBoxLayout()
        icon = QLabel()
        icon.setPixmap(classic.gem_icon().pixmap(32, 32))
        head.addWidget(icon, 0, Qt.AlignTop)
        col = QVBoxLayout()
        t = QLabel(TITLE)
        bold(t, 13)
        col.addWidget(t)
        line = QLabel("This program builds Pokemon Bravely Emerald on this computer from your own Pokemon Emerald ROM.")
        line.setWordWrap(True)
        col.addWidget(line)
        head.addLayout(col, 1)
        lay.addLayout(head)

        g = QGroupBox("Choose your ROM")
        gl = QVBoxLayout(g)
        row = QHBoxLayout()
        self.rom_edit = QLineEdit()
        self.rom_edit.setPlaceholderText("Click Browse to find your Pokemon Emerald .gba file")
        self.rom_edit.setAccessibleName("ROM file")
        self.rom_edit.editingFinished.connect(self.check_rom)
        browse = QPushButton("&Browse...")
        browse.clicked.connect(self.browse_rom)
        row.addWidget(self.rom_edit, 1)
        row.addWidget(browse)
        gl.addLayout(row)
        res = QHBoxLayout()
        self.rom_icon = QLabel()
        self.rom_icon.setFixedSize(16, 16)
        self.rom_text = QLabel("")
        self.rom_text.setWordWrap(True)
        res.addWidget(self.rom_icon, 0, Qt.AlignTop)
        res.addWidget(self.rom_text, 1)
        gl.addLayout(res)
        lay.addWidget(g)

        g = QGroupBox("Before you start")
        gl = QVBoxLayout(g)
        self.notice = QLabel(notice_html(self.builder.host["kind"]))
        self.notice.setWordWrap(True)
        self.notice.setTextFormat(Qt.RichText)
        gl.addWidget(self.notice)
        lay.addWidget(g)

        brow = QHBoxLayout()
        self.problem_icon = QLabel()
        self.problem_icon.setFixedSize(16, 16)
        self.problem_text = QLabel("")
        self.problem_text.setWordWrap(True)
        brow.addWidget(self.problem_icon, 0, Qt.AlignTop)
        brow.addWidget(self.problem_text, 1)
        lay.addLayout(brow)
        lay.addStretch(1)
        self.build_btn = QPushButton("&Build Bravely Emerald")
        bold(self.build_btn, 12)
        self.build_btn.setMinimumHeight(52)
        self.build_btn.setDefault(True)
        self.build_btn.clicked.connect(self.confirm_and_build)
        lay.addWidget(self.build_btn)
        return w

    def _progress_view(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(14, 12, 14, 10)
        lay.setSpacing(8)
        self.prog_title = QLabel("Building Bravely Emerald")
        bold(self.prog_title, 13)
        lay.addWidget(self.prog_title)
        self.prog_step = QLabel("")
        self.prog_step.setWordWrap(True)
        lay.addWidget(self.prog_step)
        row = QHBoxLayout()
        self.bar = QProgressBar()
        self.bar.setRange(0, 100)
        self.bar.setTextVisible(False)
        self.bar.setFixedHeight(22)
        self.bar.setAccessibleName("Progress")
        self.pct_lbl = QLabel("0%")
        self.pct_lbl.setMinimumWidth(36)
        row.addWidget(self.bar, 1)
        row.addWidget(self.pct_lbl)
        lay.addLayout(row)
        self.elapsed = QLabel("Time elapsed: 0:00:00")
        lay.addWidget(self.elapsed)
        lay.addWidget(QLabel("This takes a while. You can leave this window open and do something else."))

        self.fail_box = QGroupBox("Something went wrong")
        fl = QHBoxLayout(self.fail_box)
        ic = QLabel()
        ic.setPixmap(classic.flat_icon("warning"))
        fl.addWidget(ic, 0, Qt.AlignTop)
        self.fail_text = QLabel("")
        self.fail_text.setWordWrap(True)
        self.fail_text.setTextFormat(Qt.RichText)
        self.fail_text.setTextInteractionFlags(Qt.TextSelectableByMouse)
        fl.addWidget(self.fail_text, 1)
        self.fail_box.hide()
        lay.addWidget(self.fail_box)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setFont(classic.mono_font())
        self.log.setMaximumBlockCount(60000)
        self.log.setAccessibleName("Details")
        self.log.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.log.hide()
        lay.addWidget(self.log, 10)
        lay.addStretch(1)

        row = QHBoxLayout()
        self.details_btn = QPushButton("Show &details >>")
        self.details_btn.setCheckable(True)
        self.details_btn.toggled.connect(self.toggle_details)
        self.copy_btn = QPushButton("Copy &log")
        self.copy_btn.clicked.connect(self.copy_log)
        self.retry_btn = QPushButton("&Try again")
        self.retry_btn.clicked.connect(self.try_again)
        self.back_btn = QPushButton("&Back")
        self.back_btn.clicked.connect(lambda: self.stack.setCurrentIndex(0))
        self.cancel_btn = QPushButton("&Cancel")
        self.cancel_btn.clicked.connect(self.cancel)
        row.addWidget(self.details_btn)
        row.addWidget(self.copy_btn)
        row.addStretch(1)
        row.addWidget(self.retry_btn)
        row.addWidget(self.back_btn)
        row.addWidget(self.cancel_btn)
        lay.addLayout(row)
        self.retry_btn.hide()
        self.back_btn.hide()
        return w

    def _ready_view(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(14, 12, 14, 10)
        lay.setSpacing(10)
        head = QHBoxLayout()
        ic = QLabel()
        ic.setPixmap(classic.flat_icon("info"))
        head.addWidget(ic, 0, Qt.AlignTop)
        self.ready_title = QLabel("Your build is ready.")
        bold(self.ready_title, 13)
        head.addWidget(self.ready_title, 1)
        lay.addLayout(head)
        lay.addWidget(QLabel("The game is in this folder:"))
        self.ready_path = QLineEdit()
        self.ready_path.setReadOnly(True)
        lay.addWidget(self.ready_path)
        row = QHBoxLayout()
        self.open_btn = QPushButton("&Open folder")
        self.open_btn.clicked.connect(lambda: self.open_result("folder"))
        self.play_btn = QPushButton("&Play")
        self.play_btn.setDefault(True)
        self.play_btn.clicked.connect(lambda: self.open_result("launch"))
        row.addWidget(self.open_btn)
        row.addWidget(self.play_btn)
        row.addStretch(1)
        lay.addLayout(row)
        self.next_lbl = QLabel("")
        self.next_lbl.setWordWrap(True)
        lay.addWidget(self.next_lbl)
        self.help_btn = QPushButton("&Help...")
        self.help_btn.clicked.connect(lambda: self.open_help("Add it to Steam"))
        lay.addWidget(self.help_btn, 0, Qt.AlignLeft)
        lay.addStretch(1)
        legal = QLabel("This build contains Nintendo's game data. It is for your own use only. Please do not share it.")
        legal.setWordWrap(True)
        lay.addWidget(legal)
        again = QPushButton("Build &again")
        again.clicked.connect(lambda: self.stack.setCurrentIndex(0))
        lay.addWidget(again, 0, Qt.AlignRight)
        return w

    def run_box(self, box):
        if self.no_modal:
            self.last_box = box
            return QMessageBox.Cancel
        return box.exec()

    # ------------------------------------------------------------------ settings access
    def setting(self, key, default=None):
        return load_settings().get(key, default)

    def cpu_level(self):
        v = self.setting("cpu", "low")
        return v if v in ("low", "normal", "high") else "low"

    def install_dir(self):
        return self.setting("dir") or self.builder.default_dir()

    def log_folder(self):
        if self.builder.host["kind"] == "windows":
            return os.path.join(self.builder.repo or self.source.work, "build-win", "builder-logs")
        return os.path.join(self.install_dir(), "builder-logs")

    def open_settings(self):
        if self.task and self.task.state == "running":
            self.run_box(info_box(self, "info", "Settings", "Please wait until the build is finished.", QMessageBox.Ok))
            return
        dlg = SettingsDialog(self)
        if dlg.exec():
            save_settings(dlg.values())
            self.update_state()

    def open_help(self, find=None):
        HelpDialog(self, find).exec()

    def about(self):
        info_box(self, "info", "About " + TITLE, "%s\n\nBuilds Pokemon Bravely Emerald from your own ROM.\n%s\n\n%s" %
                 (TITLE, self.source.describe(), LEGAL_NOTE), QMessageBox.Ok).exec()

    # ------------------------------------------------------------------ source and checks (background, at start)
    def prepare_source(self, force=False):
        self.source_busy = True
        self.update_state()
        self.step_panel.setText("Preparing the program files...")

        def work():
            try:
                self.bridge.source_ready.emit(self.source.prepare(force), "")
            except (SourceError, OSError) as e:
                self.bridge.source_ready.emit("", str(e))
        threading.Thread(target=work, daemon=True).start()

    def on_source_ready(self, path, err):
        self.source_busy = False
        if err:
            self.builder.repo = ""
            self.step_panel.setText("The program files could not be prepared")
            self.source_error = err
            self.update_state()
            return
        self.source_error = ""
        self.builder.repo = os.path.abspath(path)
        self.step_panel.setText("Ready")
        self.update_state()
        self.run_checks()

    def run_checks(self):
        def work():
            try:
                res = self.builder.checks()
            except Exception as e:
                res = [{"id": "err", "label": "Check", "status": "bad", "detail": repr(e), "blocking": True}]
            self.bridge.checks_done.emit(res)
        threading.Thread(target=work, daemon=True).start()

    def on_checks(self, items):
        self.blockers = [c for c in items if c.get("blocking")]
        self.update_state()

    def choose_work(self):
        d = QFileDialog.getExistingDirectory(self, "Work folder for the program files", os.path.dirname(self.source.work) or os.path.expanduser("~"))
        if d:
            self.source.work = os.path.join(os.path.normpath(d), "source")
            save_settings({"source_dir": self.source.work})
            self.prepare_source()

    def choose_own(self):
        d = QFileDialog.getExistingDirectory(self, "Choose your own checkout of the project", os.path.expanduser("~"))
        if not d:
            return
        if not is_repo(d):
            self.run_box(info_box(self, "error", "Settings", "%s does not hold the build scripts (fetch_assets.sh, tools\\deck, tools\\windows)." % d, QMessageBox.Ok))
            return
        self.source.own = os.path.abspath(d)
        save_settings({"own_checkout": self.source.own})
        self.prepare_source()

    def use_bundled(self):
        self.source.own = ""
        save_settings({"own_checkout": ""})
        self.prepare_source()

    # ------------------------------------------------------------------ ROM
    def browse_rom(self):
        f, _ = QFileDialog.getOpenFileName(self, "Choose your Pokemon Emerald ROM", os.path.dirname(self.rom_edit.text()) or os.path.expanduser("~"),
                                           "GBA ROM (*.gba);;All files (*)")
        if f:
            self.rom_edit.setText(os.path.normpath(f))
            self.check_rom()

    def check_rom(self):
        p = self.rom_edit.text().strip()
        if not p:
            self.rom_ok = False
            self.rom_icon.clear()
            self.rom_text.setText("")
        else:
            v = validate_rom(p)
            self.rom_ok = v["ok"]
            self.rom_icon.setPixmap(classic.flat_icon("tick" if v["ok"] else "cross", 16))
            if v["ok"]:
                self.rom_text.setText("Pokemon Emerald (%s) found." % LANG.get(v["code"][3:4], v["code"]))
            else:
                self.rom_text.setText(v["message"])
        self.update_state()

    # ------------------------------------------------------------------ idle state
    def update_state(self):
        busy = bool(self.task and self.task.state == "running") or self.planning
        have_files = bool(self.builder.repo) and self.builder.have_assets()
        warn, info = "", ""
        if self.source_busy:
            info = "Please wait while the program files are prepared..."
        elif not self.builder.repo:
            warn = getattr(self, "source_error", "") or "The program files are not available. Open File, Settings to fix this."
        elif self.blockers:
            warn = "The build cannot start yet: " + "; ".join("%s. %s" % (c["label"], c["detail"]) for c in self.blockers)
            for c in self.blockers:
                if c.get("manual"):
                    warn += " Run this in a terminal: " + c["manual"]
        elif not have_files and not self.rom_ok:
            info = "Choose your Pokemon Emerald ROM to continue."
        elif have_files and not self.rom_text.text():
            info = "The game files from an earlier build were found, so the ROM is not needed again."
        self.problem_icon.setPixmap(classic.flat_icon("warning", 16)) if warn else self.problem_icon.clear()
        self.problem_text.setText(warn or info)
        ready = bool(self.builder.repo) and not self.source_busy and not warn and (have_files or self.rom_ok)
        self.build_btn.setEnabled(ready and not busy)

    # ------------------------------------------------------------------ build
    def build_opts(self):
        jobs, low = cpu_profile(self.cpu_level(), os.cpu_count() or 1)
        target = "windows" if self.builder.host["kind"] == "windows" else "linux32"
        return {"targets": [target], "rom": self.rom_edit.text().strip(), "dir": self.install_dir(), "jobs": jobs,
                "low_priority": low, "run_test": bool(self.setting("run_test", False)),
                "container": self.setting("container", "emerald-build")}

    def make_confirm_box(self):
        text = ("The build will now download and install what it needs, then build the game.\n\n"
                "It needs an internet connection and downloads the build tools, the SDL2 libraries and the game files from the public "
                "pret/pokeemerald project (several hundred MB in all, more if the tools are not installed yet).\n\n"
                "It takes about 30 to 60 minutes the first time and needs about 15 GB of free disk space.\n"
                "Your ROM is only read to check it. The finished build is for your own use only.\n\n"
                "Do you want to continue?")
        box = info_box(self, "question", TITLE, text, QMessageBox.Ok | QMessageBox.Cancel, QMessageBox.Ok)
        box.button(QMessageBox.Ok).setText("Continue")
        return box

    def confirm_and_build(self):
        if self.run_box(self.make_confirm_box()) != QMessageBox.Ok:
            return
        self.start_build()

    def start_build(self):
        opts = self.build_opts()
        self.last_opts = opts
        save_settings({"rom": opts["rom"]})
        self.planning = True
        self.update_state()
        self.step_panel.setText("Preparing the build...")

        def work():
            try:
                self.bridge.plan_ready.emit(self.builder.plan_build(opts), "")
            except ValueError as e:
                self.bridge.plan_ready.emit(None, str(e))
            except Exception as e:
                self.bridge.plan_ready.emit(None, repr(e))
        threading.Thread(target=work, daemon=True).start()

    def on_plan(self, plan, err):
        self.planning = False
        if plan is None:
            self.step_panel.setText("Ready")
            self.update_state()
            self.run_box(info_box(self, "error", TITLE, err, QMessageBox.Ok))
            return
        label, stages, steps, meta = plan
        meta["container"] = (self.last_opts or {}).get("container", "emerald-build")
        try:
            self.task = self.builder.start(label, stages, steps, meta, "build", lambda ev: self.bridge.event.emit(ev))
        except ValueError as e:
            self.run_box(info_box(self, "error", TITLE, str(e), QMessageBox.Ok))
            return
        self.stage_order = list(stages)
        self.stage_done = set()
        self.cur_stage = None
        self.pct = 0
        self.log.clear()
        self.log_buf = []
        self.fail_box.hide()
        self.prog_title.setText("Building Bravely Emerald")
        self.prog_step.setText(STEP_TEXT["prereq"])
        self.set_progress()
        for b, on in ((self.cancel_btn, True), (self.retry_btn, False), (self.back_btn, False)):
            b.setVisible(on)
        self.cancel_btn.setEnabled(True)
        self.stack.setCurrentIndex(1)
        self.update_state()

    def set_progress(self):
        total = sum(WEIGHT.get(s, 1) for s in self.stage_order) or 1
        done = sum(WEIGHT.get(s, 1) for s in self.stage_done)
        cur = 0.0
        if self.cur_stage in self.stage_order:
            w = WEIGHT.get(self.cur_stage, 1)
            cur = w * (self.pct / 100.0 if self.cur_stage == "compile" else 0.3)
        v = int(min(100, 100.0 * (done + cur) / total))
        self.bar.setValue(v)
        self.pct_lbl.setText("%d%%" % v)

    def on_event(self, ev):
        t = ev["type"]
        if t == "log":
            self.log_buf.append(ev["text"])
        elif t == "stage":
            s = ev["stage"]
            if s in self.stage_order:
                self.stage_done.update(self.stage_order[:self.stage_order.index(s)])
                self.cur_stage = s
                self.pct = 0
                self.prog_step.setText(STEP_TEXT.get(s, s))
                self.step_panel.setText(STEP_SHORT.get(s, s))
                self.set_progress()
        elif t == "progress":
            self.pct = ev["pct"]
            self.set_progress()
        elif t == "state":
            self.flush_log()
            self.finish_task(ev)

    def flush_log(self):
        if not self.log_buf:
            return
        sb = self.log.verticalScrollBar()
        at_end = sb.value() >= sb.maximum() - 4
        self.log.appendPlainText("\n".join(self.log_buf))
        self.log_buf = []
        if at_end:
            self.log.moveCursor(QTextCursor.End)

    def finish_task(self, ev):
        st = ev["state"]
        self.cancel_btn.setVisible(False)
        if st == "done":
            self.stage_done.update(self.stage_order)
            self.set_progress()
            self.show_ready(self.task.result or {})
        elif st == "cancelled":
            self.step_panel.setText("Cancelled")
            self.stack.setCurrentIndex(0)
            self.update_state()
            self.run_box(info_box(self, "info", TITLE, "The build was cancelled. Finished steps are kept, so the next build continues where this one stopped.",
                                  QMessageBox.Ok))
        else:
            self.show_failure(ev)
        self.update_state()

    def show_failure(self, ev):
        hints = ev.get("hints") or []
        html = "<b>A step did not finish.</b>"
        plain = "A step did not finish."
        for h in hints:
            html += "<br><br><b>%s</b><br>%s" % (h["title"], h["fix"])
            plain += "\n\n%s\n%s" % (h["title"], h["fix"])
        if not hints:
            extra = "No known fix matches this problem. Press Show details and read the first error lines."
            html += "<br><br>" + extra
            plain += "\n\n" + extra
        self.fail_text.setText(html)
        self.fail_box.show()
        self.prog_title.setText("The build stopped")
        self.step_panel.setText("Stopped")
        for b, on in ((self.retry_btn, True), (self.back_btn, True), (self.cancel_btn, False)):
            b.setVisible(on)
        box = info_box(self, "warning", TITLE, plain, QMessageBox.Retry | QMessageBox.Cancel, QMessageBox.Retry)
        if self.run_box(box) == QMessageBox.Retry:
            self.try_again()

    def try_again(self):
        self.start_build()

    def toggle_details(self, on):
        self.log.setVisible(on)
        self.details_btn.setText("<< Hide &details" if on else "Show &details >>")

    def copy_log(self):
        self.flush_log()
        QGuiApplication.clipboard().setText(self.log.toPlainText())
        self.step_panel.setText("The log was copied to the clipboard")

    def tick(self):
        t = self.task
        if t and t.state == "running":
            self.elapsed.setText("Time elapsed: " + fmt_secs(time.time() - t.started))

    def cancel(self):
        if not (self.task and self.task.state == "running"):
            return
        box = info_box(self, "question", TITLE, "Do you want to cancel the build?\n\nFinished steps are kept.", QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if self.run_box(box) == QMessageBox.Yes:
            self.cancel_btn.setEnabled(False)
            self.builder.cancel()

    # ------------------------------------------------------------------ ready
    def show_ready(self, res):
        self.res = res
        self.ready_path.setText(res.get("folder") or "")
        self.ready_title.setText("Dry run finished. Nothing was built." if self.builder.dry_run else "Your build is ready.")
        self.open_btn.setEnabled(bool(res.get("ok")))
        self.play_btn.setEnabled(bool(res.get("launch")))
        if res.get("target") == "linux32":
            self.next_lbl.setText("Next step: to play from Steam Game Mode, add the launcher to Steam. Press Help for the steps.")
            self.help_btn.setVisible(True)
        else:
            self.next_lbl.setText("Next step: press Play, or double-click run.bat in that folder.")
            self.help_btn.setVisible(False)
        self.step_panel.setText("Done")
        self.stack.setCurrentIndex(2)

    def open_result(self, what):
        try:
            self.builder.open_result(what)
        except Exception as e:
            self.run_box(info_box(self, "error", TITLE, str(e), QMessageBox.Ok))

    # ------------------------------------------------------------------ closing
    def closeEvent(self, e):
        if self.task and self.task.state == "running":
            box = info_box(self, "question", TITLE, "A build is running. Cancel it and exit?", QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if self.run_box(box) != QMessageBox.Yes:
                e.ignore()
                return
            self.builder.cancel()
            for _ in range(50):
                if self.task.state != "running":
                    break
                time.sleep(0.1)
        e.accept()
