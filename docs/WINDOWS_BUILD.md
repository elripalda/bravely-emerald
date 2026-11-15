# Windows build

A 32-bit Windows (x86) build of the Deck port: same sources, same `PLATFORM_DECK` effects (post
chain, shaders, sun shadow, UI polish, title and reveal flash, ambient audio), on desktop OpenGL.
Branch `deck-win`.

## Why 32-bit

The decompiled game's data is assembled with 4-byte pointers (`.long`, see CMakeLists.txt, the
`DECK` block), so the program must have 4-byte pointers: target `i686-w64-mingw32`. MSYS2's
MINGW32 repository still ships `mingw-w64-i686-gcc` (16.2.0 when this was built) but no SDL2
package, so SDL2 and SDL2_image are built from the official source (`tools/windows/build_deps.sh`,
the same versions the Deck uses: 2.32.10 and 2.8.12) as static libraries. Result: one exe, no DLLs
besides the ones Windows has.

## Rebuild

1. Install MSYS2 (https://www.msys2.org, installer to `C:\msys64`), then in the MSYS shell:
   ```sh
   pacman -Syu
   pacman -S --needed make git cmake tar patch diffutils python zip unzip \
       mingw-w64-i686-toolchain mingw-w64-i686-cmake mingw-w64-i686-ninja \
       mingw-w64-x86_64-gcc mingw-w64-x86_64-libpng mingw-w64-x86_64-pkgconf mingw-w64-x86_64-python-pillow
   ```
2. Game files (once, needs your own Emerald dump; this repository carries none):
   ```sh
   # MINGW64 shell
   FETCH_NO_CONVERT=1 ./tools/fetch_assets.sh /path/to/emerald.gba     # stops at "Generating the port's files": run step 3 first
   make -f make_tools.mk -j8 CC="gcc -static" CXX="g++ -static"
   make -f vita/assets.mk -j8
   ./tools/fetch_assets.sh /path/to/emerald.gba     # again: now it makes the UI atlas and .assets_ok
   ```
   (The fetch script's last step needs the graphics that `assets.mk` converts, so on a clean
   tree it has to run twice. The Deck flow hit the same ordering.)
3. `tools\windows\build.bat` (or `tools/windows/build.sh` then `tools/windows/package.sh` in the
   MSYS shell). It builds the host tools and SDL if missing, configures CMake with `-DWINDOWS=ON`,
   builds `build-win/out/pokeemerald.exe` and packs `dist-win/` and `pokeemerald-win.zip`.

Run `tools/windows/build.sh --clean` for a clean rebuild. Nothing of the game (ROM, graphics,
derived data) is committed; `build-win/`, `dist-win/` and the zip are git-ignored.

## What differs from the Deck build

| Area | Windows |
|---|---|
| CMake | `-DWINDOWS=ON` implies `DECK`; `-msse2` baseline (no Zen 2 flags); `-m i386pe` for `ld -r`; link `-static -lopengl32 ...`, `-mwindows` (no console) unless `-DWINDOWS_CONSOLE=ON`; `-Wl,--wrap=fopen` as on the Deck |
| Symbols | 32-bit COFF prefixes C symbols with `_`; the assembly data names them bare, so `game_data.o` gets `objcopy --prefix-symbols=_` |
| Game reset | PE has no `__start_`/`__stop_` for `gamedata`/`gamebss`: `vita_reset.c` finds the sections in the image's section table |
| OpenGL | `opengl32.dll` exports GL 1.1 only. `src/platform/deck/compat/win/GL/gl.h` (first on the include path) makes every newer call (buffers, framebuffers, mipmaps, programs, ...) a function pointer loaded by `WinGl_Load()` (win_gl.c) through `SDL_GL_GetProcAddress` right after the context is created. It also defines `WINGDIAPI`/`APIENTRY` so `windows.h` is not pulled in (it clashes with `ScrollWindow` and `abs` of the game). `deck_gl_shader.c` already loaded GL 2.0 by name; timer queries fail safe |
| Entry | `SDL_MAIN_HANDLED`, `SDL_SetMainReady()`, working directory set to the exe's folder; without a console `stderr` goes to `userdata/stderr.txt` |
| Time and sleep | `SDL_GetTicks64`, performance counter; `VitaPlatform_DelayMicroseconds` sleeps whole milliseconds and spins the rest; `mkdir`, `localtime_s` |
| Telemetry | `/proc` and `/sys` readers return -1, CPU time not read (optional) |
| Window | Resizable 1280x720 window; `DECK_FULLSCREEN=1` gives the Deck's desktop fullscreen |

## Output

`dist-win/`: `pokeemerald.exe`, `run.bat`, `README.txt`, `app0/` (voxel, ui, ui2, followers assets)
and `userdata/settings.txt`. No save is shipped. To continue a Deck save, copy
`userdata/saves/pokeemerald.sav` from the Deck install (`build-deck/dist/`) to `userdata\saves\`.

Debug env vars of the Deck work unchanged (`DECK_GL_DEBUG`, `DECK_SHOT`, `DECK_AUTOPLAY`, ...),
for example from a command prompt in the game folder:
`set DECK_AUTOPLAY=title.txt & set DECK_SHOT=12000:shots/a.png & pokeemerald.exe`
(create the `shots` folder first).

## Test results (2026-11-14, this PC)

AMD Radeon Graphics, vendor ATI Technologies, GL 4.6.14756 compatibility profile, GLSL 4.60,
Windows 10 LTSC 2021. Runs from a copy of the dist folder with `DECK_GL_DEBUG=1`, `DECK_SHOT`,
`DECK_AUTOPLAY` (all work on Windows).
- Title (title.txt): intro skipped, new BRAVELY title screen correct (shot at 22 s).
- New game by scripted A presses (fresh empty save): Birch intro with UI glow, the 3D voxel moving
  truck scene with the text box. The script stalls in the truck (A on the boxes), so no overworld
  or battle shot was taken.
- Logs: 0 GLERR, shader self-test OK, FBOs complete, `[POST] ready extended=1`, programs linked:
  post chain, sway, battlefx, rain, particles, water glint, char outline and blob, flatfx;
  `[SUNSHADOW] ready`; timer query works. The default framebuffer reports 16 depth bits (SDL asked
  for 24); the scene renders into its own 24-bit depth texture.
- Not checked: overworld and battle visuals, other GPUs, controller, audio by ear, performance
  (the perf summary of the truck scene showed a few hitches over 33 ms, 4.6 percent of frames,
  one 762 ms stall, probably the first-load shader and texture work).

