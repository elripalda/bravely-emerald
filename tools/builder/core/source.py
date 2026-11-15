"""The project source bundled inside the app, and how it is extracted and chosen as the repository.

Packaging (package.py) stores a snapshot of the tracked files of one commit as `bundle/source.tar.xz` next to a
`bundle/source-manifest.json` (commit, date, file count, sha256 of the archive). At run time the app extracts it
into a work folder and builds from there. A user's own checkout is never modified: it is only read, and only
when the user chooses it under Advanced.
"""
import hashlib
import json
import os
import shutil
import sys
import tarfile
import tempfile
import time

from .repo import is_repo, guess_repo
from .validate import source_path_problem
from .settings import load_settings

MANIFEST_NAME = "source-manifest.json"
ARCHIVE_NAME = "source.tar.xz"
MARKER = ".bravely-source.json"          # in the work folder: what is extracted there, and whether it finished
FILELIST = ".bravely-source-files.txt"
MAX_TOTAL_BYTES = 1 << 30                  # refuse an archive that unpacks to more than 1 GiB (the real one is about 85 MB)

# Paths left out of the snapshot because no build needs them (a real build proves the list, see docs/BUILDER.md).
# Prefix match on the repository path. design/stitch/_tools is kept: fetch_assets.sh runs export_assets.py from it.
EXCLUDE_PREFIXES = ("docs/", "design/", "tools/builder/", ".github/", "README.md", "CONTRIBUTING.md", "build_vita.ps1",
                    "build_vita.sh", "build_all.ps1", "build_all.sh", "fetch_assets.ps1")
KEEP_PREFIXES = ("docs/licenses/", "design/stitch/_tools/")


class SourceError(Exception):
    pass


def excluded(path):
    """True when a repository path (forward slashes) is left out of the snapshot."""
    if path.startswith(KEEP_PREFIXES):
        return False
    return path.startswith(EXCLUDE_PREFIXES)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def bundle_dir():
    """Folder with source.tar.xz and the manifest, or None. Packaged app: inside the app; source checkout: tools/builder/bundle."""
    cands = []
    if getattr(sys, "frozen", False):
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        cands += [os.path.join(base, "bundle"), os.path.join(os.path.dirname(sys.executable), "bundle")]
    cands.append(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "bundle"))
    for c in cands:
        if os.path.isfile(os.path.join(c, MANIFEST_NAME)) and os.path.isfile(os.path.join(c, ARCHIVE_NAME)):
            return c
    return None


def read_manifest(bdir):
    try:
        with open(os.path.join(bdir, MANIFEST_NAME), encoding="utf-8") as f:
            m = json.load(f)
    except (OSError, ValueError) as e:
        raise SourceError("the bundled manifest cannot be read: %s" % e)
    for k in ("commit", "date", "files", "sha256", "archive"):
        if k not in m:
            raise SourceError("the bundled manifest has no '%s'" % k)
    return m


def default_work_dir():
    if os.environ.get("BRAVELY_BUILDER_HOME"):
        return os.path.join(os.environ["BRAVELY_BUILDER_HOME"], "source")
    if os.name == "nt":
        path = os.path.join(os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), "AppData", "Local"),
                            "BravelyEmerald", "source")
        if source_path_problem(path):            # a space or an accent in the user name: use a plain folder on the system drive
            path = os.path.join(os.environ.get("SystemDrive", "C:") + os.sep, "BravelyEmerald", "source")
        return path
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(base, "bravely-emerald", "source")


def read_marker(work):
    try:
        with open(os.path.join(work, MARKER), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def needs_extract(work, manifest):
    m = read_marker(work)
    return not (m and m.get("complete") and m.get("sha256") == manifest["sha256"] and m.get("commit") == manifest["commit"]
                and is_repo(work))


def _safe_target(root, name):
    """Join a member name onto root; reject absolute paths, drive letters, '..' and anything leaving root."""
    if not name or name.startswith(("/", "\\")) or (len(name) > 1 and name[1] == ":") or "\x00" in name:
        raise SourceError("unsafe path in the archive: %r" % name)
    parts = name.replace("\\", "/").split("/")
    if ".." in parts:
        raise SourceError("unsafe path in the archive: %r" % name)
    target = os.path.normpath(os.path.join(root, *parts))
    root_n = os.path.normpath(root)
    if target != root_n and not target.startswith(root_n + os.sep):
        raise SourceError("unsafe path in the archive: %r" % name)
    return target


def extract_archive(archive, dest, progress=None):
    """Extract regular files and folders of a tar into dest (created). Links, devices and unsafe names are refused."""
    total = 0
    n = 0
    with tarfile.open(archive, "r:xz") as t:
        for m in t:
            if m.isdir():
                os.makedirs(_safe_target(dest, m.name), exist_ok=True)
                continue
            if not m.isfile():
                raise SourceError("the archive holds a link or special file (%r); refusing to extract it" % m.name)
            total += m.size
            if total > MAX_TOTAL_BYTES:
                raise SourceError("the archive unpacks to more than %d bytes; refusing" % MAX_TOTAL_BYTES)
            target = _safe_target(dest, m.name)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            src = t.extractfile(m)
            with open(target, "wb") as out:
                shutil.copyfileobj(src, out, 1 << 20)
            n += 1
            if progress and n % 500 == 0:
                progress("extracted %d files" % n)
    return n


def _same_file(a, b):
    try:
        if os.path.getsize(a) != os.path.getsize(b):
            return False
        return sha256_file(a) == sha256_file(b)
    except OSError:
        return False


def _write_marker(work, manifest, complete):
    tmp = os.path.join(work, MARKER + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"commit": manifest["commit"], "date": manifest["date"], "sha256": manifest["sha256"], "files": manifest["files"],
                   "complete": complete, "when": time.strftime("%Y-%m-%dT%H:%M:%S")}, f, indent=2)
    os.replace(tmp, os.path.join(work, MARKER))


def ensure_extracted(bdir, work, force=False, progress=None):
    """Make `work` hold the bundled source. Returns True if something was extracted, False if it was already current.

    - the archive hash is checked against the manifest before anything is written;
    - a first extraction goes to a staging folder that is renamed into place (atomic);
    - an update overlays the existing tree (so build-win/, .assets_ok and downloaded files survive), rewriting only
      files whose content changed and removing files the old snapshot had and the new one does not; the marker says
      'incomplete' until the end, so an interrupted run is simply repeated (resumable).
    """
    manifest = read_manifest(bdir)
    archive = os.path.join(bdir, manifest["archive"])
    if os.path.basename(manifest["archive"]) != manifest["archive"]:
        raise SourceError("the manifest names an archive outside the bundle")
    if not force and not needs_extract(work, manifest):
        return False
    if not os.path.isfile(archive):
        raise SourceError("the bundled archive is missing: %s" % archive)
    if progress:
        progress("checking the bundled archive")
    got = sha256_file(archive)
    if got != manifest["sha256"]:
        raise SourceError("the bundled archive does not match its manifest (sha256 %s, expected %s); reinstall the app" % (got[:12], manifest["sha256"][:12]))
    parent = os.path.dirname(os.path.abspath(work))
    os.makedirs(parent, exist_ok=True)
    stage = tempfile.mkdtemp(prefix=".bravely-stage-", dir=parent)
    try:
        if progress:
            progress("extracting the source (%s files)" % manifest["files"])
        extract_archive(archive, stage, progress)
        if not is_repo(stage):
            raise SourceError("the extracted source has no build scripts; the bundle is damaged")
        files = []
        for root, _, fs in os.walk(stage):
            for fn in fs:
                files.append(os.path.relpath(os.path.join(root, fn), stage).replace("\\", "/"))
        if not os.path.isdir(work) or not os.listdir(work):
            if os.path.isdir(work):
                os.rmdir(work)
            with open(os.path.join(stage, FILELIST), "w", encoding="utf-8") as f:
                f.write("\n".join(sorted(files)) + "\n")
            os.rename(stage, work)                         # atomic for a first extraction
            _write_marker(work, manifest, True)
            return True
        # update of an existing work folder (only one this app made: it has the marker)
        if read_marker(work) is None:
            raise SourceError("%s is not empty and was not made by this app; choose an empty or new source folder" % work)
        _write_marker(work, manifest, False)
        old = set()
        try:
            with open(os.path.join(work, FILELIST), encoding="utf-8") as f:
                old = {ln.strip() for ln in f if ln.strip()}
        except OSError:
            pass
        for rel in files:
            src = os.path.join(stage, rel)
            dst = _safe_target(work, rel)
            if os.path.isfile(dst) and _same_file(src, dst):
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            os.replace(src, dst)
        for rel in old - set(files):
            try:
                os.remove(_safe_target(work, rel))
            except OSError:
                pass
        with open(os.path.join(work, FILELIST), "w", encoding="utf-8") as f:
            f.write("\n".join(sorted(files)) + "\n")
        _write_marker(work, manifest, True)
        return True
    finally:
        shutil.rmtree(stage, ignore_errors=True)


class Source:
    """Which repository the app builds from, and its description for the Setup page."""

    def __init__(self, own_checkout=None, work_dir=None, force_repo=None):
        s = load_settings()
        self.force_repo = force_repo                       # --repo on the command line: wins over everything
        self.own = own_checkout if own_checkout is not None else s.get("own_checkout", "")
        self.work = work_dir or s.get("source_dir") or default_work_dir()
        self.bundle = bundle_dir()
        self.manifest = read_manifest(self.bundle) if self.bundle else None

    @property
    def mode(self):
        if self.force_repo and is_repo(self.force_repo):
            return "repo-arg"
        if self.own and is_repo(self.own):
            return "own"
        if self.manifest:
            return "bundled"
        if guess_repo():
            return "checkout"
        return "none"

    def repo_path(self):
        m = self.mode
        return {"repo-arg": self.force_repo, "own": self.own, "bundled": self.work, "checkout": guess_repo()}.get(m, "")

    def describe(self):
        m = self.mode
        if m == "bundled":
            d = self.manifest
            return "Bundled source: %s, %s (%s files)" % (d["commit"][:10], d["date"][:10], d["files"])
        if m in ("own", "repo-arg", "checkout"):
            return "Using the checkout %s (never modified by the app)" % self.repo_path()
        return "No source available: this app has no bundled source and no checkout was found. Use Advanced to pick a checkout."

    def prepare(self, force=False, progress=None):
        """Make the chosen repository usable (extract the bundle when it is the bundled one). Returns its path."""
        if self.mode == "bundled":
            ensure_extracted(self.bundle, self.work, force, progress)
        path = self.repo_path()
        if not path:
            raise SourceError(self.describe())
        return path


def help_file():
    """The bundled help page (Markdown), or None. Packaged app: help/BUILDER_HELP.md; source checkout: docs/BUILDER_HELP.md."""
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cands = []
    if getattr(sys, "frozen", False):
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        cands.append(os.path.join(base, "help", "BUILDER_HELP.md"))
    cands.append(os.path.join(here, "help", "BUILDER_HELP.md"))
    cands.append(os.path.join(here, "..", "..", "docs", "BUILDER_HELP.md"))
    for c in cands:
        if os.path.isfile(c):
            return os.path.abspath(c)
    return None
