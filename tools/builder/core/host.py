"""Host detection."""
import os
import platform

# ---------------------------------------------------------------------------------------------
def read_os_release():
    out = {}
    for path in ("/etc/os-release", "/usr/lib/os-release"):
        try:
            with open(path, encoding="utf-8") as f:
                for line in f:
                    if "=" in line:
                        k, v = line.rstrip("\n").split("=", 1)
                        out[k] = v.strip().strip('"')
            break
        except OSError:
            continue
    return out


def detect_host():
    """Return dict(kind=windows|steamos|linux|other, label, arch). BRAVELY_FAKE_HOST is for tests."""
    fake = os.environ.get("BRAVELY_FAKE_HOST")
    if fake in ("windows", "steamos", "linux"):
        return {"kind": fake, "label": "%s (simulated, dry run only)" % fake, "arch": "x86_64", "fake": True}
    s = platform.system()
    arch = platform.machine().lower()
    if s == "Windows":
        return {"kind": "windows", "label": "Windows %s" % platform.release(), "arch": arch, "fake": False}
    if s == "Linux":
        osr = read_os_release()
        name = osr.get("PRETTY_NAME") or osr.get("NAME") or "Linux"
        if osr.get("ID") == "steamos" or osr.get("VARIANT_ID") == "steamdeck":
            return {"kind": "steamos", "label": "SteamOS / Steam Deck (%s)" % name, "arch": arch, "fake": False,
                    "id_like": osr.get("ID_LIKE", "")}
        return {"kind": "linux", "label": name, "arch": arch, "fake": False, "id_like": osr.get("ID", "") + " " + osr.get("ID_LIKE", "")}
    return {"kind": "other", "label": "%s (unsupported)" % s, "arch": arch, "fake": False}
