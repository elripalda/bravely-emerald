# Repository cleanup plan (working document, removed when the cleanup is verified)

The Steam Deck and Windows builds keep `-DPLATFORM_VITA`, so "Vita only" is decided by evidence:
who references the file, and whether a Deck or Windows build step needs it.

| Path | Referenced by | Used by Deck/Windows | Action |
|---|---|---|---|
| `build_vita.sh`, `build_vita.ps1` | `build_all.*`, `builder/core/source.py` exclude list, `fetch_assets.sh` closing message, 2 doc mentions | no (VitaSDK + VPK only) | delete |
| `build_all.sh`, `build_all.ps1` | `builder/core/source.py` exclude list, docs/BUILDER.md | no (VitaSDK install + VPK) | delete |
| `vita/vita_link.sh` | CMake RULE_LAUNCH_LINK in the `NOT DECK` block | no | delete |
| `vita/make_livearea.sh`, `vita/sce_sys/**` (5 files) | CMake VPK packing, `.gitignore` | no (LiveArea art for the VPK) | delete |
| `vita/assets.mk` | build_deck.sh, windows build.sh, fetch_assets.sh, builder, docs | yes | keep |
| `make_tools.mk`, `*_rules.mk`, `charmap.txt` | `vita/assets.mk` (relative include), CMake, build scripts | yes | keep at root |
| `CMakeLists.txt` Vita branches (`else()` toolchain, VitaSDK find_program, link libs, `if(NOT DECK)` SELF/VPK block) | only the Vita build | no | remove, keep DECK and WINDOWS branches |
| `src/platform/vita/*` | whole renderer, UI, settings (`PLATFORM_VITA`) | yes | keep (`vita_platform.c`, `vita_input.c` are filtered out of the Deck build but left: no source edits) |
| `fetch_assets.sh`, `fetch_assets.ps1` | builder (repo.py, builder.py, cli.py, errors.py, main_window.py, source.py, tests), build_deck.sh, windows build.sh, docs | yes | move to `tools/` and update every reference |
| `design/stitch/_tools/export_assets.py` | fetch_assets (step 3), builder selftest, source.py KEEP, test_source | yes (builds the UI atlas inputs) | keep in place (its output path and `ROOT` are tied to `design/stitch`) |
| `design/stitch/**` (mocks, shared css/js, font bake pages) | about 50 comments in `src/platform/ui`, docs/README, VITA_UI.md, `tools/ui2/*` | design reference of the code the Deck runs | keep |
| `assets/voxel`, `assets/ui`, `assets/followers` | CMake POST_BUILD dist copy | yes | keep |
| `.github/ISSUE_TEMPLATE` | GitHub | n/a | keep |
| `docs/VITA.md`, `docs/VITA_UI.md` | docs index | describe code the Deck still runs | keep |
| `.gitignore` entries `build-vita/`, `*.vpk`, `/vita/sce_sys_custom/` | n/a | no | remove |
| other root files (`README.md`, `CONTRIBUTING.md`, `NOTICE.md`, `LICENSE`, `.editorconfig`, `.gitattributes`) | n/a | yes | keep |
