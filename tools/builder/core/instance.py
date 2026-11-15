"""One copy of the app at a time (two builds in one work folder would corrupt each other). A lock on a file in the app home;
the operating system drops it when the process ends, even after a crash, so there is no stale lock to clean up."""
import os

MESSAGE = ("Bravely Emerald Builder is already running.\n\nLook for its window in the taskbar. If you cannot find it, "
           "open Task Manager, end 'BravelyEmeraldBuilder.exe' and start the app again.")


class InstanceLock:
    def __init__(self, home=None):
        if home is None:
            from .settings import app_home
            home = app_home()
        self.path = os.path.join(home, "builder.lock")
        self.fh = None

    def acquire(self):
        """True when this process now holds the lock. An unwritable home does not block the app (returns True)."""
        try:
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            fh = open(self.path, "a+")
        except (OSError, ValueError):
            return True
        try:
            if os.name == "nt":
                import msvcrt
                fh.seek(0)
                msvcrt.locking(fh.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            fh.close()
            return False
        self.fh = fh
        return True

    def release(self):
        if self.fh:
            try:
                if os.name == "nt":
                    import msvcrt
                    self.fh.seek(0)
                    msvcrt.locking(self.fh.fileno(), msvcrt.LK_UNLCK, 1)
                self.fh.close()
            except OSError:
                pass
            self.fh = None
