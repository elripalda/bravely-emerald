#!/bin/bash
# Builds static 32-bit SDL2 2.32.10 and SDL2_image 2.8.12 for the Windows build (MSYS2 MINGW32
# shell: the mingw32 repo has no SDL2 package). Sources come from the official libsdl-org
# releases and are cached in build-win/src. Result: build-win/deps (prefix).
set -eEuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/tools/windows/lib_cache.sh"
bravely_trap build_deps.sh
W="$ROOT/build-win"
P="$W/deps"
case "$(deps_state "$P")" in
    present) echo "already done: SDL2 $SDL2_VERSION and SDL2_image $SDL2_IMAGE_VERSION are built in $P"; exit 0 ;;
    outdated) echo "the SDL2 build in $P is half done or another version: starting it again"
              rm -rf "$P" "$W/src/SDL2-$SDL2_VERSION/build" "$W/src/SDL2_image-$SDL2_IMAGE_VERSION/build" ;;
esac
mkdir -p "$W/src" "$P"
cd "$W/src"
SDL=SDL2-$SDL2_VERSION
IMG=SDL2_image-$SDL2_IMAGE_VERSION
# fetch <file> <url>: download once; a leftover partial or damaged archive is thrown away and fetched again
fetch() {
    if [ -f "$1" ] && tar tzf "$1" >/dev/null 2>&1; then
        echo "already downloaded: $1"
        return 0
    fi
    rm -f "$1" "$1.part"
    echo "downloading $1"
    rc=0
    curl -fsSL --retry 3 --retry-delay 5 -o "$1.part" "$2" || rc=$?
    if [ $rc -eq 60 ]; then
        # exit 60: MSYS2's own certificate bundle does not trust what the network presents (antivirus or a proxy that
        # re-signs HTTPS). That root is in the Windows store, so try again with it.
        echo "certificate not trusted by MSYS2; retrying with the Windows certificate store"
        rm -f "$1.part"
        curl -fsSL --ca-native --retry 3 --retry-delay 5 -o "$1.part" "$2"
    elif [ $rc -ne 0 ]; then
        exit $rc
    fi
    mv "$1.part" "$1"
}
fetch $SDL.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/$SDL.tar.gz
fetch $IMG.tar.gz https://github.com/libsdl-org/SDL_image/releases/download/release-$SDL2_IMAGE_VERSION/$IMG.tar.gz
[ -d $SDL ] || tar xf $SDL.tar.gz
[ -d $IMG ] || tar xf $IMG.tar.gz
cmake -S $SDL -B $SDL/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$P"
cmake --build $SDL/build
cmake --install $SDL/build
cmake -S $IMG -B $IMG/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH="$P" -DCMAKE_INSTALL_PREFIX="$P" -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_SAMPLES=OFF \
  -DSDL2IMAGE_TESTS=OFF -DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=OFF \
  -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_QOI=OFF
cmake --build $IMG/build
cmake --install $IMG/build
[ "$(deps_state "$P")" = present ] || die "the SDL2 build finished but $P is not complete"
echo "deps ready: $P"
