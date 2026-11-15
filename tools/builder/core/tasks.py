"""Task = a pipeline of subprocess steps with an event stream; Step = one subprocess."""
import os
import re
import signal
import subprocess
import threading
import time

class Step:
    def __init__(self, name, stage, argv, env=None, cwd=None, tolerate=None, stage_rules=None, note=None):
        self.name, self.stage, self.argv = name, stage, argv
        self.env, self.cwd, self.tolerate = env or {}, cwd, tolerate
        self.stage_rules = stage_rules or []       # [(regex, stage)] applied to each output line
        self.note = note


class Task:
    def __init__(self, kind, label, stages, logfile=None, dry_run=False):
        self.kind, self.label, self.stages = kind, label, stages
        self.events = []
        self.cond = threading.Condition()
        self.state = "running"        # running | done | failed | cancelled
        self.proc = None
        self.cancel_requested = False
        self.started = time.time()
        self.finished = None
        self.stage = None
        self.logfile = logfile
        self.dry_run = dry_run
        self.result = None
        self.hints = []
        self.listeners = []            # callables(event dict), called from the worker thread
        self.meta = {}
        self._fh = None
        if logfile:
            try:
                os.makedirs(os.path.dirname(logfile), exist_ok=True)
                self._fh = open(logfile, "a", encoding="utf-8", errors="replace")
            except OSError:
                self._fh = None

    def emit(self, type_, **kw):
        with self.cond:
            ev = {"i": len(self.events), "type": type_, "t": round(time.time() - self.started, 1)}
            ev.update(kw)
            self.events.append(ev)
            self.cond.notify_all()
        for fn in list(self.listeners):
            try:
                fn(ev)
            except Exception:
                pass
        if type_ == "log" and self._fh:
            try:
                self._fh.write(kw.get("text", "") + "\n")
                self._fh.flush()
            except OSError:
                pass

    def log(self, text):
        self.emit("log", text=text)

    def set_stage(self, stage):
        if stage and stage != self.stage:
            self.stage = stage
            self.emit("stage", stage=stage)

    def finish(self, state, **kw):
        self.state = state
        self.finished = time.time()
        self.emit("state", state=state, **kw)
        if self._fh:
            try:
                self._fh.close()
            except OSError:
                pass

    def summary(self):
        return {"kind": self.kind, "label": self.label, "state": self.state, "stage": self.stage,
                "stages": self.stages, "started": self.started, "finished": self.finished,
                "logfile": self.logfile, "result": self.result, "hints": self.hints,
                "events": len(self.events), "dry_run": self.dry_run}


def kill_tree(proc):
    """Kill a subprocess and all its children."""
    if proc is None or proc.poll() is not None:
        return
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True, timeout=30)
        else:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                return
            for _ in range(30):
                if proc.poll() is not None:
                    break
                time.sleep(0.1)
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass
