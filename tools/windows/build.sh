#!/bin/bash
# Windows build (32-bit MinGW). Run in an MSYS2 MSYS shell (not MINGW32): CMakeLists.txt calls
# /bin/sh, so it needs the MSYS cmake. build.bat starts this script. See docs/WINDOWS_BUILD.md.
#
#   tools/windows/build.sh [--clean]
#
# Needs: the game files (tools/fetch_assets.sh, once), MSYS2 packages listed in docs/WINDOWS_BUILD.md.
# Result: build-win/out/dist (game folder), then tools/windows/package.sh makes dist-win and the zip.
# Every part that is already done is skipped with a line saying so; a stop always prints the line it stopped at.
set -eEo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
. "$ROOT/tools/windows/lib_cache.sh"
bravely_trap build.sh
JOBS=$(nproc 2>/dev/null || echo 4)
BUILD_DIR="build-win/out"
step() { echo; echo "=== $* ==="; }
have() { echo "already done: $*"; }

[ "${1:-}" = "--clean" ] && rm -rf "$BUILD_DIR"

# i686 compiler first, then the 64-bit one (host tools, Python with Pillow), then MSYS
# BRAVELY_STUBS (tests only): a folder of stand-in tools that comes first, so the script can be run without MSYS2
STUBS="${BRAVELY_STUBS:+$BRAVELY_STUBS:}"
export PATH="${STUBS}/mingw32/bin:/mingw64/bin:/usr/bin"
command -v i686-w64-mingw32-gcc >/dev/null || die "mingw-w64-i686-toolchain missing: pacman -S mingw-w64-i686-toolchain"
command -v cmake >/dev/null || die "pacman -S cmake (MSYS package)"
[ "$(i686-w64-mingw32-gcc -dumpmachine)" = "i686-w64-mingw32" ] || die "wrong compiler"

if [ ! -f .assets_ok ] || [ ! -f graphics/pokemon/bulbasaur/front.png ] || [ ! -f assets/ui2/ui_atlas.bin ]; then
    die "the game files are missing: run ./tools/fetch_assets.sh <your Pokemon Emerald .gba> first (docs/BUILD.md)"
fi
have "game files (.assets_ok)"

if [ ! -x tools/preproc/preproc.exe ] || [ ! -x tools/gbagfx/gbagfx.exe ]; then
    step "Building the host tools"
    PATH="${STUBS}/mingw64/bin:/usr/bin" make -f make_tools.mk -j"$JOBS" CC="gcc -static" CXX="g++ -static"
else
    have "host tools (preproc, gbagfx) are built"
fi

step "Converting the game files (make only redoes what changed)"
PATH="${STUBS}/mingw64/bin:/usr/bin" make -f vita/assets.mk -j"$JOBS"

if [ ! -f assets/ui/intro_margins.bin ]; then
    step "Intro margins"
    python tools/ui_gen/gen_intro_margins.py --output assets/ui/intro_margins.bin
else
    have "intro margins"
fi

DEPS="$(deps_state build-win/deps)"
if [ "$DEPS" = "present" ]; then
    have "SDL2 $SDL2_VERSION and SDL2_image $SDL2_IMAGE_VERSION are built (build-win/deps)"
else
    step "SDL2 and SDL2_image (static, 32-bit): $DEPS"
    if [ -n "${BRAVELY_STUBS:-}" ]; then        # tests only: no login shell, so the stand-in tools stay first on PATH
        PATH="${STUBS}$PATH" bash "$ROOT/tools/windows/build_deps.sh"
    else
        MSYSTEM=MINGW32 bash -lc "bash '$ROOT/tools/windows/build_deps.sh'"
    fi
fi

step "Configuring CMake in $BUILD_DIR"
drop_foreign_build "$BUILD_DIR" "$ROOT"
run_logged build-win/last-configure.log cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" -DWINDOWS=ON -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/mingw32/bin/gcc.exe -DCMAKE_MAKE_PROGRAM=/usr/bin/make.exe

step "Building pokeemerald.exe"
run_logged build-win/last-compile.log cmake --build "$BUILD_DIR" -j"$JOBS"
echo "=== Built: $ROOT/$BUILD_DIR/dist/pokeemerald.exe ==="
