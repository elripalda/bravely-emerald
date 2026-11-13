# Steam Deck build container

Container `emerald-build` (distrobox, Arch Linux) on the Deck. Used for 32-bit builds.

## Storage

The SD-card variant failed. `distrobox create` succeeded but starting the container failed:

```
Error: unable to start container "...": crun: open `/run/media/deck/SD512/emerald-deck/containers/overlay/.../merged`: Permission denied: OCI permission denied
```

Fallback from the brief applied: `storage.conf` removed, container recreated on internal disk.
Podman graphroot is `/home/deck/.local/share/containers/storage` (internal NVMe, `/home`).
Do not retry the SD-card variant.

Leftover data from the failed SD attempt remains at `/run/media/deck/SD512/emerald-deck/containers` (about 415 MB). Some subdirectories are root-owned and unreadable by user `deck`.

## Commands that worked

```bash
# Step 2 (internal disk, no storage.conf)
ssh deck 'distrobox create --yes --name emerald-build --image docker.io/library/archlinux:latest'
ssh deck 'distrobox enter emerald-build -- echo container-ok'
```

The first create attempt (exit with `Installing basic packages... Error: An error occurred`) was a pacman sync timeout against `fastly.mirror.pkgbuild.com` and `geo.mirror.pkgbuild.com`. Retrying after removing the container worked. Mirrors answered from the host right after.

```bash
ssh deck 'distrobox rm --force emerald-build'
ssh deck 'distrobox create --yes --name emerald-build --image docker.io/library/archlinux:latest'
```

```bash
# Step 3: multilib + toolchain (guarded so a retry does not duplicate the multilib block)
distrobox enter emerald-build -- sudo bash -c 'grep -q "^\[multilib\]" /etc/pacman.conf || printf "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n" >> /etc/pacman.conf; pacman -Syu --noconfirm && pacman -S --noconfirm base-devel cmake git pkgconf lib32-glibc lib32-gcc-libs lib32-mesa lib32-libglvnd'
```

Note: `lib32-sdl2` and `lib32-sdl2_image` are not in Arch multilib (see below), so the brief's package list fails on those two names. The command above omits them.

## Verification (Step 4)

```bash
distrobox enter emerald-build -- bash -lc 'cd /tmp && cat > t.c <<EOF
#include <stdio.h>
int main(void){ printf("%zu\n", sizeof(void*)); return 0; }
EOF
gcc -m32 t.c -o t32 && ./t32'
```

Output: `4` (32-bit pointers). `file` reports `ELF 32-bit LSB pie executable, Intel i386`.

## Known gap: 32-bit SDL2

- `pacman -Ss "^lib32-sdl"` in multilib returns only `lib32-sdl3 3.4.20-1`. No `lib32-sdl2`, no `lib32-sdl2_image`.
- `PKG_CONFIG_PATH=/usr/lib32/pkgconfig pkg-config --cflags --libs sdl2 SDL2_image gl` fails: `Package 'sdl2' not found`, `Package 'SDL2_image' not found`.
- `gl` resolves (`/usr/lib32/pkgconfig/gl.pc`, `opengl.pc`, `libglvnd.pc` present).
- Open decision: build SDL2 + SDL2_image 32-bit from source, or use another source. Not done here.

## Task 2: Deck tool and asset build (2026-11-13)

Repo sync:
- Bare repo `/run/media/deck/SD512/emerald-deck/repo.git`. Local checkout is shallow, so the bare repo needs `receive.shallowUpdate true` (set once). Push `deck-port:main`, clone to `.../emerald-deck/src` (branch `main`).

Host tools (`make -f make_tools.mk -j8`):
- Built OK: preproc, gbagfx, scaninc, mid2agb, jsonproc.
- First run failed: `png.h` missing / `libpng` not found. Re-run passed with no change; `libpng 1.6.59-1` and `/usr/lib/pkgconfig/libpng.pc` present. Likely the other agent's concurrent install was still in progress.

Assets:
- `bash ./fetch_assets.sh` needs `bash`: the script has no exec bit in the clone (`Permission denied` on `./fetch_assets.sh`).
- ROM check: `Pokémon Emerald (BPEE), USA/Europe 1.0: verified`. Download + manifest check + copy of 9168 files passed.
- Then fails: `ERROR: Python 3 with Pillow is needed`. Container has `/usr/bin/python3` but no `PIL`. `python-pillow` is not in the Arch repos under that name (pacman -Q / pip not run; no install per rule). Needs `python-pillow` or `pip install pillow` by the user.
- Impact: "Generating the port's files" step (intro margins) not done.
- `make -f vita/assets.mk -j8`: OK, 9300 binary assets, 420 songs. `include/constants/map_groups.h` exists after this step.

Status: partial. Blocked on Pillow for fetch_assets.sh step 3.

## 32-bit SDL2 deps

Arch multilib has no lib32-sdl2. Built static -m32 SDL2 2.32.10 + SDL2_image 2.8.12 in container `emerald-build`.
Prefix: /run/media/deck/SD512/emerald-deck/deps32 ; sources: /run/media/deck/SD512/emerald-deck/build-deps

```
sudo pacman -S --needed --noconfirm lib32-alsa-lib lib32-libpulse lib32-libxcursor lib32-libxi lib32-libxrandr \
  lib32-libxss lib32-libxkbcommon lib32-libpng lib32-libxfixes lib32-libxrender lib32-libxtst lib32-pipewire \
  lib32-libdecor wayland-protocols wayland
cd /run/media/deck/SD512/emerald-deck/build-deps
curl -fsSLO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
curl -fsSLO https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/SDL2_image-2.8.12.tar.gz
tar xf SDL2-2.32.10.tar.gz; tar xf SDL2_image-2.8.12.tar.gz
export PKG_CONFIG_PATH=/usr/lib32/pkgconfig:/usr/share/pkgconfig PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig
P=/run/media/deck/SD512/emerald-deck/deps32
cd SDL2-2.32.10
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release \
  -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$P
cmake --build build -j8 && cmake --install build
cd ../SDL2_image-2.8.12
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH=$P -DCMAKE_INSTALL_PREFIX=$P -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
  -DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF \
  -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_QOI=OFF
cmake --build build -j8 && cmake --install build
# use:
PKG_CONFIG_PATH=$P/lib/pkgconfig pkg-config --cflags --libs --static sdl2 SDL2_image
```
SDL2 backends ON (dlopen at runtime): X11 (+xcursor/xinput/xrandr/xscrnsaver/xfixes), Wayland (+libdecor), KMSDRM, OpenGL, GLES, Vulkan, ALSA, PulseAudio, PipeWire, OSS. OFF: JACK, sndio, ESD, ARTS.
SDL2_image PNG comes from the bundled stb backend (no libpng link). Static libs; libSDL2.a dlopens X11/pulse/wayland, so the final link has only -lm -pthread (+ dl).
Test: SSH has no X auth; run with `XAUTHORITY=/run/user/1000/xauth_* DISPLAY=:0` (else SDL falls back to offscreen driver).
