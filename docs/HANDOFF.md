# Handoff: Steam Deck port of the Emerald Vita 3D port

Date: 2026-11-13. Branch `deck-port` in `F:\emerald-deck` (local only, never pushed to GitHub).

## Next session starter

Read `docs/HANDOFF.md` and `.superpowers/sdd/2026-11-13-deck-visual-phase1-shaders/progress.md`, then continue the phase 1 plan: Task 4 Sonnet review gate and warp-fade tint check, then Task 5. Use Marcos' Token Optimizer. Caveman style in chat. Per-task review gates use Sonnet; Opus only for the final whole-branch review.

## What works (verified)

- Stage 1 and 2: the game runs natively on the Steam Deck (32-bit x86 Linux, SDL2 static, desktop GL compatibility context). 2D, 3D voxel world, 3D battles, followers, audio, controller, saves. User confirmed.
- Window: 1280x800, 960x544 picture letterboxed through a `glViewport` macro in `src/platform/deck/compat/vitaGL.h`.
- 60 FPS mode presents every 16.67 ms (baseline in `docs/results/deck-visual-phase1-perf.txt`). Vsync itself is unproven; 30 FPS mode is unproven (see the ledger).
- Tools: `tools/deck/run_autoplay.sh` runs a scripted unattended session on a copy (`build-deck/dist-test`), takes in-engine screenshots (`DECK_SHOT`), runs `tools/deck/perf_report.sh`. `tools/deck/setkey.sh` edits settings. Screenshots are real (Read the PNGs).
- Phase 1 Tasks 1, 1b, 2, 2b, 3 are complete and reviewed (Sonnet). Fix round 1 applied.

## Task 4 state (post effects): runs verified, review gate and warp-fade check pending

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

- Phase 1 Tasks 1-8 implemented and reviewed (Sonnet, PASS with minors). Deferred to one end pass with DECK_GL_DEBUG: perf compare (legacy/low/medium/high, plan Task 7 Step 3), tuning by eye (see DECK_NOTES "Phase 1: post presets"), warp fade-tint check, Vita build (no VitaSDK here). Then phases 2 to 8.
- Unproven: real vsync, 30 FPS mode, trap on interrupted runs.
- The 415 MB of root-owned leftovers in `SD512/emerald-deck/containers` need `sudo` to remove.
- Intro-edge slivers may need a rebuild of the generated assets (Pillow was installed later).
- Task 8 of the stage 1 plan (host-native run without the container) is not done; the user launched it from Desktop Mode only.

## Phase 2 status (wind sway)

Done and verified on the Deck (0 GLERR, screenshots read): see docs/DECK_NOTES.md "Phase 2". Plan: docs/superpowers/plans/2026-11-13-deck-phase2-wind.md. Visual pass: judge amplitude, check a map crossing and Route 119/120 grass.

## Phase 3 status (ambient audio bed)

Done and verified by data on the Deck: see docs/DECK_NOTES.md "Phase 3". Plan: docs/superpowers/plans/2026-11-13-deck-phase3-ambient.md. `test_ambient` PASS, still.txt run: 0 GLERR, ambient context and 5 s stat lines in debug.log, queue 6.4k to 11k bytes with the bed on and off alike. Listening pass needed: levels against music, bird and cricket character, waves, thunder, indoor fade; setting AMBIENT VOLUME (GENERAL tab).

## Autonomous run (user asleep, from 2026-11-13 night)

User ruling: full autonomy through all remaining phases (2 to 8) of `docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md`; ask nothing, decide and record below; visual checks, tuning and the perf compare happen when the user wakes. Reviews use Sonnet only. Ledger: `.superpowers/sdd/2026-11-13-deck-visual-phase1-shaders/progress.md` (git-ignored). Rules: one Deck run at a time, no detached loops, absolute outdirs for `run_autoplay.sh`, exactly one normal game instance at the end.

### Autonomous decisions
(append: date, phase, decision, why, cost if wrong)
- 2026-11-13, phase 2: Sway weight lives in a parallel byte array in the builder (`swayW`), not in `VoxelVertex`. Why: building model files are `fread` straight into `VoxelVertex` (`voxel_building.c:517`), so the struct cannot grow. Cost if wrong: none visible; one more scratch array.
- 2026-11-13, phase 2: Weight packed into the tree vertices' alpha as 255 - w (65..255), so untouched geometry and legacy path stay alpha 255. Why: no vertex format change, and alpha test GREATER 0 with blending off ignores it. Cost: if a later pass blends trees, they would turn translucent on the legacy path.
- 2026-11-13, phase 2: Wave phase uses world position (vertex + chunk draw offset - total map-crossing shifts). Why: the frame is re-based on each crossing, so frame coordinates would jump the phase. Cost: float drift is negligible; untested across a real crossing.
- 2026-11-13, phase 2: New setting `voxel_sway` (0/1, default on), gated by `voxel_shaders`. Why: spec asks for an off switch, same pattern as `voxel_bloom`. Cost: one more menu row in EFFECTS.
- 2026-11-13, phase 2: Amplitude: crowns top 0.5 x 0.8 tile x strength, long grass 1.0 x; rain damped by half. Why: spec says "a couple of texels" for trees, more for grass; no tuning rounds per ruling. Cost: may be too weak or strong; constants in `deck_sway.c`.
- 2026-11-13, phase 2: Added `tools/deck/autoplay/still.txt` (continue save, then stand still) for static-camera screenshot pairs. Why: field.txt walks, so pairs are not comparable.
- 2026-11-13, phase 3: Ambient bed is fully procedural (no samples, no owl, no recorded birds; birds are swept-sine chirps). Why: spec "fully procedural", no licensed files, none chosen. Cost if wrong: birds sound synthetic; swap in CC0 WAVs later via the same layer targets.
- 2026-11-13, phase 3: Mixed in `Platform_QueueAudio` (`sdl2.c`) after the master volume loop and after the "queue full, drop newest frame" return, via `DeckAmbient_Frame` (`deck_ambient_game.c`) behind `PLATFORM_DECK`. Why: frame count and the VBlank PLL stay untouched, a dropped frame mixes nothing. Cost if wrong: none for pacing; the bed passes through the DC blocker with the music (harmless).
- 2026-11-13, phase 3: Setting `ambient_volume` 0 to 10, default 6, 0 = hard off (no DSP, no state change), clamped at use, row "AMBIENT VOLUME" in GENERAL/SOUND, final gain = volume/10 x master volume. Why: spec 0-10 default 6. Cost: GENERAL tab has one more row.
- 2026-11-13, phase 3: Level calibrated by data, not ears: per-layer gains set so a single layer at gain 1 sits at -30 to -39 dBFS RMS (water -27), everything at once -24 dBFS RMS with peak 0.30, soft limiter x/(1+|x|), hard clamp at +-1 after the add. At the default 6 this is roughly 15 to 20 dB under typical music. Why: no listening possible. Cost if wrong: too quiet or loud; constants are the `* 0.9f`, `* 0.75f` etc. in `deck_ambient.c`.
- 2026-11-13, phase 3: Context rules (6 Hz updates): outdoor map types (town, city, route, ocean route) play wind, birds by daylight (ramp 0.25 to 0.75), crickets by darkness (ramp 0.6 to 0.1), rain by `VoxelWorld_WeatherAmount`, sand wind, thunder on the rising edge of `VoxelWorld_Lightning` > 0.5; waves always on ocean routes and elsewhere from a 9x9 metatile water scan; underground maps get cave tone and drips; indoors, battles, menus, transitions (anything but `CB2_Overworld`) fade out over 1 s. Why: spec table. Cost: the bag/party screens (a different callback) fade the bed out; secret bases and underwater are silent.
- 2026-11-13, phase 3: Fast forward mutes the bed (state reset, no DSP) and it fades back in over 1 s. Why: queue is cleared there. Cost: none.
- 2026-11-13, phase 3: Ambient log lines are written straight to userdata/debug.log with open/close per line, not `VITA_LOG`. Why: `VITA_LOG` is a 32 KB fully buffered stream that is never flushed, so short autoplay runs show nothing. Cost: one fopen per context change and per 5 s.
