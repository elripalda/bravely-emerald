# Steam Deck Port, Stage 1 (game boots) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 32-bit x86 Linux build of the port that starts on the Steam Deck, shows the title screen, lets you start a new game, walk, save, quit and reload.

**Architecture:** Keep the existing code and build pipeline. Add a `DECK` option to `CMakeLists.txt` that swaps the Vita toolchain for 32-bit GCC in a container, replaces the three Vita-SDK platform files with Deck versions under `src/platform/deck/`, and supplies small compatibility headers so the remaining Vita-flavoured files compile unchanged. The 3D voxel renderer is compiled but switched off at run time; it is stage 2.

**Tech Stack:** C (gnu99), CMake, GCC `-m32`, SDL2, desktop OpenGL (Mesa), distrobox on an Arch Linux image, SSH to the Deck.

**Spec:** `docs/superpowers/specs/2026-11-13-steam-deck-port-design.md`

## Global Constraints

- Target: Linux x86 32-bit (`-m32`), run on the Steam Deck (SteamOS, Zen 2).
- All work and files on the Deck live under `/run/media/deck/SD512/emerald-deck/`. The Deck is reached with `ssh deck`.
- Code is written in this repo at `F:\emerald-deck` and pushed to the Deck with `git` (bare repo on the Deck, see Task 2). Nothing is pushed to GitHub unless Marcos asks.
- Keep the Vita build working: Deck changes sit behind `PLATFORM_DECK` or the `DECK` CMake option.
- The ROM is `/run/media/deck/SD512/emerald-deck/rom/myemerald.gba` (SHA-1 `f3ae088181bf583e55daf962a92bb46f4f1d07b7`, already copied).
- Tooling stays minimal: gcc, make, cmake, pkg-config, git, SDL2 and SDL2_image (32-bit), Mesa GL (32-bit).
- Never guess an error cause. Read the compiler or linker message, then the code, then the docs in `docs/`.
- Agents: Sonnet for Tasks 3 to 7, Haiku for mechanical sub-steps marked "Haiku". At most 30 tool calls and 3 retries per agent, report in 40 lines or fewer.

## Task commands used throughout

Run a command in the container on the Deck from this PC:

```bash
ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src && <command>'"
```

Below this is written as `DECK_RUN '<command>'`.

---

### Task 1: Build container on the SD card

**Agent:** Haiku (mechanical, commands only)

**Files:** none in the repo.

**Interfaces:**
- Produces: a distrobox named `emerald-build` with `gcc`, `make`, `cmake`, `pkg-config`, `git`, 32-bit glibc, 32-bit SDL2, SDL2_image and Mesa.

- [ ] **Step 1: Point podman storage at the SD card**

```bash
ssh deck 'mkdir -p /run/media/deck/SD512/emerald-deck/containers ~/.config/containers && cat > ~/.config/containers/storage.conf <<EOF
[storage]
driver = "overlay"
graphroot = "/run/media/deck/SD512/emerald-deck/containers"
EOF'
```

- [ ] **Step 2: Create the container and test that it can start**

```bash
ssh deck 'distrobox create --yes --name emerald-build --image docker.io/library/archlinux:latest && distrobox enter emerald-build -- echo container-ok'
```

Expected: `container-ok`. If creation fails with a permission or `nodev` error on the SD card, remove the `storage.conf` from Step 1 (`ssh deck 'rm ~/.config/containers/storage.conf'`), recreate the container on the internal disk and note it in `docs/DECK_NOTES.md`. Do not retry the SD card variant.

- [ ] **Step 3: Enable the multilib repository and install the toolchain**

```bash
ssh deck "distrobox enter emerald-build -- sudo bash -c \"printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf && pacman -Syu --noconfirm && pacman -S --noconfirm base-devel cmake git pkgconf lib32-glibc lib32-gcc-libs lib32-sdl2 lib32-sdl2_image lib32-mesa lib32-libglvnd\""
```

- [ ] **Step 4: Verify 32-bit compile, link and run work**

```bash
ssh deck "distrobox enter emerald-build -- bash -lc 'cd /tmp && printf \"#include <stdio.h>\nint main(){printf(\\\"%zu\\\\n\\\",sizeof(void*));}\" > t.c && gcc -m32 t.c -o t32 && ./t32 && PKG_CONFIG_PATH=/usr/lib32/pkgconfig pkg-config --cflags --libs sdl2 SDL2_image gl'"
```

Expected: `4`, then compiler and linker flags that mention `/usr/lib32` or `-lSDL2`. If `pkg-config` finds nothing, run `ls /usr/lib32/pkgconfig | grep -i -E 'sdl2|gl'` and use the names it prints.

- [ ] **Step 5: Record the container setup**

Create `docs/DECK_NOTES.md` with the exact commands that worked, then commit:

```bash
git add docs/DECK_NOTES.md && git commit -m "docs: Deck build container setup"
```

---

### Task 2: Sync repo to the Deck, build host tools and game assets

**Agent:** Haiku (commands), then Sonnet if a tool fails to build.

**Files:**
- Create: none. Uses `make_tools.mk`, `fetch_assets.sh`, `vita/assets.mk`.

**Interfaces:**
- Consumes: Task 1 container; ROM at `/run/media/deck/SD512/emerald-deck/rom/myemerald.gba`.
- Produces: `/run/media/deck/SD512/emerald-deck/src` checkout with built tools (`tools/preproc/preproc` and others) and generated assets (`include/constants/map_groups.h`, `graphics/`, `sound/` binaries).

- [ ] **Step 1: Make a bare repo on the Deck and push this checkout to it**

```bash
ssh deck 'git init --bare /run/media/deck/SD512/emerald-deck/repo.git'
git remote add deck deck:/run/media/deck/SD512/emerald-deck/repo.git
git push deck HEAD:main
ssh deck 'git clone /run/media/deck/SD512/emerald-deck/repo.git /run/media/deck/SD512/emerald-deck/src'
```

Expected: clone succeeds. The `deck` remote is local to this checkout; nothing goes to GitHub.

- [ ] **Step 2: Build the host tools (native 64-bit, they run on the build machine only)**

Run: `DECK_RUN 'make -f make_tools.mk -j8 2>&1 | tail -20'`
Expected: `tools/preproc/preproc` exists. On error, read the compiler line, search `docs/BUILD.md` and `make_tools.mk` for the cause, fix, re-run (max 3 times, then report).

- [ ] **Step 3: Fetch assets, checking the ROM**

Run: `DECK_RUN './fetch_assets.sh ../rom/myemerald.gba 2>&1 | tail -30'`
Expected: the ROM check passes (it prints the title and checksum result) and downloads finish without hash errors. Needs network access from the Deck.

- [ ] **Step 4: Generate the portable build's assets**

Run: `DECK_RUN 'make -f vita/assets.mk -j8 2>&1 | tail -20'`
Expected: `include/constants/map_groups.h` exists (`DECK_RUN 'ls include/constants/map_groups.h'`).

- [ ] **Step 5: Commit nothing; record result**

Append the outcome to `docs/DECK_NOTES.md` and commit it (`git commit -am "docs: Deck tool and asset build notes"`). Do not commit the generated assets; they hold Nintendo data and are ignored by the repo's `.gitignore`. Check with `git status --short | head` that no generated asset shows up.

---

### Task 3: `DECK` option in CMakeLists.txt

**Agent:** Sonnet

**Files:**
- Modify: `CMakeLists.txt` lines 12-20, 39-42, 44, 59-60, 110, 119, 130-143, 163-172, 196-300 (Vita-only parts), and the post-build section.

**Interfaces:**
- Produces: `cmake -S . -B build-deck -DDECK=ON` configures; `cmake --build build-deck` compiles and links an executable `build-deck/pokeemerald` (link may fail until Tasks 4 to 7 are done).
- Produces: variables `DECK_AS`, `DECK_LD`, `DECK_OBJCOPY`, `DECK_CPP` (aliases of the `VITA_*` ones, so the shared pipeline code needs no rename).

- [ ] **Step 1: Add the option and branch the toolchain**

Replace the block from `cmake_minimum_required` to `include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)` with:

```cmake
cmake_minimum_required(VERSION 3.16)

option(DECK "Build for the Steam Deck (32-bit x86 Linux) instead of the PS Vita" OFF)

if(DECK)
    # 32-bit x86: the game data is assembled with 4-byte pointers (.4byte).
    set(CMAKE_C_FLAGS_INIT "-m32")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
    project(pokeemerald_deck C)
else()
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        if(NOT DEFINED ENV{VITASDK})
            message(FATAL_ERROR "VITASDK is not set; see README.md")
        endif()
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    endif()
    project(pokeemerald_vita C)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
endif()
```

- [ ] **Step 2: Tool variables for the Deck**

Replace the four `find_program(VITA_CPP ...)` lines with:

```cmake
if(DECK)
    find_program(VITA_CPP     cpp     REQUIRED)
    find_program(VITA_AS      as      REQUIRED)
    find_program(VITA_LD      ld      REQUIRED)
    find_program(VITA_OBJCOPY objcopy REQUIRED)
else()
    find_program(VITA_CPP     arm-vita-eabi-cpp     HINTS "$ENV{VITASDK}/bin" REQUIRED)
    find_program(VITA_AS      arm-vita-eabi-as      HINTS "$ENV{VITASDK}/bin" REQUIRED)
    find_program(VITA_LD      arm-vita-eabi-ld      HINTS "$ENV{VITASDK}/bin" REQUIRED)
    find_program(VITA_OBJCOPY arm-vita-eabi-objcopy HINTS "$ENV{VITASDK}/bin" REQUIRED)
endif()
```

- [ ] **Step 3: Defines, assembler flags and data directives per target**

After `set(GAME_DEFINES ...)` add:

```cmake
if(DECK)
    # Keep PLATFORM_VITA: the port's UI and renderer code is guarded by it. PLATFORM_DECK marks
    # the places where the Deck differs.
    list(APPEND GAME_DEFINES -DPLATFORM_DECK)
    set(ASM_ARCH --32)
    set(ASM_WORD ".long")
    set(LD_EMULATION -m elf_i386)
else()
    set(ASM_ARCH -march=armv7-a)
    set(ASM_WORD ".word")
    set(LD_EMULATION "")
endif()
```

In the song assembly command (line 110) replace `s/\\.4byte/\\.word/g;s/\\.2byte/\\.short/g` with `s/\\.4byte/${ASM_WORD}/g;s/\\.2byte/\\.short/g` and `-march=armv7-a` with `${ASM_ARCH}`. In the data assembly command (line 119) replace `-march=armv7-a` with `${ASM_ARCH}`. In the two `${VITA_LD} -r` commands add `${LD_EMULATION}` after the executable name.

- [ ] **Step 4: Compile options and libraries per target**

Replace `VITA_CPU_TUNING_OPTIONS` with:

```cmake
if(DECK)
    # Deck CPU is Zen 2. SSE2 is the baseline for the PPU conversion (Task 6).
    set(VITA_CPU_TUNING_OPTIONS -march=znver2 -mtune=znver2 -msse2)
    set(OPT_LEVEL -O3)
else()
    set(VITA_CPU_TUNING_OPTIONS -mcpu=cortex-a9 -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard)
    set(OPT_LEVEL -O2)
endif()
```

and in `COMMON_COMPILE_OPTIONS` replace `-O2` with `${OPT_LEVEL}`. Replace the `find_package(SDL2 ...)` and `target_link_libraries(pokeemerald ...)` blocks with an `if(DECK)` branch:

```cmake
if(DECK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DECK_LIBS REQUIRED sdl2 SDL2_image gl)
    target_include_directories(pokeemerald PRIVATE "${ROOT}/src/platform/deck/compat" ${DECK_LIBS_INCLUDE_DIRS})
    target_link_libraries(pokeemerald PRIVATE ${DECK_LIBS_LDFLAGS} pthread m)
else()
    # (existing find_package and target_link_libraries lines stay here)
endif()
```

Wrap everything from `target_link_options(... max-page-size ...)` through the end of the VPK section in `if(NOT DECK) ... endif()`, then add after it:

```cmake
if(DECK)
    # Platform-side sources: swap three Vita-SDK files for the Deck versions.
    # (done in Task 4 where the files exist)
    add_custom_command(TARGET pokeemerald POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/dist/app0"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ROOT}/assets/voxel" "${CMAKE_BINARY_DIR}/dist/app0/voxel"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ROOT}/assets/ui" "${CMAKE_BINARY_DIR}/dist/app0/ui"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:pokeemerald>" "${CMAKE_BINARY_DIR}/dist/pokeemerald")
endif()
```

Check the `assets/followers` handling: if the VPK section copied it, copy it the same way.

- [ ] **Step 5: Configure and check**

Run: `DECK_RUN 'cmake -S . -B build-deck -DDECK=ON -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -15'`
Expected: `-- Configuring done` and `-- Generating done`. Fix any CMake error from the message. Check the Vita configure still parses: `grep -n "if(DECK)\|endif()" CMakeLists.txt` and confirm each `if(DECK)` has a matching `endif()`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt && git commit -m "build: add DECK option (32-bit x86 Linux)"
```

---

### Task 4: Deck platform files (platform, input, video)

**Agent:** Sonnet

**Files:**
- Create: `src/platform/deck/deck_platform.c`, `src/platform/deck/deck_input.c`, `src/platform/deck/deck_video.c`, `src/platform/deck/deck_video.h`
- Modify: `src/platform/vita/vita_platform.h` (data directory define), `CMakeLists.txt` (source selection)

**Interfaces:**
- Consumes: the function declarations in `src/platform/vita/vita_platform.h` and `vita_input.h` (unchanged).
- Produces (`deck_video.h`):

```c
#ifndef GUARD_DECK_VIDEO_H
#define GUARD_DECK_VIDEO_H
#include <stdbool.h>
#include <SDL2/SDL.h>

#define DECK_LOGICAL_WIDTH  960
#define DECK_LOGICAL_HEIGHT 544

bool DeckVideo_Init(SDL_Window **outWindow);               // window + GL context, vsync on
void DeckVideo_Swap(void);                                  // SDL_GL_SwapWindow
void DeckVideo_SetSwapInterval(int interval);               // 0, 1 or 2
// Letterboxed 960x544 viewport inside the window, in window pixels.
void DeckVideo_GetViewport(int *x, int *y, int *w, int *h);
#endif
```

- [ ] **Step 1: Data directory define**

In `src/platform/vita/vita_platform.h` replace `#define VITA_DATA_DIR "ux0:data/POKEVITA/"` with:

```c
#ifdef PLATFORM_DECK
// Relative to the working directory; the launcher script changes into the install folder.
#define VITA_DATA_DIR "userdata/"
#else
#define VITA_DATA_DIR "ux0:data/POKEVITA/"
#endif
```

- [ ] **Step 2: Write `deck_video.c`**

```c
#include "deck_video.h"
#include <stdio.h>

static SDL_Window *sWindow;
static SDL_GLContext sContext;

bool DeckVideo_Init(SDL_Window **outWindow)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    sWindow = SDL_CreateWindow("Pokemon Emerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (sWindow == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    sContext = SDL_GL_CreateContext(sWindow);
    if (sContext == NULL)
    {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    *outWindow = sWindow;
    return true;
}

void DeckVideo_Swap(void)
{
    SDL_GL_SwapWindow(sWindow);
}

void DeckVideo_SetSwapInterval(int interval)
{
    SDL_GL_SetSwapInterval(interval);
}

void DeckVideo_GetViewport(int *x, int *y, int *w, int *h)
{
    int winW, winH;
    SDL_GL_GetDrawableSize(sWindow, &winW, &winH);
    // Fit 960x544 (16:9) inside the window, centred.
    int fitW = winW;
    int fitH = (winW * DECK_LOGICAL_HEIGHT) / DECK_LOGICAL_WIDTH;
    if (fitH > winH)
    {
        fitH = winH;
        fitW = (winH * DECK_LOGICAL_WIDTH) / DECK_LOGICAL_HEIGHT;
    }
    *x = (winW - fitW) / 2;
    *y = (winH - fitH) / 2;
    *w = fitW;
    *h = fitH;
}
```

- [ ] **Step 3: Write `deck_platform.c`**

Implements every function in `vita_platform.h` that `src/platform/vita/vita_platform.c` defines. Copy that file, then replace as follows:

| Vita function | Deck body |
|---|---|
| `VitaPlatform_Init` | `mkdir("userdata", 0777)` and `mkdir("userdata/saves", 0777)` (`<sys/stat.h>`); remove the `scePower*` block and log lines; keep `VitaInput_Init()`, `VitaAudio_Init()`, `VitaExtras_Init()` calls and their failure logs. |
| `VitaPlatform_Exit` | keep the log flush, `VitaPerf_Shutdown()`, `VitaAudio_Terminate()`, then `SDL_Quit(); exit(0);` |
| `VitaPlatform_LoadAsset` | the same code, with the three path formats changed to `"app0/%s"`, `"%s%s"` (`VITA_DATA_DIR`), `"app0/assets/%s"` |
| `VitaPlatform_PinCurrentThread` | `(void)name; (void)core;` and a `VITA_LOG`. Name the thread with `pthread_setname_np(pthread_self(), name)` (`<pthread.h>`, guard with `#ifdef __linux__`). |
| `VitaPlatform_DelayMicroseconds` | `struct timespec ts = { us / 1000000, (us % 1000000) * 1000 }; nanosleep(&ts, NULL);` |
| `VitaPlatform_GetCpuMHz` | `return 3500;` with a comment that the Deck's clock is not controlled by the game. |
| `VitaPlatform_GetLocalTime` | `time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);` and fill the fields: year `tm_year + 1900`, month `tm_mon + 1`, day `tm_mday`, dayOfWeek `tm_wday`, hour, minute, second. Return 0. |
| `VitaPlatform_GetMilliseconds` | `clock_gettime(CLOCK_MONOTONIC)` minus a static start value, in ms. |
| `VitaPlatform_GetWallSeconds` | `return (int64_t)time(NULL) + 62135596800LL;` — Sony's tick counts from year 1. **Step 5 verifies this against the callers.** |
| `VitaPlatform_ReadButtons`, `ReadTouch`, `ReadTouchGBA`, `BuildPath` | unchanged (they call `VitaInput_*` and `snprintf`) |

Remove every `#include <psp2/...>` and the `_newlib_heap_size_user` variable. Add `#include <time.h> <sys/stat.h> <unistd.h>`.

- [ ] **Step 4: Write `deck_input.c`**

Copy `src/platform/vita/vita_input.c`, delete the `psp2` includes and replace `VitaInput_Init`, `DecodeButtons` and the polling code. Keep `VitaInput_GetButtonsHeld`, `VitaInput_ReadTouchScreen` and `VitaInput_ReadTouchGBA` unchanged.

```c
#include <SDL2/SDL.h>
#include "deck_video.h"

static SDL_GameController *sPad;

bool VitaInput_Init(void)
{
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
        {
            sPad = SDL_GameControllerOpen(i);
            break;
        }
    }
    memset(&sCurrentState, 0, sizeof(sCurrentState));
    sPreviousButtons = 0U;
    VITA_LOG("[INPUT] controller %s\n", sPad ? "opened" : "not found");
    return true;
}

static uint8_t AxisToByte(Sint16 v)
{
    return (uint8_t)((v >> 8) + 128);   // -32768..32767 -> 0..255, centre 128
}

static uint32_t DecodePad(SDL_GameController *pad, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry)
{
    uint32_t b = VITA_BTN_NONE;
    *lx = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
    *ly = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
    *rx = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
    *ry = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));

#define HELD(btn) SDL_GameControllerGetButton(pad, btn)
    if (HELD(SDL_CONTROLLER_BUTTON_A))             b |= VITA_BTN_A;
    if (HELD(SDL_CONTROLLER_BUTTON_B))             b |= VITA_BTN_B;
    if (HELD(SDL_CONTROLLER_BUTTON_X))             b |= VITA_BTN_SQUARE;
    if (HELD(SDL_CONTROLLER_BUTTON_Y))             b |= VITA_BTN_TRIANGLE;
    if (HELD(SDL_CONTROLLER_BUTTON_BACK))          b |= VITA_BTN_SELECT;
    if (HELD(SDL_CONTROLLER_BUTTON_START))         b |= VITA_BTN_START;
    if (HELD(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  b |= VITA_BTN_L;
    if (HELD(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= VITA_BTN_R;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_UP)    || *ly < STICK_DEADZONE_LOW)  b |= VITA_BTN_UP;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || *ly > STICK_DEADZONE_HIGH) b |= VITA_BTN_DOWN;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || *lx < STICK_DEADZONE_LOW)  b |= VITA_BTN_LEFT;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || *lx > STICK_DEADZONE_HIGH) b |= VITA_BTN_RIGHT;
#undef HELD
    if (*ry < STICK_DEADZONE_LOW)  b |= VITA_BTN_RS_UP;
    if (*ry > STICK_DEADZONE_HIGH) b |= VITA_BTN_RS_DOWN;
    if (*rx < STICK_DEADZONE_LOW)  b |= VITA_BTN_RS_LEFT;
    if (*rx > STICK_DEADZONE_HIGH) b |= VITA_BTN_RS_RIGHT;

    if ((b & (VITA_BTN_UP | VITA_BTN_DOWN)) == (VITA_BTN_UP | VITA_BTN_DOWN))
        b &= ~(VITA_BTN_UP | VITA_BTN_DOWN);
    if ((b & (VITA_BTN_LEFT | VITA_BTN_RIGHT)) == (VITA_BTN_LEFT | VITA_BTN_RIGHT))
        b &= ~(VITA_BTN_LEFT | VITA_BTN_RIGHT);
    return b;
}
```

In `VitaInput_Poll`, replace the `sceCtrl` block with: if `sPad` is NULL, treat all as 0; else call `DecodePad` and update `buttonsHeld/Pressed/Released` and stick fields exactly as the Vita version does. Replace the `sceTouch` block with the following. Steam Input may turn touch into mouse events, so both are read:

```c
    int vx, vy, vw, vh;
    DeckVideo_GetViewport(&vx, &vy, &vw, &vh);
    SDL_Window *win = SDL_GL_GetCurrentWindow();
    int winW = 1, winH = 1;
    if (win) SDL_GL_GetDrawableSize(win, &winW, &winH);

    float px = 0.0f, py = 0.0f;
    bool down = false;
    if (SDL_GetNumTouchDevices() > 0)
    {
        SDL_TouchID id = SDL_GetTouchDevice(0);
        if (SDL_GetNumTouchFingers(id) > 0)
        {
            SDL_Finger *f = SDL_GetTouchFinger(id, 0);
            px = f->x * (float)winW;
            py = f->y * (float)winH;
            down = true;
        }
    }
    if (!down)
    {
        int mx, my;
        if (SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT))
        {
            px = (float)mx; py = (float)my; down = true;
        }
    }
    sCurrentState.touchActive = false;
    if (down && px >= vx && px < vx + vw && py >= vy && py < vy + vh)
    {
        sCurrentState.touchActive  = true;
        sCurrentState.touchScreenX = (int32_t)((px - vx) * DECK_LOGICAL_WIDTH / vw);
        sCurrentState.touchScreenY = (int32_t)((py - vy) * DECK_LOGICAL_HEIGHT / vh);
        /* GBA mapping: copy the existing viewport maths from vita_input.c unchanged */
    }
```

Copy the existing "Map to GBA coords" block under the marked comment. The mouse position is in window units; on the Deck window and drawable sizes match (no high-DPI), so no extra scaling.

- [ ] **Step 5: Verify the wall-clock epoch against callers**

Run: `grep -rn "VitaPlatform_GetWallSeconds" src --include=*.c | grep -v "vita_platform.c\|deck_platform.c"`
Read each caller. If none treats the value as a Unix time (for example, none subtracts 1970 values), keep the `+ 62135596800LL` offset. If any compares it with `time()`, drop the offset. Write the finding as a comment above the function.

- [ ] **Step 6: Select the sources in CMake**

In `CMakeLists.txt`, after `list(FILTER GAME_C EXCLUDE REGEX "\\.inc\\.c$")` add:

```cmake
if(DECK)
    list(FILTER GAME_C EXCLUDE REGEX "/src/platform/vita/(vita_platform|vita_input)\\.c$")
else()
    list(FILTER GAME_C EXCLUDE REGEX "/src/platform/deck/")
endif()
```

`GAME_C` is the glob of all `src/*.c`, so the Deck files are picked up automatically; the existing `PLATFORM_SIDE_REGEX` has `/src/platform/(vita|voxel|ui)/`, so add `deck` there: `/src/platform/(vita|voxel|ui|deck)/`.

- [ ] **Step 7: Commit**

```bash
git add src/platform/deck src/platform/vita/vita_platform.h CMakeLists.txt
git commit -m "feat(deck): platform, input and video files"
```

---

### Task 5: Compatibility headers for the remaining Vita code

**Agent:** Sonnet

**Files:**
- Create: `src/platform/deck/compat/vitaGL.h`, `src/platform/deck/compat/psp2/power.h`, `src/platform/deck/compat/psp2/kernel/threadmgr.h`, `src/platform/deck/compat/psp2/kernel/processmgr.h`, `src/platform/deck/compat/psp2/rtc.h`, `src/platform/deck/compat/psp2/audioout.h`, `src/platform/deck/compat/psp2/ctrl.h`, `src/platform/deck/compat/psp2/touch.h`
- Modify: `src/platform/sdl2.c` (init block, swap calls), `src/platform/vita/vita_perf.c` only if its `sce*` calls need a stub

**Interfaces:**
- Consumes: `deck_video.h` from Task 4.
- Produces: headers that make the unchanged includes in `sdl2.c`, `ui_render.c`, `ui_sprites.c`, `vita_perf.c`, `vita_stage.c`, `voxel_renderer.c`, `vita_audio.c` compile.

- [ ] **Step 1: Write `vitaGL.h`**

```c
#ifndef DECK_COMPAT_VITAGL_H
#define DECK_COMPAT_VITAGL_H

#include <GL/gl.h>
#include <GL/glext.h>
#include "../deck_video.h"

// vitaGL setup calls have no desktop equivalent: the window and context come from DeckVideo_Init.
#define SHARK_OPT_FAST 0
#define SCE_GXM_MULTISAMPLE_NONE 0
#define SCE_KERNEL_CPU_MASK_USER_2 0

static inline void vglSetupGarbageCollector(int a, int b) { (void)a; (void)b; }
static inline void vglSetupRuntimeShaderCompiler(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; }
static inline void vglUseLowPrecision(int v) { (void)v; }
static inline void vglUseVramForUSSE(int v) { (void)v; }
static inline void vglUseCachedMem(int v) { (void)v; }
static inline void vglUseTripleBuffering(int v) { (void)v; }
static inline void vglSwapResolution(int w, int h) { (void)w; (void)h; }
static inline void vglSwapBuffers(int hasCommonDialog) { (void)hasCommonDialog; DeckVideo_Swap(); }
static inline void eglSwapInterval(void *display, int interval) { (void)display; DeckVideo_SetSwapInterval(interval); }
int vglInitWithCustomSizes(int, int, int, int, int, int, int, int);

#endif
```

`vglInitWithCustomSizes` is defined in `deck_video.c`: add

```c
int vglInitWithCustomSizes(int a, int w, int h, int c, int d, int e, int f, int g)
{
    (void)a; (void)w; (void)h; (void)c; (void)d; (void)e; (void)f; (void)g;
    SDL_Window *win;
    return DeckVideo_Init(&win) ? 1 : 0;   // vitaGL returns true on success
}
```

Check what `vglInitWithCustomSizes` returns in the Vita headers' documentation (the call site treats the result as a boolean: failure branch logs `failed!`). Keep as written if the call site matches that.

- [ ] **Step 2: Write the `psp2/*` stubs**

Create each header with an include guard. `psp2/audioout.h`, `psp2/ctrl.h` and `psp2/touch.h` are empty (`vita_audio.c` includes `audioout.h` but uses nothing from it, and the two input headers are only for the replaced files). `psp2/power.h` and `psp2/kernel/threadmgr.h` stay empty at first; `psp2/kernel/processmgr.h` and `psp2/rtc.h` are filled from the compiler errors in Step 4.

- [ ] **Step 3: Switch the init block in `sdl2.c`**

The existing `#ifdef PLATFORM_VITA` block at lines 583 to 599 already calls the `vgl*` functions that Step 1 stubs, so it compiles unchanged. It leaves `sdlRenderer` as NULL, which the code already handles (`if (sdlRenderer == NULL) return;`). The only change is at line 46-50: no edit is needed, because `<vitaGL.h>`, `<psp2/power.h>` and `<psp2/kernel/threadmgr.h>` now resolve to the compat headers.

- [ ] **Step 4: Compile and fix by reading errors**

Run: `DECK_RUN 'cmake --build build-deck -j8 2>&1 | grep -E "error|Error" | head -30'`

For each error:
1. If it names a missing `sce*` function or `SCE_*` constant: read how it is used (`grep -n NAME src -r`), add the smallest inline stub or constant to the matching compat header with a one-line comment saying what it replaces.
2. If it names a missing header: add an empty guarded header at that path.
3. If it is in code outside `src/platform`, stop and report to the orchestrator (the game files should not need edits for this step).

Repeat until the build reaches the link step. Max 3 rounds without progress, then report.

- [ ] **Step 5: Commit**

```bash
git add src/platform/deck src/platform/sdl2.c
git commit -m "feat(deck): compatibility headers for vitaGL and psp2"
```

---

### Task 6: SSE2 replacement for the NEON line copy

**Agent:** Sonnet

**Files:**
- Modify: `src/platform/gba_fast_draw.c:5` (include) and `3136-3160` (`CopyLineToTexture`)

**Interfaces:**
- Consumes/Produces: `CopyLineToTexture(const uint16_t *pixels, int32_t i)` keeps its signature. Behaviour is unchanged: BGR555 to RGB565, green widened to 6 bits by repeating its top bit.

- [ ] **Step 1: Write the check first**

Create `tools/deck_tests/test_copyline.c`, a standalone program with a scalar reference and the SSE2 function copied under test:

```c
#include <stdint.h>
#include <stdio.h>
#include <emmintrin.h>

static uint16_t ref(uint16_t c)
{
    uint16_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    uint16_t g6 = (g << 1) | (g >> 4);
    return (uint16_t)((r << 11) | (g6 << 5) | b);
}

static void convert8(const uint16_t *src, uint16_t *dst)
{
    const __m128i mask5 = _mm_set1_epi16(0x1F);
    __m128i c  = _mm_loadu_si128((const __m128i *)src);
    __m128i r  = _mm_and_si128(c, mask5);
    __m128i g  = _mm_and_si128(_mm_srli_epi16(c, 5), mask5);
    __m128i b  = _mm_and_si128(_mm_srli_epi16(c, 10), mask5);
    __m128i g6 = _mm_or_si128(_mm_slli_epi16(g, 1), _mm_srli_epi16(g, 4));
    __m128i out = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(r, 11), _mm_slli_epi16(g6, 5)), b);
    _mm_storeu_si128((__m128i *)dst, out);
}

int main(void)
{
    uint16_t in[8], out[8];
    for (uint32_t base = 0; base < 0x8000; base += 8)
    {
        for (int i = 0; i < 8; i++) in[i] = (uint16_t)(base + i);
        convert8(in, out);
        for (int i = 0; i < 8; i++)
            if (out[i] != ref(in[i])) { printf("FAIL %04x: %04x != %04x\n", in[i], out[i], ref(in[i])); return 1; }
    }
    puts("PASS all 32768 colours");
    return 0;
}
```

- [ ] **Step 2: Run it and see it pass**

Run: `DECK_RUN 'gcc -m32 -O2 -msse2 tools/deck_tests/test_copyline.c -o /tmp/test_copyline && /tmp/test_copyline'`
Expected: `PASS all 32768 colours`. A failure means the intrinsics or the reference are wrong; fix the test first.

- [ ] **Step 3: Replace the NEON code**

At the top of `gba_fast_draw.c`, replace `#include <arm_neon.h>` with:

```c
#ifdef __ARM_NEON
#include <arm_neon.h>
#else
#include <emmintrin.h>
#endif
```

In `CopyLineToTexture`, wrap the existing NEON `mask5` declaration and loop in `#ifdef __ARM_NEON` and add the SSE2 branch in `#else`, using the exact body from `convert8` with `_mm_loadu_si128((const __m128i *)(src + x))` and `_mm_storeu_si128((__m128i *)(dst + x), out)` inside the same `for (x = x0; x < x1; x += 8)` loop. The file also has `#if defined(PLATFORM_VITA) && defined(VITA_PARALLEL_DRAW)` at lines 1-14 that includes the NEON header; make that include conditional the same way.

- [ ] **Step 4: Build and commit**

Run: `DECK_RUN 'cmake --build build-deck -j8 2>&1 | grep -E "error" | head'` (the build may still fail at link; there must be no error in `gba_fast_draw.c`).

```bash
git add src/platform/gba_fast_draw.c tools/deck_tests
git commit -m "perf(deck): SSE2 line copy in place of NEON, with exhaustive test"
```

---

### Task 7: Link, run, and the stage 1 exit test

**Agent:** Sonnet for fixes; orchestrator reviews the result.

**Files:**
- Create: `tools/deck/run.sh` (launcher), `tools/deck_tests/smoke.sh`
- Modify: whatever the linker errors name (read them first).

**Interfaces:**
- Produces: `build-deck/dist/pokeemerald`, `tools/deck/run.sh` (starts it from `dist/` so `userdata/` and `app0/` resolve).

- [ ] **Step 1: Link**

Run: `DECK_RUN 'cmake --build build-deck -j8 2>&1 | tail -30'`
Expected: first run fails or passes. For linker errors, read the first undefined symbol. Likely causes, in order: a compat stub missing (Task 5), the objcopy section rename failing for i386 (check `ld -m elf_i386` was applied to both `ld -r` calls in Task 3), `__start_gamedata` symbols missing (the section names are valid C identifiers, so GNU ld provides them; confirm with `objdump -h build-deck/game_combined.o | grep gamedata`). Do not change game code.

- [ ] **Step 2: Write the launcher**

`tools/deck/run.sh`:

```bash
#!/bin/sh
# Runs the build from its dist folder so relative paths (userdata/, app0/) resolve.
cd "$(dirname "$0")" || exit 1
exec ./pokeemerald "$@"
```

Copy it to `build-deck/dist/run.sh` in the POST_BUILD step (add one `copy` line) and `chmod +x`.

- [ ] **Step 3: Find the setting that turns 3D off, then write `pokeemerald.cfg`**

Run: `grep -n "voxel\|3d\|3D" src/platform/sdl2.c | grep -i "cfg\|setting\|key\|\"" | head -20`
Read the lines that parse `pokeemerald.cfg` (`ReadConfigFile`) and write the key that disables 3D into `userdata/pokeemerald.cfg` in `dist/`. If no such key exists, add it as the first fix in this task (read `ReadConfigFile` and `StoreConfigFile` and follow their pattern) and note it.

- [ ] **Step 4: Run in the container with a window**

Run: `ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src/build-deck/dist && DISPLAY=:0 ./run.sh'"` (Desktop Mode is needed for `DISPLAY=:0`). Marcos watches the screen and reports. Expected: a window opens and shows the Game Freak logo or the intro.

If it exits at once, run it again with `2>&1 | tail -30` and read the last line. If it crashes in a function, run `gdb -batch -ex run -ex bt ./pokeemerald` in the container (install `gdb` with `pacman -S --noconfirm gdb` if needed) and read the backtrace before changing anything.

- [ ] **Step 5: Write the smoke test**

`tools/deck_tests/smoke.sh`: starts the game for 20 seconds with `timeout 20`, and fails if the process exits before the timeout with a non-zero code or if `userdata/debug.log` (when `VITA_LOGGING` is on) contains `failed`:

```bash
#!/bin/sh
cd "$(dirname "$0")/../../build-deck/dist" || exit 1
timeout 20 ./pokeemerald >/tmp/deck_smoke.log 2>&1
code=$?
if [ "$code" -ne 124 ]; then
    echo "FAIL: exited with $code before 20 s"; tail -5 /tmp/deck_smoke.log; exit 1
fi
echo "PASS: ran 20 s without exiting"
```

- [ ] **Step 6: Exit test (Marcos plays)**

With the game running: reach the title screen, start a new game, create a character, walk in the player's house with the d-pad and the left stick, open the menu with Start and A, save, quit, start again and continue from the save. Frame time telemetry goes to `userdata/perf_summary.txt` if `VITA_LOGGING` is enabled; record the average.

Expected: every step works. Anything that does not is recorded in `docs/DECK_NOTES.md` with the exact symptom, and fixed in this task if it is in the platform layer.

- [ ] **Step 7: Commit**

```bash
git add tools/deck tools/deck_tests CMakeLists.txt docs/DECK_NOTES.md
git commit -m "feat(deck): stage 1 boots: launcher, smoke test and notes"
```

---

### Task 8: Run on the Deck host without the container

**Agent:** Sonnet

**Files:**
- Create: `tools/deck/package.sh`
- Modify: `docs/DECK_NOTES.md`

**Interfaces:**
- Consumes: `build-deck/dist/`.
- Produces: `build-deck/package/` that starts from the Deck host (`ssh deck`, no `distrobox`).

- [ ] **Step 1: Find which 32-bit libraries the host lacks**

Run on the host: `ssh deck 'cd /run/media/deck/SD512/emerald-deck/src/build-deck/dist && ldd ./pokeemerald | grep "not found"'`
Expected output lists missing libraries (probably `libSDL2-2.0.so.0` and the image libraries). `ldd` on a 32-bit binary works because the host has `ld-linux.so.2`.

- [ ] **Step 2: Bundle them**

`tools/deck/package.sh` copies `dist/` to `package/`, then for each missing library copies the 32-bit file out of the container's `/usr/lib32/` into `package/lib/`, and writes a launcher that sets `LD_LIBRARY_PATH="$(dirname "$0")/lib"`. Write the script from the `ldd` output of Step 1; the library list comes from that output, not from a guess.

- [ ] **Step 3: Run from the host**

Run: `ssh deck 'cd /run/media/deck/SD512/emerald-deck/src/build-deck/package && DISPLAY=:0 ./run.sh'`
Expected: the game window opens as in Task 7. If a library still fails to load, read the exact message, add that library, repeat (max 3 rounds). If the host's GL does not match, record it in `docs/DECK_NOTES.md` and keep running in the container (the spec's fallback).

- [ ] **Step 4: Commit**

```bash
git add tools/deck/package.sh docs/DECK_NOTES.md
git commit -m "feat(deck): host-native packaging"
```

---

## Self-review

- **Spec coverage:** build system (Task 3), platform layer (Tasks 4, 5), NEON to SSE2 (Task 6), `PLATFORM_DECK` guards (Tasks 3, 4), data directory and ROM check (Tasks 2, 4), container on the SD card (Task 1), runtime on the host (Task 8), stage 1 exit test (Task 7). The renderer shim for stage 2 and the 1280×800 UI for stage 3 are intentionally left to their own plans. Performance flags `-O3 -march=znver2` are in Task 3; frame limiter options and PGO belong to stage 2 when the 3D renderer is measured.
- **Placeholders:** none intended. The discovery steps (Task 5 Step 4, Task 7 Steps 1, 3 and 4, Task 8 Step 2) are explicit loops with an input to read, an action and a stop rule, because the exact symbol list depends on compiler output that cannot be known before the first build.
- **Name consistency:** `DeckVideo_Init`, `DeckVideo_Swap`, `DeckVideo_SetSwapInterval`, `DeckVideo_GetViewport` and `vglInitWithCustomSizes` are used with the same signatures in Tasks 4 and 5. `VITA_DATA_DIR` is `"userdata/"` in both Task 4 and the launcher in Task 7.
