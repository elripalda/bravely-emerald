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

Done and verified by data on the Deck (fix round 2026-11-13 applied: thunder queue, log gate, menu fade, see Autonomous decisions): see docs/DECK_NOTES.md "Phase 3". Plan: docs/superpowers/plans/2026-11-13-deck-phase3-ambient.md. `test_ambient` PASS, still.txt run: 0 GLERR, ambient context and 5 s stat lines in debug.log, queue 6.4k to 11k bytes with the bed on and off alike. Listening pass needed: levels against music, bird and cricket character, waves, thunder, indoor fade; setting AMBIENT VOLUME (GENERAL tab).

## Phase 4 status (standing tall grass, player bend, flowers)

Done and verified on the Deck: see docs/DECK_NOTES.md "Phase 4". Plan: docs/superpowers/plans/2026-11-13-deck-phase4-grass.md. Runs with DECK_GL_DEBUG=1: 0 GLERR (still2, grass3, grass4), Route 101 reached by `tools/deck/autoplay/grass.txt`, screenshots read (tufts standing and swaying, flowers standing in Littleroot), a wild encounter fired in the grass (`grass_in.txt`). Chunk trees phase 0.1 to 0.2 ms; Core 0 build 0.31 to 0.32 ms/chunk vs 0.31 in phase 2 (same content); worker 0.47 ms/chunk on grass vs 0.40-0.45 in phase 2 (other content). Visual pass: tuft height (0.55), flower size/colours, bend amount (0.6 tile, no spring), tuft look over each tileset's ground, Mauville/Ever Grande flowers (not done). Bend was not seen in a screenshot (the player never stood in grass at a shot time).

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
- 2026-11-13, phase 4: Tall grass is classified by behaviour (`MetatileBehavior_IsTallGrass`, not a tree part) and gets the long grass tuft standing on top of the unchanged flat ground, 0.55 of its height, no per-tileset tint. Why: spec E1; `VoxelTree_Part` is read by lighting, entities and battle, so it was not touched; behaviour and encounters unchanged. Cost if wrong: tuft over the ground's own tall grass art doubles the look; tint missing on odd tilesets.
- 2026-11-13, phase 4: Grass cards carry a bend bit in the sway weight byte (weights stored even, bit 0 = bend; scale 95 x 2), trees clear. Why: trees and grass share the weight, bend must only move grass; alpha stays 64..255 so the legacy alpha test is safe. Cost: weight resolution 2/190.
- 2026-11-13, phase 4: Player bend takes the player plus the 3 nearest visible objects within 4 tiles (follower and NPCs) as 4 uniforms, no CPU history or spring. Why: smallest correct D3; the falloff over the walker's step is the recovery. Cost: grass snaps up without overshoot.
- 2026-11-13, phase 4: GBA tall grass field effect is skipped where a tuft stands (like long grass). Why: two layers of grass otherwise. Cost: the walker is covered only by its own cell's tuft in front.
- 2026-11-13, phase 4: `CHUNK_TREE_CAP` 768 to 3072 (spec), staging 516 KB worst case, uint16 vertex offsets max 21,504. Observed per chunk: 138 to 636 tree vertices, cap never hit.
- 2026-11-13, phase 4: Tufts take one `VoxelLighting_Sample` at the foot (lightingConstant) instead of rays per corner. Why: spec budget. Cost: tufts do not show cast shadows of their own.
- 2026-11-13, phase 4: Setting `voxel_grass` (0/1, default 1, row "GRASS & FLOWERS"), off when shaders are off or the sway program failed; flag in the chunk hash and a change bumps `sWorldEpoch`. Cost: one more EFFECTS row.
- 2026-11-13, phase 4: Flowers (E2) done as procedural pixel-art cards, not the spec's atlas cut-out: the new id range, cut-out compose and animation tracking in `voxel_atlas.c` was M-L risk for the whole ground pass and unverifiable without tuning rounds. Classification by tile content (General metatile entries with tile 508-511, read once in `VoxelWorld_IsFlowerCell`); art painted by `deck_flowers.c` into free rows py 112..123 of the tree atlas at load (graded like the rest); two cards a cell, place and colour from a hash of the cell. Mauville and Ever Grande flowers NOT covered. Cost if wrong: flowers do not copy the ground art and do not animate (the flat ground flowers still do); the atlas rows are taken.
- 2026-11-13, phase 4: Autoplay `grass.txt` is a blind path (no map knowledge) that happened to reach Route 101; `grass_in.txt` steps into the grass. Why: no warp or teleport tool. Cost: depends on NPC positions; may need retuning.
- 2026-11-13, phase 3 fix round: Thunder guard is `sThunderDelay == -1` (idle); a bolt during a rumble sets a queue flag and the next rumble starts after the current one ends (0.3 to 1.5 s delay), so the playing envelope never restarts. Why: simplest that cannot step the signal; tested in `test_ambient.c`. Cost: back-to-back bolts give a longer continuous rumble instead of a louder one.
- 2026-11-13, phase 3 fix round: Thunder tail kept at about 7.5 s (tau 1.2 s), plan and notes now say so. Why: a long roll reads as thunder; 2.5 s sounded like a burst on paper. Cost: may feel long; one constant (`thunderDecay`).
- 2026-11-13, phase 3 fix round: Ambient log lines gated on `DECK_GL_DEBUG` (set and not "0", same rule as deck_video.c) and capped at 2000 per process; stats loop and hard clamp moved inside the audible branch, so `ambient_volume` 0 leaves the music buffer bit-identical (tested). Cost: when audible, the clamp of the music at +-1 still applies; with bed at 0 the music is unclamped as before the phase.
- 2026-11-13, phase 3 fix round: Menus fade the bed out (1 s master fade) when `VitaUI_IsOpen() || Ui_IsOpen() || GetStartMenuWindowId() != WINDOW_NONE`. Why: on Vita/Deck `ShowStartMenu` opens VitaUI, not a task, and `gMain.callback2` stays `CB2_Overworld`; the vanilla start menu keeps a window id. Dialogue and scripts (`ArePlayerFieldControlsLocked`) deliberately do not fade, they are not menus. Cost: ambient continues in a bare overworld text box and in menus not covered by these three flags (not found).
- 2026-11-13, phase 3 fix round: sTime wraps at 100 s (a whole number of periods of both gust sines, no jump), Init clears sWave and sCricket, rain drop envelope floors to 0 below 1e-9. The per-5 s fopen is untouched (now only runs with DECK_GL_DEBUG).
