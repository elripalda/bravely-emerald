"""Main window: Setup (repository, prerequisites, ROM, options), Build (stages, live log), Result."""
import os
import sys
import threading
import time

from PySide6.QtCore import QObject, Qt, QTimer, Signal, QUrl
from PySide6.QtGui import QDesktopServices, QTextCursor
from PySide6.QtWidgets import (QCheckBox, QFileDialog, QFrame, QGridLayout, QGroupBox, QHBoxLayout, QLabel,
                               QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit, QProgressBar, QPushButton,
                               QRadioButton, QScrollArea, QSpinBox, QTabWidget, QTextBrowser, QVBoxLayout, QWidget)

from core import Builder, LEGAL_NOTE, REPO_URL, load_settings, save_settings, validate_rom
from core.repo import guess_repo, is_repo
from core.validate import fmt_secs
from . import theme

STAGE_LABELS = {"prereq": "Prerequisites", "container": "Container", "files": "Game files", "tools": "Tools", "assets": "Assets",
                "deps": "SDL2", "compile": "Compile", "link": "Link", "package": "Package", "test": "Start-up test",
                "clone": "Clone", "download": "Download", "install": "Install", "packages": "Packages"}
STATUS_STYLE = {"ok": ("OK", theme.OK), "warn": ("TODO", theme.WARN), "bad": ("MISSING", theme.BAD), "info": ("INFO", theme.MUTED)}


class Bridge(QObject):
    """Carries results from worker threads to the GUI thread (queued signals)."""
    event = Signal(dict)
    checks_done = Signal(list)
    msys_info = Signal(dict)


def hline():
    f = QFrame()
    f.setFrameShape(QFrame.HLine)
    f.setStyleSheet("color:#2c353d")
    return f


def label(text, name=None, wrap=True, muted=False):
    lb = QLabel(text)
    lb.setWordWrap(wrap)
    if name:
        lb.setObjectName(name)
    if muted:
        lb.setProperty("muted", True)
    lb.setTextInteractionFlags(Qt.TextSelectableByMouse)
    return lb


class PrereqRow(QFrame):
    def __init__(self, item, on_install):
        super().__init__()
        self.setObjectName("card")
        lay = QHBoxLayout(self)
        lay.setContentsMargins(10, 8, 10, 8)
        text, color = STATUS_STYLE.get(item["status"], ("?", theme.MUTED))
        badge = QLabel(text)
        badge.setFixedWidth(78)
        badge.setAlignment(Qt.AlignCenter)
        badge.setStyleSheet("background:%s;color:#071410;border-radius:4px;font-weight:700;padding:2px;" % color)
        lay.addWidget(badge, 0, Qt.AlignTop)
        col = QVBoxLayout()
        title = QLabel(item["label"])
        title.setStyleSheet("font-weight:600")
        col.addWidget(title)
        col.addWidget(label(item["detail"], muted=True))
        if item.get("manual"):
            ed = QLineEdit(item["manual"])
            ed.setReadOnly(True)
            ed.setAccessibleName("Command to run in a terminal for " + item["label"])
            col.addWidget(ed)
        lay.addLayout(col, 1)
        if item.get("action"):
            btn = QPushButton("&Install")
            btn.setAccessibleName("Install " + item["label"])
            btn.clicked.connect(lambda: on_install(item["action"]))
            lay.addWidget(btn, 0, Qt.AlignTop)
        self.setAccessibleName("%s: %s" % (item["label"], text))


class MainWindow(QMainWindow):
    def __init__(self, repo=None, dry_run=False):
        super().__init__()
        self.setWindowTitle("Bravely Emerald builder")
        self.resize(1000, 780)
        self.bridge = Bridge()
        self.bridge.event.connect(self.on_event)
        self.bridge.checks_done.connect(self.apply_checks)
        self.bridge.msys_info.connect(self.show_msys_consent)
        self.settings = load_settings()
        self.builder = Builder(repo if repo is not None else guess_repo(), dry_run)
        self.checks = []
        self.task = None
        self.stage_labels = {}
        self.stage_state = {}
        self.stage_order = []
        self.log_buf = []
        self.pct = 0
        self.rom_ok = False
        self.task_kind = None
        self.res_result = {}
        self.log_timer = QTimer(self)
        self.log_timer.timeout.connect(self.flush_log)
        self.log_timer.start(120)
        self.clock = QTimer(self)
        self.clock.timeout.connect(self.tick)
        self.clock.start(1000)

        root = QWidget()
        root.setObjectName("root")
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(18, 14, 18, 12)
        outer.addLayout(self._header())
        self.tabs = QTabWidget()
        self.tabs.addTab(self._setup_page(), "&1  Setup")
        self.tabs.addTab(self._build_page(), "&2  Build")
        self.tabs.addTab(self._result_page(), "&3  Result")
        self.tabs.setTabEnabled(1, False)
        self.tabs.setTabEnabled(2, False)
        outer.addWidget(self.tabs, 1)
        self.status = QLabel("")
        self.status.setProperty("muted", True)
        outer.addWidget(self.status)
        self.load_settings_into_ui()
        self.update_repo_ui()
        QTimer.singleShot(0, self.refresh_checks)

    # ------------------------------------------------------------------ header
    def _header(self):
        lay = QVBoxLayout()
        t = QLabel("Bravely Emerald builder")
        t.setObjectName("title")
        t.setAccessibleName("Bravely Emerald builder")
        lay.addWidget(t)
        h = self.builder.host
        cpus = os.cpu_count() or 1
        sub = "%s  |  %s  |  %d CPU threads" % (h["label"], h.get("arch", "?"), cpus)
        self.host_lbl = QLabel("Detected: " + sub)
        self.host_lbl.setObjectName("subtitle")
        lay.addWidget(self.host_lbl)
        lay.addWidget(label("Builds the game on this computer from your own Pokemon Emerald dump. Nothing is uploaded; "
                            "the ROM is only read to check it.", muted=True))
        return lay

    # ------------------------------------------------------------------ setup page
    def _setup_page(self):
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        page = QWidget()
        scroll.setWidget(page)
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 8, 8, 8)

        g = QGroupBox("Repository (the build scripts)")
        gl = QGridLayout(g)
        self.repo_edit = QLineEdit()
        self.repo_edit.setReadOnly(True)
        self.repo_edit.setAccessibleName("Repository folder")
        self.repo_choose = QPushButton("Choose &folder...")
        self.repo_choose.clicked.connect(self.choose_repo)
        self.repo_clone = QPushButton("&Clone repository")
        self.repo_clone.clicked.connect(self.clone_repo)
        self.repo_note = label("", muted=True)
        gl.addWidget(self.repo_edit, 0, 0)
        gl.addWidget(self.repo_choose, 0, 1)
        gl.addWidget(self.repo_clone, 0, 2)
        gl.addWidget(self.repo_note, 1, 0, 1, 3)
        lay.addWidget(g)

        g = QGroupBox("Prerequisites")
        self.checks_lay = QVBoxLayout(g)
        top = QHBoxLayout()
        top.addWidget(label("Live checklist of what this computer needs.", muted=True), 1)
        self.refresh_btn = QPushButton("&Refresh")
        self.refresh_btn.clicked.connect(self.refresh_checks)
        top.addWidget(self.refresh_btn)
        self.checks_lay.addLayout(top)
        self.checks_body = QVBoxLayout()
        self.checks_lay.addLayout(self.checks_body)
        lay.addWidget(g)

        g = QGroupBox("Your Pokemon Emerald ROM")
        gl = QGridLayout(g)
        self.rom_edit = QLineEdit()
        self.rom_edit.setPlaceholderText("Path to your .gba dump (read only to check it, never copied)")
        self.rom_edit.setAccessibleName("ROM path")
        self.rom_edit.editingFinished.connect(self.check_rom)
        browse = QPushButton("&Browse...")
        browse.clicked.connect(self.browse_rom)
        self.rom_status = label("", wrap=True)
        self.rom_need = label("", muted=True)
        gl.addWidget(self.rom_edit, 0, 0)
        gl.addWidget(browse, 0, 1)
        gl.addWidget(self.rom_status, 1, 0, 1, 2)
        gl.addWidget(self.rom_need, 2, 0, 1, 2)
        lay.addWidget(g)

        g = QGroupBox("Build options")
        gl = QGridLayout(g)
        self.target_radios = {}
        r = 0
        for t in self.builder.targets():
            rb = QRadioButton(t["label"])
            rb.setEnabled(t["available"])
            rb.setToolTip(t["why"])
            rb.toggled.connect(self.on_target_changed)
            self.target_radios[t["id"]] = rb
            gl.addWidget(rb, r, 0, 1, 2)
            gl.addWidget(label(t["why"], muted=True), r, 2, 1, 2)
            r += 1
        self.dir_edit = QLineEdit()
        self.dir_edit.setAccessibleName("Install folder")
        self.dir_btn = QPushButton("Choose...")
        self.dir_btn.clicked.connect(self.choose_dir)
        gl.addWidget(QLabel("Install folder:"), r, 0)
        gl.addWidget(self.dir_edit, r, 1, 1, 2)
        gl.addWidget(self.dir_btn, r, 3)
        r += 1
        self.jobs = QSpinBox()
        self.jobs.setRange(1, 64)
        self.jobs.setAccessibleName("Compile jobs")
        self.low = QCheckBox("Low priority (keeps the computer usable while it builds)")
        gl.addWidget(QLabel("Compile jobs:"), r, 0)
        gl.addWidget(self.jobs, r, 1)
        gl.addWidget(self.low, r, 2, 1, 2)
        r += 1
        self.container = QLineEdit()
        self.container.setAccessibleName("Container name")
        self.run_test = QCheckBox("Run the 12 second start-up test at the end (needs the desktop)")
        self.container_lbl = QLabel("Container name:")
        gl.addWidget(self.container_lbl, r, 0)
        gl.addWidget(self.container, r, 1)
        gl.addWidget(self.run_test, r, 2, 1, 2)
        lay.addWidget(g)

        row = QHBoxLayout()
        self.blockers_lbl = label("", muted=True)
        row.addWidget(self.blockers_lbl, 1)
        self.start_btn = QPushButton("&Start build")
        self.start_btn.setObjectName("primary")
        self.start_btn.clicked.connect(self.start_build)
        row.addWidget(self.start_btn)
        lay.addLayout(row)
        lay.addStretch(1)
        return scroll

    # ------------------------------------------------------------------ build page
    def _build_page(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 10, 0, 0)
        self.task_title = QLabel("")
        self.task_title.setObjectName("h2")
        lay.addWidget(self.task_title)
        self.stage_row = QHBoxLayout()
        lay.addLayout(self.stage_row)
        pr = QHBoxLayout()
        self.bar = QProgressBar()
        self.bar.setRange(0, 1000)
        self.bar.setAccessibleName("Build progress")
        pr.addWidget(self.bar, 1)
        self.elapsed = QLabel("0:00:00")
        self.elapsed.setMinimumWidth(70)
        pr.addWidget(self.elapsed)
        lay.addLayout(pr)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(60000)
        self.log.setAccessibleName("Build log")
        lay.addWidget(self.log, 1)
        self.fail_box = QFrame()
        self.fail_box.setObjectName("card")
        self.fail_box.setStyleSheet("QFrame#card{border:1px solid %s}" % theme.BAD)
        fl = QVBoxLayout(self.fail_box)
        self.fail_lbl = label("")
        self.fail_lbl.setTextFormat(Qt.RichText)
        fl.addWidget(self.fail_lbl)
        self.fail_box.hide()
        lay.addWidget(self.fail_box)
        row = QHBoxLayout()
        self.cancel_btn = QPushButton("&Cancel build")
        self.cancel_btn.setObjectName("danger")
        self.cancel_btn.clicked.connect(self.cancel)
        self.logfile_btn = QPushButton("Open &log file")
        self.logfile_btn.clicked.connect(self.open_logfile)
        self.again_btn = QPushButton("Back to &setup")
        self.again_btn.clicked.connect(lambda: self.tabs.setCurrentIndex(0))
        row.addWidget(self.cancel_btn)
        row.addWidget(self.logfile_btn)
        row.addStretch(1)
        row.addWidget(self.again_btn)
        lay.addLayout(row)
        lay.addWidget(label("Running the build again is safe: finished steps are skipped and it continues where it stopped.", muted=True))
        return page

    # ------------------------------------------------------------------ result page
    def _result_page(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 10, 0, 0)
        self.res_title = QLabel("")
        self.res_title.setObjectName("h2")
        lay.addWidget(self.res_title)
        self.res_info = label("")
        self.res_info.setTextFormat(Qt.RichText)
        lay.addWidget(self.res_info)
        row = QHBoxLayout()
        self.open_btn = QPushButton("&Open folder")
        self.open_btn.clicked.connect(lambda: self.open_result("folder"))
        self.launch_btn = QPushButton("&Launch game")
        self.launch_btn.setObjectName("primary")
        self.launch_btn.clicked.connect(lambda: self.open_result("launch"))
        row.addWidget(self.open_btn)
        row.addWidget(self.launch_btn)
        row.addStretch(1)
        lay.addLayout(row)
        self.res_text = QTextBrowser()
        self.res_text.setOpenExternalLinks(True)
        self.res_text.setAccessibleName("Next steps")
        lay.addWidget(self.res_text, 1)
        legal = label("Legal: " + LEGAL_NOTE)
        legal.setStyleSheet("color:%s" % theme.WARN)
        lay.addWidget(legal)
        return page

    # ------------------------------------------------------------------ settings
    def load_settings_into_ui(self):
        s = self.settings
        tg = (s.get("targets") or [""])[0]
        avail = [t["id"] for t in self.builder.targets() if t["available"]]
        pick = tg if tg in avail else (avail[0] if avail else None)
        for k, rb in self.target_radios.items():
            rb.blockSignals(True)
            rb.setChecked(k == pick)
            rb.blockSignals(False)
        self.rom_edit.setText(s.get("rom", ""))
        self.dir_edit.setText(s.get("dir") or self.builder.default_dir())
        self.jobs.setValue(s.get("jobs") or max(1, (os.cpu_count() or 2) // 2))
        self.low.setChecked(s.get("low_priority", True))
        self.run_test.setChecked(s.get("run_test", False))
        self.container.setText(s.get("container") or "emerald-build")
        if s.get("msys_root"):
            self.builder.msys_root = s["msys_root"]
        self.on_target_changed()
        if self.rom_edit.text():
            self.check_rom()

    def current_target(self):
        for k, rb in self.target_radios.items():
            if rb.isChecked():
                return k
        return None

    def collect_opts(self):
        tg = self.current_target()
        return {"targets": [tg] if tg else [], "rom": self.rom_edit.text().strip(), "dir": self.dir_edit.text().strip(),
                "jobs": self.jobs.value(), "low_priority": self.low.isChecked(), "run_test": self.run_test.isChecked(),
                "container": self.container.text().strip() or "emerald-build"}

    def save(self):
        o = self.collect_opts()
        save_settings({"targets": o["targets"], "rom": o["rom"], "jobs": o["jobs"], "low_priority": o["low_priority"],
                       "run_test": o["run_test"], "container": o["container"],
                       "dir": o["dir"] if self.current_target() == "linux32" else self.settings.get("dir", "")})

    # ------------------------------------------------------------------ setup actions
    def on_target_changed(self):
        lin = self.current_target() == "linux32"
        win = self.current_target() == "windows"
        for w in (self.container, self.container_lbl, self.run_test):
            w.setVisible(lin)
        self.dir_edit.setEnabled(lin)
        self.dir_btn.setEnabled(lin)
        if win:
            self.dir_edit.setText(os.path.join(self.builder.repo, "dist-win") + "   (inside the repository)")
        elif lin and "(inside" in self.dir_edit.text():
            self.dir_edit.setText(self.settings.get("dir") or self.builder.default_dir())
        self.update_start_state()

    def update_repo_ui(self):
        ok = is_repo(self.builder.repo)
        self.repo_edit.setText(self.builder.repo if ok else "")
        self.repo_edit.setPlaceholderText("No repository chosen yet")
        self.repo_note.setText("Build scripts found." if ok else
                               "The app needs a clone of %s (the repository is private for now, so a clone may need credentials). "
                               "Pick an existing clone, or press Clone repository." % REPO_URL)
        self.update_start_state()

    def choose_repo(self):
        d = QFileDialog.getExistingDirectory(self, "Choose the bravely-emerald folder", self.builder.repo or os.path.expanduser("~"))
        if d:
            try:
                self.builder.set_repo(d)
            except ValueError as e:
                QMessageBox.warning(self, "Not the repository", str(e))
                return
            self.update_repo_ui()
            self.on_target_changed()
            self.refresh_checks()

    def clone_repo(self):
        base = QFileDialog.getExistingDirectory(self, "Choose the folder to clone into", os.path.dirname(os.path.abspath(sys.executable)) if getattr(sys, "frozen", False) else os.path.expanduser("~"))
        if not base:
            return
        dest = os.path.join(base, "bravely-emerald")
        try:
            label_, stages, steps, meta = self.builder.plan_clone(dest)
        except ValueError as e:
            QMessageBox.warning(self, "Clone", str(e))
            return
        if QMessageBox.question(self, "Clone repository", "Run git clone of\n%s\ninto\n%s\n\nThe repository is private for now: "
                                "git may ask for credentials (the app never sees them)." % (REPO_URL, dest)) != QMessageBox.Yes:
            return
        self.begin_task(label_, stages, steps, meta, "clone")

    def browse_rom(self):
        f, _ = QFileDialog.getOpenFileName(self, "Select your Pokemon Emerald .gba file", os.path.dirname(self.rom_edit.text()) or os.path.expanduser("~"),
                                           "GBA ROM (*.gba);;All files (*)")
        if f:
            self.rom_edit.setText(os.path.normpath(f))
            self.check_rom()

    def choose_dir(self):
        d = QFileDialog.getExistingDirectory(self, "Choose the install folder", self.dir_edit.text() or os.path.expanduser("~"))
        if d:
            self.dir_edit.setText(os.path.normpath(d))

    def check_rom(self):
        p = self.rom_edit.text().strip()
        if not p:
            self.rom_ok = False
            self.rom_status.setText("")
        else:
            v = validate_rom(p)
            self.rom_ok = v["ok"]
            color = theme.OK if v["ok"] else theme.BAD
            self.rom_status.setText('<span style="color:%s;font-weight:600">%s</span> %s' % (color, "VALID" if v["ok"] else "REJECTED", v["message"]))
            self.rom_status.setTextFormat(Qt.RichText)
        have = self.builder.have_assets() if self.builder.repo else False
        self.rom_need.setText("The game files already exist, so the ROM is not needed again (it is only used for the first run)." if have
                              else "Needed for the first run: it is read to check it is Pokemon Emerald; it is not copied or stored.")
        self.update_start_state()

    def refresh_checks(self):
        if not is_repo(self.builder.repo):
            self.apply_checks([])
            return
        self.refresh_btn.setEnabled(False)
        self.status.setText("Checking prerequisites...")

        def work():
            try:
                res = self.builder.checks()
            except Exception as e:
                res = [{"id": "err", "label": "Checks failed", "status": "bad", "detail": repr(e), "action": None, "manual": None, "blocking": True, "auto": False}]
            self.bridge.checks_done.emit(res)
        threading.Thread(target=work, daemon=True).start()

    def apply_checks(self, items):
        self.checks = items
        while self.checks_body.count():
            w = self.checks_body.takeAt(0).widget()
            if w:
                w.deleteLater()
        for it in items:
            self.checks_body.addWidget(PrereqRow(it, self.install))
        self.refresh_btn.setEnabled(True)
        self.status.setText("")
        self.check_rom() if self.rom_edit.text() else self.update_start_state()

    def update_start_state(self):
        busy = bool(self.task and self.task.state == "running")
        repo_ok = is_repo(self.builder.repo)
        blockers = [c for c in self.checks if c["blocking"]]
        need_rom = repo_ok and not self.builder.have_assets()
        reasons = []
        if not repo_ok:
            reasons.append("choose or clone the repository")
        if not self.current_target():
            reasons.append("no target available on this computer")
        if blockers:
            reasons.append("fix the missing prerequisites above (%s)" % ", ".join(c["label"] for c in blockers))
        if need_rom and not self.rom_ok:
            reasons.append("pick a valid ROM")
        self.blockers_lbl.setText(("Not ready: " + "; ".join(reasons) + ".") if reasons else "Ready.")
        self.start_btn.setEnabled(not reasons and not busy)

    # ------------------------------------------------------------------ installs
    def install(self, what):
        if what == "msys2":
            self.status.setText("Looking up the installer size...")
            threading.Thread(target=lambda: self.bridge.msys_info.emit(self.builder.msys2_prepare()), daemon=True).start()
            return
        text = {"packages": "Run pacman in MSYS2 to update it and install the build packages (mingw32 toolchain, cmake, make, python-pillow, ...).\n"
                            "This downloads packages from the MSYS2 mirrors.",
                "deps": "Download the official SDL2 2.32.10 and SDL2_image 2.8.12 sources and build them as static 32-bit libraries "
                        "(tools/windows/build_deps.sh). A few minutes."}.get(what, what)
        if QMessageBox.question(self, "Install", text + "\n\nContinue?") != QMessageBox.Yes:
            return
        try:
            label_, stages, steps, meta = self.builder.plan_install(what, True)
        except ValueError as e:
            QMessageBox.warning(self, "Install", str(e))
            return
        self.begin_task(label_, stages, steps, {"logdir": os.path.join(self.builder.repo, "build-win", "builder-logs"), "low": False}, "install")

    def show_msys_consent(self, info):
        self.status.setText("")
        size = ("%.1f MB (%d bytes)" % (info["size"] / 1048576.0, info["size"])) if info.get("size") else "unknown (could not reach github.com)"
        box = QMessageBox(self)
        box.setIcon(QMessageBox.Question)
        box.setWindowTitle("Download the MSYS2 installer")
        box.setText("Download and run the official MSYS2 installer?")
        box.setInformativeText("File: %s\nFrom: %s\nSize: %s\nSaved in: %s\nThen run silently into: %s\n\nWindows may show a permission prompt." %
                               (info["filename"], info["url"], size, info["dest"], info["root"]))
        box.setStandardButtons(QMessageBox.Yes | QMessageBox.Cancel)
        box.button(QMessageBox.Yes).setText("Download and install")
        box.setDefaultButton(QMessageBox.Cancel)
        if box.exec() != QMessageBox.Yes:
            return
        try:
            label_, stages, steps, meta = self.builder.plan_install("msys2", True)
        except ValueError as e:
            QMessageBox.warning(self, "Install", str(e))
            return
        self.begin_task(label_, stages, steps, {"logdir": os.path.join(os.environ.get("TEMP", "."), "bravely-builder-logs"), "low": False}, "install")

    # ------------------------------------------------------------------ build
    def start_build(self):
        self.save()
        try:
            label_, stages, steps, meta = self.builder.plan_build(self.collect_opts())
        except ValueError as e:
            QMessageBox.warning(self, "Cannot start", str(e))
            return
        meta["container"] = self.collect_opts()["container"]
        self.begin_task(label_, stages, steps, meta, "build")

    def begin_task(self, label_, stages, steps, meta, kind):
        try:
            self.task = self.builder.start(label_, stages, steps, meta, kind, lambda ev: self.bridge.event.emit(ev))
        except ValueError as e:
            QMessageBox.warning(self, "Busy", str(e))
            return
        self.task_kind = kind
        self.log.clear()
        self.log_buf = []
        self.fail_box.hide()
        self.pct = 0
        self.task_title.setText(label_)
        self.set_stages(stages)
        self.bar.setValue(0)
        self.cancel_btn.setEnabled(True)
        self.tabs.setTabEnabled(1, True)
        self.tabs.setTabEnabled(2, False)
        self.tabs.setCurrentIndex(1)
        self.update_start_state()
        self.logfile_btn.setEnabled(bool(self.task.logfile))

    def set_stages(self, stages):
        while self.stage_row.count():
            w = self.stage_row.takeAt(0).widget()
            if w:
                w.deleteLater()
        self.stage_labels, self.stage_state, self.stage_order = {}, {}, list(stages)
        for s in stages:
            lb = QLabel(STAGE_LABELS.get(s, s))
            lb.setAlignment(Qt.AlignCenter)
            self.stage_labels[s] = lb
            self.stage_state[s] = "pending"
            self.stage_row.addWidget(lb)
        self.restyle_stages()

    def restyle_stages(self):
        for s, lb in self.stage_labels.items():
            st = self.stage_state[s]
            col, bg, fw = {"pending": (theme.MUTED, "#1c2228", "400"), "active": ("#06140d", theme.ACCENT, "700"),
                           "done": (theme.OK, "#18322a", "600"), "failed": ("white", "#8a2d2d", "700")}[st]
            mark = {"done": "✓ ", "failed": "✗ "}.get(st, "")
            lb.setText(mark + STAGE_LABELS.get(s, s))
            lb.setStyleSheet("color:%s;background:%s;font-weight:%s;border-radius:11px;padding:4px 10px;" % (col, bg, fw))
        n = max(1, len(self.stage_order))
        done = sum(1 for s in self.stage_order if self.stage_state[s] == "done")
        act = next((i for i, s in enumerate(self.stage_order) if self.stage_state[s] == "active"), None)
        frac = 0.0
        if act is not None and self.stage_order[act] == "compile":
            frac = self.pct / 100.0
        elif act is not None:
            frac = 0.3
        self.bar.setValue(int(1000 * min(1.0, (done + frac) / n)))
        self.bar.setFormat("%d%%" % int(self.bar.value() / 10))

    def on_event(self, ev):
        t = ev["type"]
        if t == "log":
            self.log_buf.append(ev["text"])
        elif t == "stage":
            s = ev["stage"]
            if s in self.stage_state:
                idx = self.stage_order.index(s)
                for o in self.stage_order[:idx]:
                    if self.stage_state[o] in ("pending", "active"):
                        self.stage_state[o] = "done"
                for o in self.stage_order:
                    if self.stage_state[o] == "active" and o != s:
                        self.stage_state[o] = "done"
                self.stage_state[s] = "active"
                self.pct = 0
                self.restyle_stages()
        elif t == "progress":
            self.pct = ev["pct"]
            self.restyle_stages()
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
        self.cancel_btn.setEnabled(False)
        if st == "done":
            for s in self.stage_order:
                self.stage_state[s] = "done"
            self.bar.setValue(1000)
        else:
            for s in self.stage_order:
                if self.stage_state[s] == "active":
                    self.stage_state[s] = "failed" if st == "failed" else "pending"
        self.restyle_stages()
        kind = self.task_kind
        if st == "failed":
            hints = ev.get("hints") or []
            html = "<b>The step failed.</b> The log above has the details."
            if ev.get("step"):
                html += " Step: %s (exit code %s)." % (ev["step"], ev.get("code"))
            for h in hints:
                html += "<br><br><b>%s</b><br>%s" % (h["title"], h["fix"])
            if not hints:
                html += "<br><br>No known fix matches this message. Read the first error lines of the log; see docs/BUILD_DECK.md or docs/WINDOWS_BUILD.md."
            self.fail_lbl.setText(html)
            self.fail_box.show()
        elif st == "cancelled":
            self.fail_lbl.setText("<b>Cancelled.</b> The whole process tree was stopped. Start again to resume.")
            self.fail_box.show()
        elif st == "done":
            if kind == "build":
                self.show_result(self.task.result or {})
            elif kind == "clone":
                try:
                    self.builder.set_repo(self.task.meta["clone_dest"])
                    self.update_repo_ui()
                    self.on_target_changed()
                except ValueError as e:
                    QMessageBox.warning(self, "Clone", str(e))
            if kind in ("install", "clone"):
                self.refresh_checks()
        self.update_start_state()
        self.status.setText("%s: %s in %s" % (self.task.label, st, fmt_secs((self.task.finished or time.time()) - self.task.started)))

    def tick(self):
        t = self.task
        if t and t.state == "running":
            self.elapsed.setText(fmt_secs(time.time() - t.started))
        elif t and t.finished:
            self.elapsed.setText(fmt_secs(t.finished - t.started))

    def cancel(self):
        if self.task and self.task.state == "running":
            self.cancel_btn.setEnabled(False)
            self.builder.cancel()

    def open_logfile(self):
        if self.task and self.task.logfile and os.path.isfile(self.task.logfile):
            QDesktopServices.openUrl(QUrl.fromLocalFile(self.task.logfile))

    # ------------------------------------------------------------------ result
    def show_result(self, res):
        self.res_result = res
        ok = res.get("ok") or self.builder.dry_run
        self.tabs.setTabEnabled(2, True)
        self.tabs.setCurrentIndex(2)
        if self.builder.dry_run:
            self.res_title.setText("Dry run finished (nothing was built)")
        else:
            self.res_title.setText("Build finished" if ok else "Build incomplete")
        rows = [("Game folder", res.get("folder")), ("Zip", res.get("zip")), ("Launcher", res.get("launcher")),
                ("Saves", res.get("savedir")), ("Log", res.get("log"))]
        self.res_info.setText("<br>".join("<b>%s:</b> %s" % (k, v) for k, v in rows if v))
        self.open_btn.setEnabled(bool(res.get("ok")))
        self.launch_btn.setEnabled(bool(res.get("launch")))
        self.res_text.setHtml(self.next_steps_html(res))

    CODE_CSS = '<style>code{font-family:Consolas,"DejaVu Sans Mono",monospace;color:#2fcf8a}</style>'

    def next_steps_html(self, res):
        return self.CODE_CSS + self._next_steps(res)

    def _next_steps(self, res):
        if res.get("target") == "linux32":
            return ("<h3>Add it to Steam (Game Mode)</h3><ol>"
                    "<li>In Desktop Mode open <b>Steam</b>, then <i>Games &gt; Add a Non-Steam Game to My Library</i>.</li>"
                    "<li>Pick <b>Pokemon Emerald (Deck)</b> in the list. If it is not listed, press <i>Browse</i>, choose the file type "
                    "<i>All files</i> and select <code>%s</code>. Press <i>Add Selected Programs</i>.</li>"
                    "<li>Optional: right-click the entry &gt; <i>Properties</i> to rename it or set artwork.</li>"
                    "<li>Switch back to Game Mode with the <i>Return to Gaming Mode</i> icon. The game is under <i>Non-Steam</i>.</li></ol>"
                    "<p>The launcher starts the game inside the build container with distrobox. Starting from Game Mode has not been "
                    "verified by the authors of the guide; see docs/BUILD_DECK.md.</p>"
                    "<h3>Import a save</h3><p>Close the game, then copy <code>pokeemerald.sav</code> from another install (the Windows build's "
                    "<code>userdata\\saves\\</code>) to <code>%s</code>.</p>"
                    "<p>Saves live outside the build folder, so rebuilding never touches them.</p>" %
                    (res.get("launcher", "~/emerald-deck/emerald-launch.sh"), res.get("savedir", "")))
        return ("<h3>Play</h3><p>Open the folder and double-click <code>run.bat</code> (or <code>pokeemerald.exe</code>). Keep <code>app0\\</code> and "
                "<code>userdata\\</code> next to the exe.</p>"
                "<h3>Import a Steam Deck save</h3><p>Close the game, then copy <code>userdata/saves/pokeemerald.sav</code> from the Deck install "
                "(<code>~/emerald-deck/userdata/saves/</code>) to <code>%s</code>, replacing the existing file.</p>"
                "<p>The zip next to the repository is for yourself only; do not share it.</p>" % res.get("savedir", "userdata\\saves"))

    def open_result(self, what):
        try:
            self.builder.open_result(what)
        except Exception as e:
            QMessageBox.warning(self, "Open", str(e))

    # ------------------------------------------------------------------ closing
    def closeEvent(self, e):
        if self.task and self.task.state == "running":
            if QMessageBox.question(self, "Quit", "A task is running. Cancel it and quit?") != QMessageBox.Yes:
                e.ignore()
                return
            self.builder.cancel()
            for _ in range(50):
                if self.task.state != "running":
                    break
                time.sleep(0.1)
        self.save()
        e.accept()
