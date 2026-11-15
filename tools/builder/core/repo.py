"""Finding the repository that holds the build scripts."""
import os
import sys


def is_repo(path):
    """True when `path` is a checkout with the scripts the builder drives."""
    if not path or not os.path.isfile(os.path.join(path, "tools", "fetch_assets.sh")):
        return False
    # the Linux script or the Windows scripts (older checkouts have only the Windows ones)
    return (os.path.isfile(os.path.join(path, "tools", "deck", "build_deck.sh")) or
            os.path.isfile(os.path.join(path, "tools", "windows", "build.sh")))


def guess_repo():
    """Source checkout: two folders above tools/builder. Packaged app: the folder holding the app, its
    parent, or a 'bravely-emerald' folder next to it. Returns '' when nothing fits (ask the user)."""
    cands = []
    if getattr(sys, "frozen", False):
        here = os.path.dirname(os.path.abspath(sys.executable))
        cands += [here, os.path.dirname(here), os.path.join(here, "bravely-emerald"), os.path.join(os.path.dirname(here), "bravely-emerald")]
    else:
        cands.append(os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")))
    cands.append(os.path.join(os.path.expanduser("~"), "bravely-emerald"))
    for c in cands:
        if is_repo(c):
            return c
    return ""
