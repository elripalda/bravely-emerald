# The builder app

A desktop app (Python 3 and Qt through PySide6) that builds the game from **your own Pokemon Emerald dump**, for
Windows or for Linux and the Steam Deck. It does not replace the build scripts: it checks the computer, installs
what is missing, runs `tools/deck/build_deck.sh` or the Windows scripts as subprocesses, shows their output live,
and explains the failures it knows.

Legal note first: the repository holds no ROM and no game data, and neither does the app. The result of a build
contains Nintendo's data. Use it yourself and **do not share it**. The ROM is only read to check it (by
`fetch_assets.sh`, as before); the app never copies, stores or uploads it.

## Install and run

**Packaged app (Windows).** Download `BravelyEmeraldBuilder-win.zip`, unzip it anywhere, run
`BravelyEmeraldBuilder.exe`. No Python is needed. (There is no installer yet; an Inno Setup or NSIS installer
can be added later.)

**Packaged app (Linux, SteamOS).** Unpack `BravelyEmeraldBuilder-linux-x86_64.tar.gz`, then `sh install.sh`
(copies it to `~/.local/share/bravely-emerald-builder` and adds a menu entry; `sh install.sh --uninstall`
removes it). On a Deck use Desktop Mode.

**From source.** With the repository cloned:

```sh
pip install PySide6          # PySide6-Essentials is enough
python tools/builder/app.py
```

The app finds the build scripts relative to itself in a source checkout. The packaged app works with a clone
chosen on first run: it looks next to itself and in `~/bravely-emerald`, otherwise the Setup page asks you to
choose an existing clone (*Choose folder*) or runs `git clone https://github.com/marcoscodas/bravely-emerald`
(*Clone repository*). The repository is private for now, so the clone may need credentials; if it fails the
message is shown and you can pick an existing folder instead. Git never prompts inside the app
(`GIT_TERMINAL_PROMPT=0`): with a credential manager installed it can still sign in, otherwise clone once in a
terminal.

Options: `--repo DIR` (repository folder), `--dry-run` (the log shows every command, nothing is run),
`--smoke` (open the window, quit after 1.5 s; used to test the packaged app).

## The three pages

1. **Setup.**
   - Repository folder.
   - Prerequisites checklist with an *Install* button where the app can fix an item.
   - ROM picker: the native file dialog and a path field. The result is shown inline: size 16 MiB, title
     `POKEMON EMER`, game code `BPE?` at offset 0xAC, header checksum at 0xBD, and whether the SHA-1 is the
     USA/Europe 1.0 dump. It is the same check as `fetch_assets.sh`. The ROM is only needed the first time (when
     the game files do not exist yet).
   - Options: target, install folder (Linux), compile jobs (default half the CPU threads), low priority,
     the 12 second start-up test and the container name (Linux).
2. **Build.** Stage chips (prerequisites, container, game files, tools, assets, SDL2, compile, link, package),
   a progress bar (compile uses the `[ NN%]` lines of make), elapsed time, the live log, *Cancel* (stops the
   whole process tree), *Open log file*. A failure shows the known-error explanation. The log is saved:
   Windows `<repo>/build-win/builder-logs/`, Linux `<install dir>/builder-logs/`. Running again resumes: the
   scripts skip finished steps.
3. **Result.** Folder, zip (Windows), launcher (Linux), save folder, *Open folder*, *Launch game*, the Steam
   Game Mode steps from `docs/BUILD_DECK.md`, how to import a save between the Deck and Windows builds, and the
   legal note.

Choices are remembered in `bravely_builder_settings.json` next to the app (or in `~/.config/bravely-builder`
when that folder is not writable). It holds paths and option values only, never ROM contents.

## What it runs

| Host | Target | Commands |
|---|---|---|
| Windows | Windows exe (32-bit) | In MSYS2 (`C:\msys64`): `fetch_assets.sh` (with `FETCH_NO_CONVERT=1`, accepted to stop at "Generating the port's files"), `make -f make_tools.mk` (static host tools), `make -f vita/assets.mk`, `fetch_assets.sh` again, then `tools/windows/build.sh` and `tools/windows/package.sh`. This is the documented order of `docs/WINDOWS_BUILD.md`. When the game files already exist only the last two run. |
| Linux, SteamOS | Linux / Deck 32-bit | `bash tools/deck/build_deck.sh --dir D --jobs N --container C [--rom R] [--no-run]`. The script makes the container, the toolchain, SDL2 and the game. |

Targets that cannot be verified are shown as not available: a Windows exe from Linux, and a Linux build from
Windows (WSL). Only the native target of the host is offered.

Prerequisites (Windows): MSYS2 (official installer, **download only after a consent dialog** that shows the file
name, URL and size; run silently with `--confirm-command --accept-messages --root`), the MSYS2 packages of
`docs/WINDOWS_BUILD.md` (a package group counts as installed when all its members are), the static SDL2 2.32.10
and SDL2_image 2.8.12 (`tools/windows/build_deps.sh`), free disk space (15 GB recommended), network. Git for
Windows is only reported; the build uses the MSYS2 git. (Linux): x86_64, `distrobox` and `podman`
(the exact package command is shown for the distribution; it needs your administrator password, so you run it in a
terminal), the container and libraries (made by the build), free space.

The job count of the Windows build reaches `build.sh` through `OMP_NUM_THREADS` (GNU `nproc` honours it; the
script has no jobs option). Low priority is the Windows `BELOW_NORMAL` priority class or `nice -n 19`.

## Security model

- **No passwords.** The app never asks for, reads or stores a password. If something needs administrator rights
  it shows the exact command to run in a terminal.
- **No shell strings.** Commands are argument lists. For MSYS2 the script text is constant and every path is
  passed as a positional argument (`bash -lc 'script' builder ARG1 ARG2`), so a path cannot inject commands.
  Paths from the window are validated first: absolute, no control characters, no quotes or shell symbols, no
  spaces (the scripts require that).
- **Downloads need consent.** The only download made by the app itself is the MSYS2 installer, after the dialog.
  Everything else is downloaded by the build scripts (SDL2 tarballs are checked against SHA-256 values in
  `build_deck.sh`, the game files against `tools/assets/manifest.txt`).
- **Settings are whitelisted** by key and type.
- The app listens on no port. (An earlier design served a local web page; it was dropped for this desktop app.)

## Layout and extending

```
tools/builder/
  app.py            entry point (Qt)           cli.py   headless driver of the core
  core/             no Qt, no web: tested without a display
    host.py         Windows / SteamOS / Linux detection
    constants.py    facts from the docs: ROM size, package list, URLs
    validate.py     path validation, ROM check, disk, network
    errors.py       table of known failures -> explanation and fix
    settings.py     remembered choices
    tasks.py        Step, Task (event stream, log file), kill_tree
    builder.py      Builder: targets, checks, command construction, runner, installs, results
    repo.py         finding the repository
  ui/               theme.py (dark, emerald), main_window.py (the three pages)
  tests/            test_core.py (unittest), shots.py (renders every page to PNG)
  package.py        PyInstaller packaging      packaging/  Linux install files and the container build script
```

- *A new known error:* add a `(regex, title, fix)` row to `core/errors.py` and a line to `ErrorTableTests`.
- *A new prerequisite:* add it in `Builder._checks_windows` or `_checks_linux` (`add(id, label, status, detail, action=...)`);
  an install action is a branch in `Builder.plan_install`.
- *A new stage:* add a `(regex, stage)` rule to the `Step` and a label in `ui/main_window.py` (`STAGE_LABELS`).
- Tests: `python -m unittest discover -s tools/builder/tests -v`. Set `BRAVELY_TEST_ROM` to run the check against a
  real dump. `BRAVELY_FAKE_HOST=steamos` (only with `--dry-run`) shows the Linux command construction on any machine.
- Headless build: `python tools/builder/cli.py build --target windows --rom X.gba --jobs 8 --low`.
- Screens: `QT_QPA_PLATFORM=offscreen python tools/builder/tests/shots.py OUTDIR` (on Windows also set
  `QT_QPA_FONTDIR=C:\Windows\Fonts`, the offscreen plugin has no fonts of its own).

## Packaging

PyInstaller has to run on the OS it targets.

```sh
python -m venv .venv-builder
.venv-builder/bin/pip install PySide6-Essentials pyinstaller      # Scripts\pip on Windows
.venv-builder/bin/python tools/builder/package.py
```

Windows makes `tools/builder/dist-builder/BravelyEmeraldBuilder-win.zip` (about 40 MB, about 100 MB unpacked).
Linux makes the `.tar.gz` with `install.sh` and the `.desktop` template. On a Steam Deck run
`bash tools/builder/packaging/build_linux_in_container.sh`: it creates a fresh container (never the game's
`emerald-build`), installs Python in it, runs `package.py` and removes the container. The Qt libraries come from
the PySide6 wheel; the container installs the xcb helper libraries Qt needs.

## Relation to the Emerald 3DS web builder

The [Emerald 3DS builder](https://github.com/ZallaxDev/emerald-3ds/tree/main/builder) runs in the browser
(Pyodide) because their game executable is prebuilt and independent of the game data: the builder only packs the
data from the ROM into a `.pak`. Our builds compile the decompiled game with Nintendo's graphics and data linked
into the executable, so a prebuilt downloadable executable would distribute game data, and a 32-bit native
compiler cannot run in a browser. A browser-only builder is therefore not possible here and its code is not
reused (it is also under CC BY-NC-SA 4.0). This app is the closest equivalent: it runs on your computer, uses the
tools of the scripts, and nothing leaves it.

## Not verified

See the test report in the pull request or hand-off: the Linux target was exercised in dry-run only unless a
Deck run is noted there, the MSYS2 install and the packages and SDL2 install buttons were not run on a clean
Windows, and Game Mode start-up is as unverified as in `BUILD_DECK.md`.
