# Steam Deck port: design

Date: 2026-11-13
Status: draft, awaiting review

## Goal

Run this repository's PS Vita port of Pokémon Emerald (pokeemerald-vita-3ds, which adds the
Emerald 3DS voxel world, 3D battles and touch UI) natively on the Steam Deck, with feature parity
and the best performance the hardware allows. Native means compiled for the Deck's CPU, with no
emulator and no Vita compatibility layer.

Non-goals: Windows or macOS builds, a 64-bit conversion of the game data, a new renderer written
on Vulkan, distribution of builds (a build contains Nintendo's data; the legal notice in
README.md still applies).

## Decisions already made

| Topic | Decision | Reason |
|---|---|---|
| Target | Linux, x86 32-bit (`-m32`), run directly on SteamOS | Game data is assembled with 4-byte pointers (`.4byte` in `data/*.s` and `sound/songs/*.s`); the C structs expect that layout. The Deck's CPU runs 32-bit code in hardware. |
| Build place | On the Deck, inside a distrobox container, all files on the SD card (`/run/media/deck/SD512/emerald-deck/`) | The Deck's internal disk has 16 GB free; the SD card has about 200 GB. SteamOS's root is read-only, so tools go in a container. |
| Renderer | Keep the OpenGL fixed-function code; add a desktop GL compatibility-profile shim | The renderer is written for vitaGL, a GLES 1.x-style API. Mesa on the Deck supports it. Vulkan is reached later by running Mesa's Zink layer, which needs no code. |
| Scope | Staged full parity | Stage 1: 2D game boots. Stage 2: voxel 3D. Stage 3: native touch UI at 1280×800. |
| Upstream | Keep Vita and Deck in one tree, with `PLATFORM_DECK` guards next to the existing `PLATFORM_VITA` ones | Lets you merge HctrRB's upstream fixes. |

## Findings from reading the code

- Only 8 files use the Vita SDK: `src/platform/sdl2.c`, `vita/vita_platform.c`, `vita/vita_input.c`,
  `vita/vita_perf.c`, `vita/vita_perf.h`, `vita/vita_platform.h`, `vita/vita_audio.c`,
  `ui/ui_sprites.c`.
- vitaGL is used in 9 files, about 690 `gl*` references, mostly `voxel/voxel_renderer.c`
  (about 490). The only vitaGL-specific calls found are the init block in `sdl2.c` (around lines
  585 to 599: `vglInitWithCustomSizes`, `vglUseCachedMem` and similar) and `vglSwapBuffers` and
  `vglSwapResolution`.
- NEON is used only in `src/platform/gba_fast_draw.c`.
- `CMakeLists.txt` is Vita-specific: VitaSDK toolchain, `arm-vita-eabi-*` tools, armv7 assembler
  flags, VPK packaging, a `sed` step that turns `.4byte` into `.word` (correct for ARM, but 2
  bytes on x86, so x86 needs `.long`).
- The host's `/usr/lib32` already holds 32-bit Mesa (`libGL`, `libEGL`, `libGLESv2`, `libGLX`),
  `/lib/ld-linux.so.2` is present and the kernel has `CONFIG_IA32_EMULATION=y`. The 32-bit SDL2
  and audio libraries were not found and are assumed missing.

Not yet verified: every GL call being fixed-function (no shaders), texture formats (RGB565,
RGBA4444) on desktop GL, the project's Vita-only code in the 17 game files under
`PLATFORM_VITA`, and whether the host tools (`make_tools.mk`) build under the container.

## Design

### 1. Build system

- Add a Linux target to `CMakeLists.txt`, selected by a `DECK` option, instead of copying the
  file. It uses the container's `gcc -m32`, the system `as` for the data files, and defines
  `PLATFORM_DECK` in place of `PLATFORM_VITA`.
- Keep the existing preprocessing pipeline (`cpp`, then `preproc`, then compile), since it
  handles the game's charmap strings and `INCBIN`.
- Replace the VPK/SELF packaging with a plain executable plus a launcher script and a `lib/`
  folder for bundled 32-bit libraries.
- Tooling stays minimal: gcc with 32-bit libraries, cmake, make, git, and 32-bit SDL2 headers.
  No extra frameworks.

### 2. Platform layer: `src/platform/deck/`

Replaces the 8 Vita files under `#ifdef PLATFORM_DECK`, with the same function names so callers
do not change.

| Vita file | Deck replacement |
|---|---|
| `vita_input.c` (SceCtrl, SceTouch) | SDL2 game controller and touch events, mapped to the same internal button and touch structures. Steam Input is left to the user. |
| `vita_platform.c` (clocks, core pinning, paths) | Paths under `$XDG_DATA_HOME/pokeemerald/`. Clock setup is dropped. Thread priorities are set with SDL. |
| `vita_audio.c` | SDL2 audio at 48 kHz stereo. |
| `vita_perf.c`/`.h` | Same telemetry written with `clock_gettime`. |
| `sdl2.c` init block | Creates an SDL2 window and desktop GL compatibility context; `vglSwapBuffers` becomes `SDL_GL_SwapWindow`. |

### 3. Renderer shim

- A small header maps the few `vgl*` calls to nothing or to standard GL, so
  `voxel_renderer.c`, `ui_render.c` and `vita_stage.c` compile unchanged.
- Resolution is no longer fixed at 960×544: the Deck panel is 1280×800 (16:10). The native UI
  and 3D viewport are laid out from the window size. Letterbox or scale options are exposed.
- Texture uploads that depend on Vita's fixed memory pools use ordinary `glTexImage2D` and
  `glTexSubImage2D`.

### 4. Performance plan

The Vita runs 60 FPS with drops to 55 at 333/166 MHz, so the Deck has a large margin; the goals
are a locked frame time and low power use (battery) rather than raw speed.

- Compile with `-O3`, `-march=znver2` (the Deck's Zen 2 cores) and link-time optimization.
  Profile-guided optimization is added after the first working build if measurements justify it.
- Replace the NEON PPU color conversion with SSE2 intrinsics, with a plain C fallback. Wider
  AVX2 variants are tried only if measured as faster.
- Keep the three-way parallel PPU drawing and the chunk worker thread. The Deck has 4 cores and
  8 threads, so the Vita's core pinning is replaced by default scheduling plus thread names; no
  fixed core numbers.
- Frame pacing uses `SDL_GL_SetSwapInterval(1)` with the game's 60.00 Hz audio-driven clock, and
  a frame limiter option for 30 and 40 FPS (the Deck's 40 Hz display mode), to save battery.
- Voxel chunk geometry is moved from client-side arrays to vertex buffer objects only if the
  profile shows the draw calls are the bottleneck; this is measured first.
- All claims are measured on the Deck. `vita_perf.c` telemetry is kept so frame-time numbers
  exist before and after each change.

### 5. Stages

1. **2D boots.** Build system, platform layer, software PPU path, input, audio, saves. Exit test:
   title screen, new game, walk in the player's house, save, quit, reload.
2. **Voxel 3D.** Renderer shim and 3D battle path. Exit test: walk the overworld with sun and
   shadows, enter a wild battle in 3D, no GL errors, steady frame time.
3. **Native UI.** Touch UI at the Deck's resolution. Exit test: open every START menu screen
   using buttons and touch.

Each stage is playable and is committed on its own.

### 6. Testing

- The Deck is the only test target. Vita3K and the upstream test list are not relied on.
- Per stage: a smoke run script that starts the game headless for a fixed number of frames and
  checks for crashes, plus a manual play checklist by you.
- Frame-time telemetry logged per run to compare builds.
- Needs from you: your ROM dump (the build checks it through `fetch_assets.sh`) copied to the
  SD card.

### 7. Work split (agents)

- Sonnet: platform layer rewrite, CMake target, shim header, NEON to SSE2.
- Haiku: mechanical edits (renaming includes, adding guards, path substitutions), log summarizing.
- Orchestrator (this session): design decisions, reviews, anything touching save data or
  renderer correctness.
- Each agent gets a short brief, a 30 tool call limit and 3 retries, and reports in at most 40
  lines. Independent files go in parallel; edits to one file are never split across agents.

## Risks

- **32-bit libraries.** The 32-bit SDL2 and audio libraries may need bundling. Fallback: run
  inside the distrobox container.
- **Mesa fixed-function gaps.** If a Vita-specific GL behavior is missing in Mesa, fallback is
  ANGLE or gl4es, after verifying their GLES 1.x support.
- **SD card storage.** Container storage on the card may fail (`nodev` mount). Fallback: keep
  the container on the internal disk, repo and build output on the card.
- **Hidden Vita assumptions** in the game files' `PLATFORM_VITA` hooks (about 17 files). They are
  reviewed one by one in stage 1.
- **Unverified assumption:** the host tools in `make_tools.mk` build and run in the container.

## Open items

- Where the ROM dump will live on the Deck.
- Whether to add a `Host deck` alias to the SSH config.
