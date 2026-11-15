# Building

This fork builds for the **Steam Deck** and **Windows**. Follow the guide for your target:

- Steam Deck: [BUILD_DECK.md](BUILD_DECK.md) (one script, `tools/deck/build_deck.sh`).
- Windows (32-bit, MSYS2): [WINDOWS_BUILD.md](WINDOWS_BUILD.md).

PS Vita builds are not covered here (the Vita code lineage remains in the tree).

This repository holds no game data and no ROM. Both flows start from a dump of your own Pokémon
Emerald cartridge; see the legal note at the top of [BUILD_DECK.md](BUILD_DECK.md).

## Steps both flows share

0. **Game files** (once): `./tools/fetch_assets.sh <your Emerald dump>` (MINGW64 or Linux shell) checks the
   dump, downloads the decompilation's graphics, sounds and data, and generates the files made from
   them. Its last step needs the graphics that `assets.mk` converts, so on a clean tree it has to
   run twice, with steps 1 and 2 between the runs (`FETCH_NO_CONVERT=1` makes the first run stop
   early). The Deck and Windows scripts do this ordering for you.
1. **Host tools** (once):
   ```sh
   make -f make_tools.mk -j8 CC="gcc -static" CXX="g++ -static"
   ```
2. **Game assets** (once, and after editing graphics, maps, sound or JSON):
   ```sh
   make -f vita/assets.mk -j8
   ```

Then the platform build: `tools/deck/build_deck.sh` or `tools\windows\build.bat`. For a clean
rebuild delete the build folder (or run `tools/windows/build.sh --clean`).

## CMake options

| Option | Default | Effect |
|---|---|---|
| `DECK` | OFF | Steam Deck build (32-bit x86 Linux) |
| `WINDOWS` | OFF | Windows build; implies `DECK` |
| `VITA_PARALLEL_DRAW` | ON | Draw GBA frames on the worker threads. Also enables the 288-px widescreen lines |
| `VITA_LOGGING` | OFF | `VITA_LOG` writes a debug log |
| `DECK_LOGGING` | ON | Deck: `VITA_LOG` writes `userdata/debug.log` |
| `VITA_DRAW_SELFCHECK` | OFF | Testing: compare parallel and sequential drawing periodically |

## Warnings

The build prints no warnings; keep it that way. The flags are split in `CMakeLists.txt`:
the decompiled game keeps the leniency it was written for (`-Wno-implicit-function-declaration`
and friends), while `src/platform/` builds with `-Wall -Wextra` (only unused parameters, sign
comparisons and missing field initialisers are left out). The preprocessing step passes
`-Wno-trigraphs`, since `??` in game text is never meant as one.
