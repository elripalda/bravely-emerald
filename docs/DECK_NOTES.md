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
