"""Nothing ends silently: every uncaught error (main thread, worker thread, Qt) is written to crash.log in the app home
and handed to a callback that shows a message box. No Qt import here, so the tests and the CLI can use it."""
import os
import platform
import sys
import tempfile
import threading
import time
import traceback

_state = {"callback": None, "busy": False}


def crash_log_path():
    try:
        from .settings import app_home
        home = app_home()
        os.makedirs(home, exist_ok=True)
        probe = os.path.join(home, ".w")
        with open(probe, "a"):
            pass
        os.remove(probe)
    except Exception:
        home = tempfile.gettempdir()
    return os.path.join(home, "crash.log")


def write_crash(text):
    """Append to crash.log (a read-only or full disk falls back to the temp folder). Returns the path used, or None."""
    for path in (crash_log_path(), os.path.join(tempfile.gettempdir(), "bravely-builder-crash.log")):
        try:
            with open(path, "a", encoding="utf-8", errors="replace") as f:
                f.write(text + "\n")
            return path
        except OSError:
            continue
    return None


def describe(etype, value, tb, where):
    head = "==== %s  thread: %s ====\nPython %s, %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), where, sys.version.split()[0], platform.platform())
    return head + "".join(traceback.format_exception(etype, value, tb))


def report(etype, value, tb, where="main"):
    """Log one error and call the callback. Never raises. Returns (text, path)."""
    if _state["busy"]:                                  # an error while reporting an error: log only
        text = describe(etype, value, tb, where)
        return text, write_crash(text)
    _state["busy"] = True
    try:
        text = describe(etype, value, tb, where)
        path = write_crash(text)
        cb = _state["callback"]
        if cb:
            try:
                cb(text, path)
            except Exception:
                write_crash("the crash dialog itself failed:\n" + traceback.format_exc())
        else:
            try:
                sys.stderr.write(text)
            except Exception:
                pass
        return text, path
    finally:
        _state["busy"] = False


def install(callback=None):
    """sys.excepthook, threading.excepthook and sys.unraisablehook. callback(text, path) is called from any thread."""
    _state["callback"] = callback
    sys.excepthook = lambda t, v, tb: report(t, v, tb, "main")
    threading.excepthook = lambda a: report(a.exc_type, a.exc_value, a.exc_traceback, a.thread.name if a.thread else "worker")
    sys.unraisablehook = lambda u: report(type(u.exc_value), u.exc_value, u.exc_traceback, "unraisable: %s" % (u.err_msg or ""))


def qt_message_handler(mode, context, message):
    """For QtCore.qInstallMessageHandler: critical and fatal Qt messages go to crash.log."""
    name = str(getattr(mode, "name", mode))
    if "Critical" in name or "Fatal" in name:
        write_crash("==== %s  Qt %s ====\n%s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), name, message))
    try:
        sys.stderr.write(message + "\n")
    except Exception:
        pass


def guarded(fn, where="worker"):
    """Wrap a thread target: an exception is reported instead of vanishing with the thread."""
    def run(*a, **kw):
        try:
            return fn(*a, **kw)
        except BaseException:
            report(*sys.exc_info(), where=where)
    return run
