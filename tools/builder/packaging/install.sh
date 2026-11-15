#!/bin/sh
# Installs the Bravely Emerald builder for the current user (no root): copies the app to
# ~/.local/share/bravely-emerald-builder, links it into ~/.local/bin and adds a menu entry.
# Run it from the unpacked folder:  sh install.sh      Remove it:  sh install.sh --uninstall
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$HOME/.local/share/bravely-emerald-builder"
BIN="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"
if [ "${1:-}" = "--uninstall" ]; then
    rm -rf "$DEST" "$BIN/bravely-emerald-builder" "$APPS/bravely-emerald-builder.desktop"
    echo "Removed."
    exit 0
fi
[ -x "$HERE/BravelyEmeraldBuilder/BravelyEmeraldBuilder" ] || { echo "Run this script from the unpacked archive folder." >&2; exit 1; }
rm -rf "$DEST"
mkdir -p "$DEST" "$BIN" "$APPS"
cp -a "$HERE/BravelyEmeraldBuilder/." "$DEST/"
ln -sf "$DEST/BravelyEmeraldBuilder" "$BIN/bravely-emerald-builder"
sed "s|@EXEC@|$DEST/BravelyEmeraldBuilder|; s|@PATH@|$DEST|" "$HERE/bravely-emerald-builder.desktop.in" > "$APPS/bravely-emerald-builder.desktop"
chmod +x "$APPS/bravely-emerald-builder.desktop"
echo "Installed in $DEST"
echo "Start it from the application menu (Games: Bravely Emerald builder) or run: bravely-emerald-builder"
