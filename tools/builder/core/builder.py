"""Builder: what can be built on this host, the prerequisite checks, the commands for the existing scripts
(tools/deck/build_deck.sh, fetch_assets.sh, tools/windows/*) and the runner that executes them. No UI code."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

from .constants import (MSYS_PACKAGES, MSYS_INSTALLER_URL, MSYS_INSTALLER_FILE, MIN_FREE_GB, HARD_MIN_FREE_GB,
                        CONTAINER_RE)
from .errors import explain
from .host import detect_host
from .repo import is_repo
from .settings import load_settings
from .tasks import Step, Task, kill_tree
from .validate import clean_path, validate_rom, free_gb, network_ok, fmt_secs, sha256_file


class Builder:
    """One instance per window/CLI run. Runs one task at a time."""

    def __init__(self, repo, dry_run=False, prefix=None):
        self.repo = repo
        self.host = detect_host()
        self.dry_run = dry_run
        self.prefix = prefix or []
        self.task = None
        self.lock = threading.Lock()
        self.net_cache = (0, False)
        self.msys_root = None

    # ----- environment helpers
    def settings(self):
        return load_settings()

    def msys(self):
        root = self.msys_root or load_settings().get("msys_root") or r"C:\msys64"
        return root

    def msys_bash(self):
        return os.path.join(self.msys(), "usr", "bin", "bash.exe")

    def msys_cmd(self, script, args=(), msystem="MSYS"):
        """argv + env that run `script` in an MSYS2 login shell; paths go in as $1.. (no string splicing)."""
        argv = [self.msys_bash(), "-lc", script, "builder"] + [str(a) for a in args]
        return argv, {"MSYSTEM": msystem, "CHERE_INVOKING": "1"}

    def default_dir(self):
        if self.host["kind"] == "windows":
            return os.path.join(os.path.expanduser("~"), "BravelyEmerald", "game")
        return os.path.join(os.path.expanduser("~"), "emerald-deck")

    def have_assets(self):
        r = self.repo
        return (os.path.isfile(os.path.join(r, ".assets_ok")) and
                os.path.isfile(os.path.join(r, "graphics", "pokemon", "bulbasaur", "front.png")) and
                os.path.isfile(os.path.join(r, "assets", "ui2", "ui_atlas.bin")))

    def have_net(self):
        now = time.time()
        if now - self.net_cache[0] > 20:
            self.net_cache = (now, network_ok())
        return self.net_cache[1]

    # ----- targets
    def targets(self):
        k = self.host["kind"]
        win_ok = k == "windows"
        lin_ok = k in ("steamos", "linux") and self.host["arch"] in ("x86_64", "amd64")
        return [
            {"id": "windows", "label": "Windows exe (32-bit)", "available": win_ok,
             "why": "Native build on this PC with MSYS2 (mingw32)." if win_ok else
             "Not available here: the Windows build runs natively on Windows (MSYS2). Cross building from Linux has not been verified."},
            {"id": "linux32", "label": "Linux / Steam Deck (32-bit x86)", "available": lin_ok,
             "why": "Native build in a distrobox container (tools/deck/build_deck.sh)." if lin_ok else
             "Not available here: the Linux build needs a Linux x86_64 host with distrobox. Building it from Windows (WSL) has not been verified."},
        ]

    # ----- prerequisite checks
    def checks(self):
        k = self.host["kind"]
        items = []

        def add(id_, label, status, detail, action=None, manual=None, blocking=False, auto=False):
            items.append({"id": id_, "label": label, "status": status, "detail": detail,
                          "action": action, "manual": manual, "blocking": blocking and status == "bad", "auto": auto})

        gb = free_gb(self.repo)
        if gb >= MIN_FREE_GB:
            add("disk", "Free disk space", "ok", "%.0f GB free (at least %d GB recommended)." % (gb, MIN_FREE_GB))
        elif gb >= HARD_MIN_FREE_GB:
            add("disk", "Free disk space", "warn", "%.1f GB free; %d GB or more is recommended (build trees, SDL2, container)." % (gb, MIN_FREE_GB))
        else:
            add("disk", "Free disk space", "bad", "%.1f GB free; at least %d GB are needed." % (gb, HARD_MIN_FREE_GB), blocking=True)
        net = self.have_net()
        add("net", "Network (github.com)", "ok" if net else "bad",
            "Reachable." if net else "github.com is not reachable: packages, SDL2 and the game files are downloaded.", blocking=True)
        if k == "windows":
            self._checks_windows(add)
        elif k in ("steamos", "linux"):
            self._checks_linux(add)
        else:
            add("host", "Host", "bad", "Unsupported system: %s." % self.host["label"], blocking=True)
        return items

    def _checks_windows(self, add):
        gitwin = shutil.which("git")
        add("git", "Git", "ok" if gitwin else "info",
            ("Found: %s" % gitwin) if gitwin else "Git for Windows not on PATH. The build uses the MSYS2 'git' package instead "
            "(installed with the MSYS2 packages below). To clone this repository you need Git from https://git-scm.com.")
        bash = self.msys_bash()
        have_msys = os.path.isfile(bash)
        add("msys2", "MSYS2 (%s)" % self.msys(), "ok" if have_msys else "bad",
            "Found." if have_msys else "Not found. Install it from the official installer (about 82 MB, needs consent).",
            action=None if have_msys else "msys2", blocking=True)
        if not have_msys:
            add("packages", "MSYS2 packages (mingw32 toolchain, cmake, make, python-pillow, ...)", "bad",
                "Needs MSYS2 first.", blocking=True)
        else:
            missing, err = self._missing_packages()
            if err:
                add("packages", "MSYS2 packages", "bad", "Could not query pacman: %s" % err, action="packages", blocking=True)
            elif missing:
                add("packages", "MSYS2 packages", "bad", "Missing: %s" % ", ".join(missing), action="packages", blocking=True)
            else:
                add("packages", "MSYS2 packages", "ok", "All %d required packages are installed." % len(MSYS_PACKAGES))
        deps = os.path.join(self.repo, "build-win", "deps", "lib")
        have_deps = os.path.isfile(os.path.join(deps, "libSDL2.a")) and os.path.isfile(os.path.join(deps, "libSDL2_image.a"))
        add("deps", "SDL2 2.32.10 + SDL2_image 2.8.12 (static, 32-bit)", "ok" if have_deps else "warn",
            "Built in build-win/deps." if have_deps else "Not built yet. The build makes them (a few minutes), or press Install.",
            action=None if have_deps else "deps", auto=True)
        add("assets", "Game files (graphics, sound, data from your ROM)", "ok" if self.have_assets() else "warn",
            "Present (.assets_ok): the ROM is not needed again." if self.have_assets() else
            "Not made yet. Pick your ROM below; the build downloads and checks the files.", auto=True)

    def _missing_packages(self):
        # a name is satisfied when it is an installed package, or a package group whose members are all installed
        script = ('for p in "$@"; do pacman -Q "$p" >/dev/null 2>&1 && continue; '
                  'n=$(pacman -Sg "$p" 2>/dev/null | wc -l); '
                  '[ "$n" -gt 0 ] && [ "$(pacman -Qg "$p" 2>/dev/null | wc -l)" -eq "$n" ] && continue; '
                  'echo "MISSING $p"; done')
        argv, env = self.msys_cmd(script, MSYS_PACKAGES)
        try:
            p = subprocess.run(argv, env=dict(os.environ, **env), capture_output=True, text=True, timeout=180,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except (OSError, subprocess.TimeoutExpired) as e:
            return [], str(e)
        if p.returncode != 0 and "MISSING" not in p.stdout:
            return [], (p.stdout + p.stderr).strip()[:200] or "pacman failed"
        return re.findall(r"^MISSING (\S+)", p.stdout, re.M), None

    def _checks_linux(self, add):
        arch_ok = self.host["arch"] in ("x86_64", "amd64")
        add("arch", "CPU architecture", "ok" if arch_ok else "bad",
            self.host["arch"] if arch_ok else "%s: the Linux build is for x86_64 only." % self.host["arch"], blocking=True)
        idl = self.host.get("id_like", "")
        if "arch" in idl or self.host["kind"] == "steamos":
            cmd = "sudo pacman -S distrobox podman"
        elif "debian" in idl or "ubuntu" in idl:
            cmd = "sudo apt install distrobox podman"
        elif "fedora" in idl or "rhel" in idl:
            cmd = "sudo dnf install distrobox podman"
        elif "suse" in idl:
            cmd = "sudo zypper install distrobox podman"
        else:
            cmd = "Install distrobox and podman with your package manager (https://distrobox.it)"
        for tool in ("distrobox", "podman"):
            have = shutil.which(tool)
            if self.host["kind"] == "steamos" and not have:
                manual = "SteamOS ships %s. Run a SteamOS update in Settings > System." % tool
            else:
                manual = cmd + "   (needs your administrator password: run it in a terminal)"
            add(tool, tool, "ok" if have else "bad", ("Found: %s" % have) if have else "Not found.",
                manual=None if have else manual, blocking=True)
        if shutil.which("distrobox"):
            name = self.settings().get("container") or "emerald-build"
            try:
                p = subprocess.run(["distrobox", "list"], capture_output=True, text=True, timeout=30)
                exists = any(name == (ln.split("|")[1].strip() if "|" in ln and len(ln.split("|")) > 1 else "") for ln in p.stdout.splitlines()[1:])
            except (OSError, subprocess.TimeoutExpired):
                exists = False
            add("container", "Build container '%s'" % name, "ok" if exists else "warn",
                "Exists." if exists else "Will be created by the build (Arch Linux image, about 400 MB, then the 32-bit toolchain).", auto=True)
        add("deps32", "SDL2 + SDL2_image (32-bit, static) and the toolchain", "info",
            "Made by the build inside the container (steps 2 and 3 of build_deck.sh). Finished steps are skipped on a rerun.", auto=True)
        add("assets", "Game files (graphics, sound, data from your ROM)", "ok" if self.have_assets() else "warn",
            "Present (.assets_ok): the ROM is not needed again." if self.have_assets() else
            "Not made yet. Pick your ROM below; the build downloads and checks the files.", auto=True)
        add("sudo", "Administrator password", "info",
            "Not needed. The commands that need root run inside the container. If anything ever asks for a password, "
            "run that command in a terminal yourself; this app never asks for or stores passwords.", auto=True)

    # ----- installs (Windows only)
    def msys2_prepare(self):
        size = None
        try:
            req = urllib.request.Request(MSYS_INSTALLER_URL, method="HEAD")
            with urllib.request.urlopen(req, timeout=15) as r:
                size = int(r.headers.get("Content-Length") or 0) or None
        except Exception as e:                       # offline: still show the consent with the unknown size
            size = None
        return {"filename": MSYS_INSTALLER_FILE, "url": MSYS_INSTALLER_URL, "size": size,
                "dest": os.path.join(tempfile.gettempdir(), "bravely-builder-dl"),
                "root": self.msys(),
                "text": "Download the official MSYS2 installer and run it silently into %s." % self.msys()}

    def plan_install(self, what, consent=False):
        """Return (label, stages, steps, meta) for an install action."""
        label, stages, steps = self._plan_install(what, consent)
        logdir = os.path.join(tempfile.gettempdir() if what == "msys2" else os.path.join(self.repo, "build-win"), "bravely-builder-logs" if what == "msys2" else "builder-logs")
        return label, stages, steps, {"logdir": logdir, "low": False}

    def _plan_install(self, what, consent=False):
        if self.host["kind"] != "windows":
            raise ValueError("Install actions exist for Windows only; on Linux run the shown package command in a terminal.")
        if what == "msys2":
            if not consent:
                raise ValueError("The download needs your consent.")
            return "Install MSYS2", ["download", "install"], [("msys2", None)]
        if what == "packages":
            if not os.path.isfile(self.msys_bash()):
                raise ValueError("Install MSYS2 first.")
            s1, e1 = self.msys_cmd('pacman --noconfirm -Syu')
            s2, e2 = self.msys_cmd('pacman --noconfirm -S --needed "$@"', MSYS_PACKAGES)
            return "Install MSYS2 packages", ["packages"], [
                Step("pacman -Syu (system update; may report that a restart of MSYS2 is needed, that is fine)", "packages", s1, e1,
                     tolerate=lambda lines: True),
                Step("pacman -S the build packages", "packages", s2, e2)]
        if what == "deps":
            if not os.path.isfile(self.msys_bash()):
                raise ValueError("Install MSYS2 first.")
            argv, env = self.msys_cmd('cd "$(cygpath -u "$1")" && bash tools/windows/build_deps.sh', [self.repo], "MINGW32")
            return "Build SDL2 and SDL2_image", ["deps"], [Step("SDL2 2.32.10 + SDL2_image 2.8.12 (static, 32-bit)", "deps", argv, env)]
        raise ValueError("unknown install item")

    # ----- build planning
    def plan_build(self, opts):
        """Validate the options and return (label, stages, [Step], meta). Raises ValueError with a readable message."""
        kinds = [t for t in opts.get("targets", []) if t in ("windows", "linux32")]
        if len(kinds) != 1:
            raise ValueError("Pick exactly one target (one target is built per run).")
        target = kinds[0]
        avail = {t["id"]: t for t in self.targets()}
        if not avail[target]["available"]:
            raise ValueError(avail[target]["why"])
        jobs = opts.get("jobs")
        if not isinstance(jobs, int) or isinstance(jobs, bool) or not 1 <= jobs <= 64:
            raise ValueError("Jobs must be a number from 1 to 64.")
        low = bool(opts.get("low_priority"))
        rom = ""
        if not self.have_assets() or opts.get("force_assets"):
            rom = clean_path(opts.get("rom", ""), "ROM path", no_space=(target == "linux32"))
            v = validate_rom(rom)
            if not v["ok"]:
                raise ValueError("ROM: " + v["message"])
        if " " in self.repo:
            raise ValueError("The repository path contains a space; move the clone to a path without spaces.")
        if target == "windows":
            return self._plan_windows(rom, jobs, low, opts)
        return self._plan_linux(rom, jobs, low, opts)

    def _plan_windows(self, rom, jobs, low, opts):
        if not self.dry_run:
            blockers = [c for c in self.checks() if c["blocking"]]
            if blockers:
                raise ValueError("Prerequisites missing: " + "; ".join("%s (%s)" % (c["label"], c["detail"]) for c in blockers))
        steps = []
        bash_rules = [
            (r"=== Building the host tools", "tools"), (r"=== Converting the game files", "assets"),
            (r"=== Intro margins", "assets"), (r"=== SDL2 and SDL2_image", "deps"),
            (r"=== Configuring CMake", "compile"), (r"=== Building pokeemerald\.exe", "compile"),
            (r"Linking (C|CXX) executable|\bld\b.*pokeemerald\.exe", "link"),
        ]
        if rom:
            fetch = 'cd "$(cygpath -u "$1")" && FETCH_NO_CONVERT=1 bash ./fetch_assets.sh "$(cygpath -u "$2")"'
            a, e = self.msys_cmd(fetch, [self.repo, rom], "MINGW64")
            steps.append(Step("Game files, pass 1 (checks the ROM, downloads the files; stops at the port's files, as documented)", "files", a, e,
                              tolerate=lambda lines: any("Generating the port's files" in l for l in lines) and any("=== Copying" in l for l in lines)))
            a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && make -f make_tools.mk -j"$2" CC="gcc -static" CXX="g++ -static"', [self.repo, jobs], "MINGW64")
            steps.append(Step("Host tools (static)", "tools", a, e))
            a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && make -f vita/assets.mk -j"$2"', [self.repo, jobs], "MINGW64")
            steps.append(Step("Converting the game's graphics and data", "assets", a, e))
            a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && bash ./fetch_assets.sh "$(cygpath -u "$2")"', [self.repo, rom], "MINGW64")
            steps.append(Step("Game files, pass 2 (UI atlas)", "files", a, e))
        # build.sh takes its job count from nproc; GNU nproc honours OMP_NUM_THREADS
        a, e = self.msys_cmd('export OMP_NUM_THREADS="$2"; cd "$(cygpath -u "$1")" && tools/windows/build.sh', [self.repo, jobs], "MSYS")
        steps.append(Step("tools/windows/build.sh", "tools", a, e, stage_rules=bash_rules))
        a, e = self.msys_cmd('cd "$(cygpath -u "$1")" && tools/windows/package.sh', [self.repo], "MSYS")
        steps.append(Step("tools/windows/package.sh", "package", a, e))
        out = clean_path(opts.get("dir") or self.default_dir(), "Install folder", no_space=False)
        repo_n = os.path.normcase(os.path.abspath(self.repo))
        if os.path.normcase(os.path.abspath(out)) == repo_n or os.path.normcase(os.path.abspath(out)).startswith(repo_n + os.sep):
            raise ValueError("The install folder must be outside the source folder.")
        steps.append(Step("Copy the game to the install folder (saves and settings are kept)", "package", [], func=lambda task: install_windows_result(task, self.repo, out)))
        stages = ["prereq", "files", "tools", "assets", "deps", "compile", "link", "package"]
        meta = {"target": "windows", "dir": out, "logdir": os.path.join(self.repo, "build-win", "builder-logs"),
                "low": low}
        return "Windows build", stages, steps, meta

    def _plan_linux(self, rom, jobs, low, opts):
        d = clean_path(opts.get("dir") or self.default_dir(), "Install folder")
        container = opts.get("container") or "emerald-build"
        if not isinstance(container, str) or not CONTAINER_RE.match(container):
            raise ValueError("Container name: letters, digits, '.', '_' and '-' only.")
        if not self.dry_run:
            blockers = [c for c in self.checks() if c["blocking"]]
            if blockers:
                raise ValueError("Prerequisites missing: " + "; ".join("%s (%s)" % (c["label"], c["detail"]) for c in blockers))
        argv = ["bash", os.path.join(self.repo, "tools", "deck", "build_deck.sh"), "--dir", d,
                "--jobs", str(jobs), "--container", container]
        if rom:
            argv += ["--rom", rom]
        if not opts.get("run_test"):
            argv.append("--no-run")
        rules = [
            (r"^=== 1/6", "prereq"), (r"^=== 2/6", "container"), (r"^=== 3/6", "deps"), (r"^=== 4/6", "files"),
            (r"^=== 5/6", "compile"), (r"Linking (C|CXX) executable", "link"), (r"^=== 6/6", "package"),
            (r"^=== Start-up test", "test"),
        ]
        stages = ["prereq", "container", "deps", "files", "compile", "link", "package"] + (["test"] if opts.get("run_test") else [])
        steps = [Step("tools/deck/build_deck.sh", "prereq", argv, {}, stage_rules=rules)]
        meta = {"target": "linux32", "dir": d, "logdir": os.path.join(d, "builder-logs"), "low": low}
        return "Linux / Steam Deck build", stages, steps, meta

    # ----- repository
    def set_repo(self, path):
        if not is_repo(path):
            raise ValueError("%s does not contain the build scripts (fetch_assets.sh, tools/deck, tools/windows)." % path)
        self.repo = os.path.abspath(path)

    # ----- running
    def start(self, label, stages, steps, meta, kind="build", listener=None):
        with self.lock:
            if self.task and self.task.state == "running":
                raise ValueError("Another task is running. Wait for it or press Cancel.")
            ts = time.strftime("%Y%m%d-%H%M%S")
            logfile = os.path.join(meta["logdir"], "%s-%s.log" % (kind, ts)) if meta and meta.get("logdir") and not self.dry_run else None
            task = Task(kind, label, stages, logfile, self.dry_run)
            task.meta = meta or {}
            self.task = task
        if listener:
            task.listeners.append(listener)
        threading.Thread(target=self._run, args=(task, steps), daemon=True).start()
        return task

    def _spawn(self, step, low):
        argv = list(step.argv)
        env = dict(os.environ)
        env.update(step.env)
        env["PYTHONUNBUFFERED"] = "1"
        kw = {}
        if os.name == "nt":
            flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
            if low:
                flags |= getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
            kw["creationflags"] = flags
        else:
            kw["start_new_session"] = True
            if low and shutil.which("nice"):
                argv = ["nice", "-n", "19"] + argv
            argv = self.prefix + argv
        return subprocess.Popen(argv, cwd=step.cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, **kw), argv

    def _run(self, task, steps):
        low = task.meta.get("low", False)
        tail = []
        try:
            task.log("# %s, started %s%s" % (task.label, time.strftime("%Y-%m-%d %H:%M:%S"), "  [DRY RUN: nothing is executed]" if self.dry_run else ""))
            if steps and isinstance(steps[0], tuple):          # the MSYS2 installer download
                ok = self._run_msys2_install(task)
                task.finish("done" if ok else "failed", hints=[] if ok else explain(tail))
                return
            task.set_stage("prereq")
            for step in steps:
                if task.cancel_requested:
                    break
                task.set_stage(step.stage)
                task.log("")
                task.log("##### %s" % step.name)
                if step.func:
                    task.log("(in-process step)")
                else:
                    task.log("$ " + subprocess.list2cmdline(step.argv))
                if self.dry_run:
                    time.sleep(0.05)
                    task.log("[dry run] not executed")
                    continue
                if step.func:
                    try:
                        step.func(task)
                    except Exception as e:
                        task.log("ERROR: %s" % e)
                        task.finish("failed", hints=[{"title": "The step failed.", "fix": str(e)}], step=step.name)
                        return
                    continue
                try:
                    proc, real = self._spawn(step, low)
                except OSError as e:
                    task.log("ERROR: cannot start: %s" % e)
                    task.finish("failed", hints=[{"title": "The program could not be started.", "fix": str(e)}])
                    return
                task.proc = proc
                lines = []
                for raw in iter(proc.stdout.readline, b""):
                    line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                    lines.append(line)
                    tail.append(line)
                    if len(tail) > 600:
                        del tail[:200]
                    task.log(line)
                    for pat, st in step.stage_rules:
                        if re.search(pat, line):
                            task.set_stage(st)
                    m = re.match(r"^\[\s*(\d+)%\]", line)
                    if m:
                        task.emit("progress", pct=int(m.group(1)))
                proc.wait()
                proc.stdout.close()
                task.proc = None
                rc = proc.returncode
                if task.cancel_requested:
                    break
                if rc != 0 and not (step.tolerate and step.tolerate(lines)):
                    task.log("")
                    task.log("##### FAILED: %s (exit code %s)" % (step.name, rc))
                    hints = explain(tail)
                    task.hints = hints
                    task.finish("failed", hints=hints, code=rc, step=step.name)
                    return
                if rc != 0:
                    task.log("(exit code %s accepted for this step)" % rc)
            if task.cancel_requested:
                task.log("##### CANCELLED")
                task.finish("cancelled")
                return
            task.result = self.collect_result(task)
            if task.kind == "build" and not self.dry_run and not task.result.get("ok"):
                task.log("##### The scripts finished but the result is incomplete: %s" % task.result.get("problem"))
                task.finish("failed", hints=[{"title": "The build ended without the expected files.", "fix": task.result.get("problem", "")}])
                return
            task.log("")
            task.log("##### DONE in %s" % fmt_secs(time.time() - task.started))
            task.finish("done", result=task.result)
        except Exception as e:           # never leave the UI waiting
            task.log("INTERNAL ERROR: %r" % (e,))
            task.finish("failed", hints=[{"title": "Internal error in the builder.", "fix": repr(e)}])

    def _run_msys2_install(self, task):
        """Download the official installer (consent was given in the UI) and run it silently."""
        dest_dir = os.path.join(tempfile.gettempdir(), "bravely-builder-dl")
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, MSYS_INSTALLER_FILE)
        task.set_stage("download")
        task.log("Downloading %s" % MSYS_INSTALLER_URL)
        if self.dry_run:
            task.log("[dry run] would download to %s and run: %s in --confirm-command --accept-messages --root %s" % (dest, dest, self.msys()))
            return True
        try:
            req = urllib.request.Request(MSYS_INSTALLER_URL, headers={"User-Agent": "bravely-builder"})
            with urllib.request.urlopen(req, timeout=60) as r, open(dest, "wb") as out:
                total = int(r.headers.get("Content-Length") or 0)
                got, last = 0, 0
                while True:
                    if task.cancel_requested:
                        task.log("cancelled")
                        return False
                    chunk = r.read(1 << 18)
                    if not chunk:
                        break
                    out.write(chunk)
                    got += len(chunk)
                    if total and got * 100 // total >= last + 10:
                        last = got * 100 // total
                        task.log("  %d%% (%d of %d MB)" % (last, got >> 20, total >> 20))
            task.log("Downloaded %d bytes, sha256 %s" % (os.path.getsize(dest), sha256_file(dest)))
        except Exception as e:
            task.log("ERROR: download failed: %s" % e)
            return False
        task.set_stage("install")
        task.log("Running the installer silently (a Windows permission prompt may appear).")
        argv = [dest, "in", "--confirm-command", "--accept-messages", "--root", self.msys().replace("\\", "/")]
        task.log("$ " + subprocess.list2cmdline(argv))
        try:
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            task.proc = proc
            for raw in iter(proc.stdout.readline, b""):
                task.log(raw.decode("utf-8", errors="replace").rstrip())
            proc.wait()
            task.proc = None
        except OSError as e:
            task.log("ERROR: %s" % e)
            return False
        ok = proc.returncode == 0 and os.path.isfile(self.msys_bash())
        task.log("Installer exit code %s; bash.exe %s" % (proc.returncode, "found" if ok else "missing"))
        return ok

    def collect_result(self, task):
        meta = task.meta
        if meta.get("target") == "windows":
            d = meta.get("dir") or os.path.join(self.repo, "dist-win")
            exe = os.path.join(d, "pokeemerald.exe")
            zp = os.path.join(self.repo, "pokeemerald-win.zip")
            ok = os.path.isfile(exe) and os.path.isdir(os.path.join(d, "app0"))
            return {"ok": ok, "target": "windows", "folder": d, "exe": exe, "zip": zp if os.path.isfile(zp) else None,
                    "problem": None if ok else "dist-win/pokeemerald.exe or app0 is missing",
                    "launch": ok, "savedir": os.path.join(d, "userdata", "saves"), "log": task.logfile}
        if meta.get("target") == "linux32":
            d = meta["dir"]
            dist = os.path.join(d, "build", "dist")
            launcher = os.path.join(d, "emerald-launch.sh")
            ok = os.path.isfile(os.path.join(dist, "pokeemerald")) and os.path.isfile(launcher)
            return {"ok": ok, "target": "linux32", "folder": dist, "launcher": launcher, "zip": None,
                    "problem": None if ok else "build/dist/pokeemerald or emerald-launch.sh is missing",
                    "launch": ok, "savedir": os.path.join(d, "userdata", "saves"), "log": task.logfile,
                    "container": task.meta.get("container")}
        return {"ok": True}

    def cancel(self):
        t = self.task
        if not t or t.state != "running":
            return False
        t.cancel_requested = True
        t.log("##### Cancel requested: stopping the whole process tree")
        kill_tree(t.proc)
        return True

    def open_result(self, what):
        t = self.task
        res = t.result if t else None
        if not res or not res.get("ok"):
            raise ValueError("There is no finished build to open.")
        if what == "folder":
            path = res["folder"]
            if os.name == "nt":
                os.startfile(path)             # noqa: path comes from our own result, not from the request
            else:
                subprocess.Popen(["xdg-open", path], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            return "Opened %s" % path
        if what == "launch":
            if os.name == "nt":
                subprocess.Popen([res["exe"]], cwd=res["folder"], close_fds=True,
                                 creationflags=getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
            else:
                subprocess.Popen([res["launcher"]], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            return "Started the game."
        raise ValueError("unknown action")


def install_windows_result(task, repo, out):
    """Copy repo/dist-win to the install folder. Existing saves and settings are never overwritten."""
    src = os.path.join(repo, "dist-win")
    if not os.path.isfile(os.path.join(src, "pokeemerald.exe")):
        raise RuntimeError("dist-win/pokeemerald.exe is missing; the package step did not finish")
    n = kept = 0
    for root, _, files in os.walk(src):
        rel_root = os.path.relpath(root, src)
        os.makedirs(os.path.normpath(os.path.join(out, rel_root)), exist_ok=True)
        for fn in files:
            rel = os.path.normpath(os.path.join(rel_root, fn)).replace("\\", "/")
            dst = os.path.join(out, rel)
            if os.path.exists(dst) and (rel.startswith("userdata/saves/") or rel == "userdata/settings.txt"):
                kept += 1
                continue
            shutil.copy2(os.path.join(root, fn), dst)
            n += 1
    os.makedirs(os.path.join(out, "userdata", "saves"), exist_ok=True)
    task.log("copied %d files to %s (%d existing save/settings files kept)" % (n, out, kept))
