#!/bin/bash
# Packs the build into dist-win/ (the game folder) and pokeemerald-win.zip. Never copies saves:
# only settings.txt is seeded into userdata/. Run after build.sh (build.bat does both).
set -eo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
export PATH="/mingw64/bin:/usr/bin"
SRC="build-win/out/dist"
OUT="dist-win"
[ -f "$SRC/pokeemerald.exe" ] || { echo "ERROR: build first (tools/windows/build.sh)" >&2; exit 1; }
rm -rf "$OUT" pokeemerald-win.zip
mkdir -p "$OUT/userdata"
cp "$SRC/pokeemerald.exe" "$SRC/run.bat" "$OUT/"
cp -r "$SRC/app0" "$OUT/app0"
cp "$SRC/userdata/settings.txt" "$OUT/userdata/settings.txt"
cp tools/windows/README_WINDOWS.txt "$OUT/README.txt"
# SDL2 is linked statically; list any DLL the exe still needs besides the Windows ones
if command -v objdump >/dev/null; then
    echo "DLL imports of pokeemerald.exe:"
    objdump -p "$OUT/pokeemerald.exe" | grep "DLL Name" | sort -u
fi
python -m zipfile -c pokeemerald-win.zip "$OUT"
ls -l pokeemerald-win.zip
