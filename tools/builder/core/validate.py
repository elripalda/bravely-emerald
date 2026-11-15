"""Input validation: paths, the ROM check (mirror of fetch_assets.sh), disk, network."""
import hashlib
import os
import re
import shutil
import socket

from .constants import ROM_SIZE, ROM_TITLE, EMERALD_US_SHA1


# ---------------------------------------------------------------------------------------------
def clean_path(p, what="path", allow_empty=False, no_space=True):
    """Validate a path received from the UI. Returns the normalized string or raises ValueError."""
    if not isinstance(p, str):
        raise ValueError("%s must be a string" % what)
    p = p.strip().strip('"')
    if not p:
        if allow_empty:
            return ""
        raise ValueError("%s is empty" % what)
    if len(p) > 400 or any(ord(c) < 32 for c in p):
        raise ValueError("%s has invalid characters" % what)
    if re.search(r"[\"'`$;&|<>*?!]", p):
        raise ValueError("%s contains characters the build scripts cannot handle (quotes or shell symbols)" % what)
    if no_space and " " in p:
        raise ValueError("%s contains a space; the build scripts need a path without spaces" % what)
    if not os.path.isabs(os.path.expanduser(p)):
        raise ValueError("%s must be an absolute path" % what)
    return os.path.normpath(os.path.expanduser(p))


def validate_rom(path):
    """Mirror of the check in fetch_assets.sh (size, title, game code, header checksum, SHA-1).
    Returns dict(ok, code, message, level). Reads the file only; never copies it."""
    try:
        path = clean_path(path, "ROM path", no_space=False)
    except ValueError as e:
        return {"ok": False, "message": str(e)}
    if not os.path.isfile(path):
        return {"ok": False, "message": "File not found: %s" % path}
    try:
        size = os.path.getsize(path)
        if size != ROM_SIZE:
            return {"ok": False, "message": "Not an Emerald dump: size is %d bytes, expected %d (16 MiB). "
                    "A truncated, trimmed or different file." % (size, ROM_SIZE)}
        with open(path, "rb") as f:
            f.seek(0xA0)
            head = f.read(0x20)          # 0xA0 .. 0xBF
            f.seek(0)
            h = hashlib.sha1()
            while True:
                chunk = f.read(1 << 20)
                if not chunk:
                    break
                h.update(chunk)
    except OSError as e:
        return {"ok": False, "message": "Cannot read the file: %s" % e}
    title = head[0:12].rstrip(b"\0")
    code = head[12:16]
    if title != ROM_TITLE:
        return {"ok": False, "message": "Not Pokemon Emerald: the header title is %r." % title.decode("latin-1")}
    if not (code[:3] == b"BPE" and len(code) == 4):
        return {"ok": False, "message": "Not Pokemon Emerald: the game code is %r, expected BPE?." % code.decode("latin-1")}
    total = sum(head[0:29])                      # bytes 0xA0..0xBC
    expected = (0 - (total + 25)) & 255
    if expected != head[29]:                     # byte 0xBD
        return {"ok": False, "message": "The ROM header checksum is wrong: the dump is modified or damaged."}
    sha = h.hexdigest()
    if sha == EMERALD_US_SHA1:
        return {"ok": True, "code": code.decode(), "message": "Pokemon Emerald (%s), USA/Europe 1.0: verified (SHA-1 matches)." % code.decode(),
                "sha1_match": True}
    return {"ok": True, "code": code.decode(), "message": "Pokemon Emerald (%s): header verified. Another language or revision "
            "than USA/Europe 1.0; any language works." % code.decode(), "sha1_match": False}


def free_gb(path):
    probe = path
    while probe and not os.path.exists(probe):
        nxt = os.path.dirname(probe)
        if nxt == probe:
            break
        probe = nxt
    return shutil.disk_usage(probe or ".").free / (1024 ** 3)


def network_ok():
    try:
        socket.create_connection(("github.com", 443), timeout=6).close()
        return True
    except OSError:
        return False

def fmt_secs(s):
    s = int(s)
    return "%d:%02d:%02d" % (s // 3600, s % 3600 // 60, s % 60)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()
