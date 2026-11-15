#!/bin/bash
# Windows build (32-bit MinGW). Run in an MSYS2 MSYS shell (not MINGW32): CMakeLists.txt calls
# /bin/sh, so it needs the MSYS cmake. build.bat starts this script. See docs/WINDOWS_BUILD.md.
#
#   tools/windows/build.sh [--clean]
#
# Needs: the game files (fetch_assets.sh, once), MSYS2 packages listed in docs/WINDOWS_BUILD.md.
# Result: build-win/out/dist (game folder), then tools/windows/package.sh makes dist-win and the zip.
set -eo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
JOBS=$(nproc 2>/dev/null || echo 4)
BUILD_DIR="build-win/out"
step() { echo; echo "=== $* ==="; }
die() { echo "ERROR: $*" >&2; exit 1; }

[ "${1:-}" = "--clean" ] && rm -rf "$BUILD_DIR"

# i686 compiler first, then the 64-bit one (host tools, Python with Pillow), then MSYS
export PATH="/mingw32/bin:/mingw64/bin:/usr/bin"
command -v i686-w64-mingw32-gcc >/dev/null || die "mingw-w64-i686-toolchain missing: pacman -S mingw-w64-i686-toolchain"
command -v cmake >/dev/null || die "pacman -S cmake (MSYS package)"
[ "$(i686-w64-mingw32-gcc -dumpmachine)" = "i686-w64-mingw32" ] || die "wrong compiler"

if [ ! -f .assets_ok ] || [ ! -f graphics/pokemon/bulbasaur/front.png ] || [ ! -f assets/ui2/ui_atlas.bin ]; then
    die "the game files are missing: run ./fetch_assets.sh <your Pokemon Emerald .gba> first (docs/BUILD_YOUR_OWN.md)"
fi

if [ ! -x tools/preproc/preproc.exe ] || [ ! -x tools/gbagfx/gbagfx.exe ]; then
    step "Building the host tools"
    PATH="/mingw64/bin:/usr/bin" make -f make_tools.mk -j"$JOBS" CC="gcc -static" CXX="g++ -static"
fi

step "Converting the game files"
PATH="/mingw64/bin:/usr/bin" make -f vita/assets.mk -j"$JOBS"

if [ ! -f assets/ui/intro_margins.bin ]; then
    step "Intro margins"
    python tools/ui_gen/gen_intro_margins.py --output assets/ui/intro_margins.bin
fi

if [ ! -f build-win/deps/lib/libSDL2.a ] || [ ! -f build-win/deps/lib/libSDL2_image.a ]; then
    step "SDL2 and SDL2_image (static, 32-bit)"
    MSYSTEM=MINGW32 bash -lc "bash '$ROOT/tools/windows/build_deps.sh'"
fi

step "Configuring CMake in $BUILD_DIR"
. "$ROOT/tools/windows/lib_cache.sh"
drop_foreign_build "$BUILD_DIR" "$ROOT"
cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" -DWINDOWS=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/mingw32/bin/gcc.exe -DCMAKE_MAKE_PROGRAM=/usr/bin/make.exe

step "Building pokeemerald.exe"
cmake --build "$BUILD_DIR" -j"$JOBS"
echo "=== Built: $ROOT/$BUILD_DIR/dist/pokeemerald.exe ==="
