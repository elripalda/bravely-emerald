#!/bin/bash
# Builds static 32-bit SDL2 2.32.10 and SDL2_image 2.8.12 for the Windows build (MSYS2 MINGW32
# shell: the mingw32 repo has no SDL2 package). Sources come from the official libsdl-org
# releases and are cached in build-win/src. Result: build-win/deps (prefix).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
W="$ROOT/build-win"
P="$W/deps"
mkdir -p "$W/src" "$P"
cd "$W/src"
SDL=SDL2-2.32.10
IMG=SDL2_image-2.8.12
[ -f $SDL.tar.gz ] || curl -fsSLO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/$SDL.tar.gz
[ -f $IMG.tar.gz ] || curl -fsSLO https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/$IMG.tar.gz
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
echo "deps ready: $P"
