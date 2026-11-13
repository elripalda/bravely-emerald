# Handoff: Steam Deck port of the Emerald Vita 3D port

Date: 2026-11-13. Branch `deck-port` in `F:\emerald-deck` (local only, never pushed to GitHub).

## Next session starter

Read `docs/HANDOFF.md` and `.superpowers/sdd/2026-11-13-deck-visual-phase1-shaders/progress.md`, then continue the phase 1 plan at the Task 4 verification step. Use Marcos' Token Optimizer. Caveman style in chat. Per-task review gates use Sonnet; Opus only for the final whole-branch review.

## What works (verified)

- Stage 1 and 2: the game runs natively on the Steam Deck (32-bit x86 Linux, SDL2 static, desktop GL compatibility context). 2D, 3D voxel world, 3D battles, followers, audio, controller, saves. User confirmed.
- Window: 1280x800, 960x544 picture letterboxed through a `glViewport` macro in `src/platform/deck/compat/vitaGL.h`.
- 60 FPS mode presents every 16.67 ms (baseline in `docs/results/deck-visual-phase1-perf.txt`). Vsync itself is unproven; 30 FPS mode is unproven (see the ledger).
- Tools: `tools/deck/run_autoplay.sh` runs a scripted unattended session on a copy (`build-deck/dist-test`), takes in-engine screenshots (`DECK_SHOT`), runs `tools/deck/perf_report.sh`. `tools/deck/setkey.sh` edits settings. Screenshots are real (Read the PNGs).
- Phase 1 Tasks 1, 1b, 2, 2b, 3 are complete and reviewed (Sonnet). Fix round 1 applied.

## Task 4 state (post effects), NOT verified

- Commit `64febc5` adds `src/platform/deck/deck_post.c/.h`, hooks in `src/platform/voxel/voxel_renderer.c` and `tools/deck_tests/shot_stats.py`. The agent was stopped (user shutdown) before it compared legacy and post screenshots, ran the fallback test (`DECK_POST_FORCE_FAIL`) or measured performance. Its last message was about running a Python script (`/c/Python314` exists on the PC).
- Next: build (`-j4`) and run the brief's runs A-D, read the PNGs, compare legacy vs post, then a Sonnet review gate (brief: `.superpowers/sdd/2026-11-13-deck-visual-phase1-shaders/task-4-brief.md`), then Tasks 5 to 8 of the plan `docs/superpowers/plans/2026-11-13-deck-visual-phase1-shaders.md`.

## Deck facts

- SSH: `ssh deck` (key `~/.ssh/id_ed25519_deck`, alias in `~/.ssh/config`, host 192.168.50.91 at the time; IP may change). The Deck must be awake.
- Everything on the SD card: `/run/media/deck/SD512/emerald-deck/` (`src/` clone, `repo.git` bare, `rom/myemerald.gba`, `deps32/` static 32-bit SDL2).
- Container: distrobox `emerald-build` (Arch), on the internal disk. Build: `ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src && cmake --build build-deck -j4'"`. Sync: `git push deck deck-port:main` here, `git pull --ff-only` on the Deck.
- Game processes: the main thread is renamed `MainThread`, so `pkill -x pokeemerald` never matches. Use `ps -eo pid,args | grep "[.]/pokeemerald"` and kill by pid. Exactly one instance at a time.
- Normal instance runs from `build-deck/dist` (`./run.sh`, DISPLAY=:0 and XAUTHORITY=$(ls /run/user/1000/xauth_*|head -1), `setsid nohup distrobox enter emerald-build -- ./run.sh`). User settings in `dist/userdata/settings.txt`: voxel_3d=1 voxel_battle=1 voxel_fps=1.
- Desktop session: Xwayland under kwin_wayland. GL 4.6 Compatibility, Mesa 26.2.7, GLSL 4.60.
- State at shutdown: no game running on the Deck. Leftover from the stopped agent: `build-deck-dev/` and `build-deck-dev.log` untracked in the repo clone on the Deck; safe to delete.

## Decisions and rulings

All in the SDD ledger (`.superpowers/sdd/2026-11-13-deck-visual-phase1-shaders/progress.md`) and in `docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md` ("Decisions"): both Medium and High presets, fully procedural ambient audio, 3D scene to about 1280x720 in phase 7, grass and flowers reuse existing tiles, effects Deck-only.

## Open items

- Phase 1 Tasks 4 to 8, then phases 2 to 8 of the visual upgrade design.
- Unproven: real vsync, 30 FPS mode, trap on interrupted runs.
- The 415 MB of root-owned leftovers in `SD512/emerald-deck/containers` need `sudo` to remove.
- Intro-edge slivers may need a rebuild of the generated assets (Pillow was installed later).
- Task 8 of the stage 1 plan (host-native run without the container) is not done; the user launched it from Desktop Mode only.
