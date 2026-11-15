"""The text block for a GitHub issue: header (versions, system, plan table), then the full log with stdout and stderr, then the
FAILED line and the probable cause (the last two are written by Task.finish). The log file on disk has the same content."""
import ctypes
import os
import platform
import re
import shutil
import subprocess
import sys

from .constants import APP_VERSION, MSYS_PACKAGES


def ram_gb():
    try:
        if os.name == "nt":
            class MS(ctypes.Structure):
                _fields_ = [("l", ctypes.c_ulong), ("load", ctypes.c_ulong), ("total", ctypes.c_ulonglong), ("av", ctypes.c_ulonglong),
                            ("pt", ctypes.c_ulonglong), ("pa", ctypes.c_ulonglong), ("vt", ctypes.c_ulonglong), ("va", ctypes.c_ulonglong),
                            ("ve", ctypes.c_ulonglong)]
            m = MS()
            m.l = ctypes.sizeof(MS)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
            return m.total / 1024 ** 3
        with open("/proc/meminfo") as f:
            return int(re.search(r"MemTotal:\s+(\d+)", f.read()).group(1)) / 1024 ** 2
    except Exception:
        return 0.0


def cpu_name():
    try:
        if os.name != "nt":
            with open("/proc/cpuinfo") as f:
                m = re.search(r"model name\s*:\s*(.+)", f.read())
                if m:
                    return m.group(1).strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def pacman_versions(builder):
    """`pacman -Q` of the packages that matter (Windows with MSYS2 only). Empty string when it cannot be asked."""
    if builder.host["kind"] != "windows" or not os.path.isfile(builder.msys_bash()):
        return ""
    names = ["msys2-runtime", "pacman"] + [p for p in MSYS_PACKAGES if p.startswith("mingw-w64-") and p.endswith(("toolchain", "cmake", "gcc"))] \
        + ["make", "cmake", "python"]
    argv, env = builder.msys_cmd('pacman -Q "$@" 2>&1', names)
    try:
        p = subprocess.run(argv, env=dict(os.environ, **env), capture_output=True, text=True, timeout=60,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return p.stdout.strip()
    except (OSError, subprocess.TimeoutExpired) as e:
        return "pacman -Q failed: %s" % e


def header(builder, manifest=None, include_pacman=True):
    """Lines for the top of a log / issue."""
    free = 0.0
    try:
        free = shutil.disk_usage(builder.repo or ".").free / 1024 ** 3
    except OSError:
        pass
    m = manifest or {}
    lines = ["Bravely Emerald Builder %s" % APP_VERSION,
             "Bundled source: %s (%s)" % ((m.get("commit") or "none (a checkout is used)")[:12], (m.get("date") or "")[:10]),
             "System: %s, %s" % (builder.host.get("label"), platform.platform()),
             "CPU: %s, %d threads; RAM %.1f GB; free disk at the source folder %.1f GB" % (cpu_name(), os.cpu_count() or 0, ram_gb(), free),
             "Source folder: %s" % builder.repo,
             "Python %s%s" % (sys.version.split()[0], " (frozen app)" if getattr(sys, "frozen", False) else "")]
    if builder.host["kind"] == "windows":
        root, how = builder.msys_root_found()
        lines.append("MSYS2: %s (%s)" % (root, how or "not installed"))
        if include_pacman:
            pv = pacman_versions(builder)
            if pv:
                lines.append("pacman -Q:")
                lines += ["  " + ln for ln in pv.splitlines()]
    try:
        lines.append("Plan (what was found on this PC before the build):")
        lines += ["  " + ln for ln in builder.plan_text().splitlines()]
    except Exception as e:                       # a broken probe must not hide the log
        lines.append("Plan: could not be made (%r)" % (e,))
    return lines


def full_report(task, builder=None, manifest=None):
    """Header + every log line of the task (stdout and stderr were merged by the runner) + the FAILED footer already in the log.
    The header is written to the log by the runner when it started, so it is only added here when the log has none."""
    log = [e["text"] for e in task.events if e["type"] == "log"]
    out = []
    if builder is not None and not (log and log[0].startswith("Bravely Emerald Builder")):
        out += header(builder, manifest, include_pacman=False) + [""]
    out += log
    return "```\n" + "\n".join(out) + "\n```\n"
