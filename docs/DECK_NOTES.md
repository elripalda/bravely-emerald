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

## 32-bit SDL2 deps

Arch multilib has no lib32-sdl2. Built static -m32 SDL2 2.32.10 + SDL2_image 2.8.12 in container `emerald-build`.
Prefix: /run/media/deck/SD512/emerald-deck/deps32 ; sources: /run/media/deck/SD512/emerald-deck/build-deps

```
sudo pacman -S --needed --noconfirm lib32-alsa-lib lib32-libpulse lib32-libxcursor lib32-libxi lib32-libxrandr \
  lib32-libxss lib32-libxkbcommon lib32-libpng lib32-libxfixes lib32-libxrender lib32-libxtst lib32-pipewire \
  lib32-libdecor wayland-protocols wayland
cd /run/media/deck/SD512/emerald-deck/build-deps
curl -fsSLO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
curl -fsSLO https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/SDL2_image-2.8.12.tar.gz
tar xf SDL2-2.32.10.tar.gz; tar xf SDL2_image-2.8.12.tar.gz
export PKG_CONFIG_PATH=/usr/lib32/pkgconfig:/usr/share/pkgconfig PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig
P=/run/media/deck/SD512/emerald-deck/deps32
cd SDL2-2.32.10
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release \
  -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$P
cmake --build build -j8 && cmake --install build
cd ../SDL2_image-2.8.12
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH=$P -DCMAKE_INSTALL_PREFIX=$P -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
  -DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF \
  -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_QOI=OFF
cmake --build build -j8 && cmake --install build
# use:
PKG_CONFIG_PATH=$P/lib/pkgconfig pkg-config --cflags --libs --static sdl2 SDL2_image
```
SDL2 backends ON (dlopen at runtime): X11 (+xcursor/xinput/xrandr/xscrnsaver/xfixes), Wayland (+libdecor), KMSDRM, OpenGL, GLES, Vulkan, ALSA, PulseAudio, PipeWire, OSS. OFF: JACK, sndio, ESD, ARTS.
SDL2_image PNG comes from the bundled stb backend (no libpng link). Static libs; libSDL2.a dlopens X11/pulse/wayland, so the final link has only -lm -pthread (+ dl).
Test: SSH has no X auth; run with `XAUTHORITY=/run/user/1000/xauth_* DISPLAY=:0` (else SDL falls back to offscreen driver).

## Task 7: first run (2026-11-13)

Dist: `build-deck/dist/{pokeemerald,run.sh,app0/{voxel,ui,ui2,followers},userdata/}`. POST_BUILD seeds `userdata/settings.txt` from `tools/deck/settings.txt` only when absent.
3D off: key `voxel_3d=0` (and `voxel_battle=0`) in `userdata/settings.txt` (read by `VitaExtras_Init`, vita_extras.c). `pokeemerald.cfg` has no 3D key (`voxelRes` is ignored, `voxelFps` is only the frame cap).
Run over SSH: `DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/xauth_*) XDG_RUNTIME_DIR=/run/user/1000`, in the container. The game runs 20+ s without crash; smoke test passes.
Window: fullscreen 1280x800; the 960x544 picture is letterboxed through `DeckGlViewport` (glViewport macro in compat/vitaGL.h) with a scissor, bars cleared black in `DeckVideo_Swap`.
Open: (1) 8 early `Failed to queue audio: Invalid audio device ID` lines, then none (device opens with pulseaudio/pipewire drivers; alsa fails). Likely calls made before the device is opened; sound not verified by ear. (2) Intro scene shows repeated tile slivers at the left/right edges: the port's "intro margins" generation (fetch_assets.sh step 3) was skipped (needs Pillow). (3) debug.log is absent (VITA_LOGGING off), perf_summary.txt exists but reports Vita labels.
Screenshot: `spectacle -b -n -f -o file.png` on the Deck host (no import/xwd/scrot in the container).

## Phase 1: swap interval (2026-11-13)

- GL_VERSION `4.6 (Compatibility Profile) Mesa 26.2.7-arch1.1`, GLSL `4.60`, renderer `AMD Custom GPU 0405 (radeonsi, vangogh, ACO)`, `vblank_mode` unset.
- Session: kwin_wayland with Xwayland; SDL video driver `x11`. No `~/.drirc`.
- `SDL_GL_SetSwapInterval(1)` after context creation: returns 0, `SDL_GL_GetSwapInterval` = 1.
- Finding: `VitaPlatform_ApplyTargetFramerate` (sdl2.c:1013) calls `eglSwapInterval` before the window exists; SDL answers -1 "Video subsystem has not been initialized". `DeckVideo_SetSwapInterval` therefore remembers an interval only after SDL accepted it, else the later real call at init would be skipped.
- Present pacing (60 and 30 FPS) not yet measured: needs a human route (xdotool input is unreliable, spectacle steals focus). Run the brief's Step 7 by hand.
- Measured with the autoplay harness (tools/deck/run_autoplay.sh): voxel_fps=1 presents at 16.67 ms (p95 16.85, p99 17.62), paced. voxel_fps=0 still presents at 16.67 ms: the interval-2 request only happens before the window exists and fails; nothing re-requests it after init. Needs a fix before the 30 FPS cap works on Deck.

## Phase 1: post presets (2026-11-13)

Task 5 was reduced to reading the code and recording state. No value was tuned and no human judged the look; Marcos will judge all of it in one pass later.

Current values in `src/platform/deck/deck_post.c` (`kPresets`, columns grade, saturation, vignette, bloomGain, threshold, bloomPasses):
- Low: 0.60, 1.04, 0.12, 0.0, 0.70, 1
- Medium: 0.80, 1.06, 0.18, 3.0, 0.65, 1
- High: 1.00, 1.12, 0.30, 5.0, 0.58, 2
- `kShadowTint` (day, dusk, night): {0.94,0.97,1.06}, {1.02,0.90,1.08}, {0.86,0.92,1.12}
- `kHighTint` (day, dusk, night): {1.04,1.01,0.95}, {1.10,0.97,0.84}, {0.97,1.00,1.05}

Checked in code: Medium grade 0.80 is at most 0.85 (faithful plus), and Low `bloomGain` is 0, so Low never blooms (`DeckPost_Run` also skips the bloom passes when `light.bloom` is 0).

Weather with `light.bloom > 0` (`BaseLightFor`, `voxel_renderer.c` lines 2825-2893): SUN 0.10, RAIN 0.08 + 0.30 * bolt (spikes at lightning), SAND 0.16, ASH 0.10, PARTICLES 0.12, SHADE 0.12. FOG and the default (indoor) case have no bloom (0.0).

To judge later (Marcos):
- Dusk and night grade looks heavily teal at the 19:19 clock; shots `.claude-logs/t4/q1.png` and `q2.png`.
- Indoors the post passes `daylight=1, glow=0`, which selects the day tint, not a neutral one.
- `Pass_DayNightGrade` (in-scene tint) stacks with the post grade, so the time-of-day grade is applied twice.
- No noon shot has been taken yet.
- The warp fade-to-black and fade-to-white tint check is not done (the autoplay script has no warp).
- Rain with lightning: bloom spike following `bolt` not observed.

## Phase 2: wind sway (tree crowns, long grass)

- Weights are baked at chunk build: `VoxelBuilder.swayW` (parallel byte array, `voxel_mesh_builder.h`), written in `VoxelBuilder_Tri` from the input vertex height over the crown foot; packed as alpha = 255 - weight in the tree-phase `PackVoxelVertices`. 255 = still. `VoxelVertex` itself is unchanged (building model files are read into it).
- Shader `src/platform/deck/deck_sway.c`: phase from world position (`gl_Vertex.xz + uChunkOffset`, offset = chunk draw offset minus map-crossing shifts), wind from `uWind`/`uTime`. Amplitude constants (to tune by eye): 0.8 tile x strength, damped `0.25 + (s-0.25)*0.5`; weight top 0.5 for crowns, 1.0 for long grass.
- Setting `voxel_sway` (0/1, default 1, EFFECTS group); also off with `voxel_shaders=0` or if the program fails to build (old fixed-function path).
- Verified (field.txt, still.txt, DECK_GL_DEBUG=1): 0 GLERR, `[SWAY] ready`; static camera screenshot pairs differ in the tree rows (13k and 7.5k of 51k samples over 2 to 4 s); crowns intact over trunks and between rows.
- Not checked: crossing a map border (phase continuity), long grass on Route 119/120, rain/sand amplitude, sway with `voxel_sway=0` picture.

## Phase 3: procedural ambient bed

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase3-ambient.md`. Synth: `src/platform/deck/deck_ambient.c` (pure DSP, no game headers). Game mapping and hook: `deck_ambient_game.c`, called from `Platform_QueueAudio` (`sdl2.c`) after the master volume loop, on the game thread.
- Layers: wind (gust-modulated low-passed noise), rain (high-passed noise plus droplet ticks), waves (swell-shaped noise, 6 to 10 s), crickets (4 chirp-train voices), birds (3 voices, swept sine calls), cave (low tone plus drips), thunder (on the lightning edge). Slew 2.5 s per layer (rain 1.5 s), 1 s master fade; indoors, battles, menus (VitaUI, new UI, vanilla start menu window) and fast forward are silent. Thunder tail about 7.5 s (tau 1.2 s); a bolt during a rumble queues one more.
- Setting `ambient_volume` (0 to 10, default 6, 0 = off with no DSP). Final gain = ambient_volume/10 x master volume.
- Data test: `tools/deck_tests/test_ambient.c` (build line in its header, run in the distrobox): NaN, peak, RMS band, clipping, block seam steps, off switch, master 0 silence, fade, fast-forward mute ramp, no write past the frame count.
- Log lines (only with `DECK_GL_DEBUG` set and not 0, at most 2000 lines per process) in `userdata/debug.log`: `ambient: mapType ... -> wind ... master` on every context change and `ambient: 5s out peak rms queue min max` every 5 s. `VITA_LOG` is buffered and never flushed on kill, so use debug.log direct appends for short runs.
- Not checked by ear: loudness against the music, bird/cricket character, wave pacing, thunder delay, crossfade feel, water radius at beaches.

## Phase 4: standing tall grass, player bend, flowers

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase4-grass.md`. Setting `voxel_grass` (0/1, default 1, row "GRASS & FLOWERS" in EFFECTS); also off when `voxel_shaders` is 0 or the sway program failed (`DeckGrass_Enabled`), then the old flat look. Toggling bumps `sWorldEpoch` and the flag is in the chunk content hash, so chunks rebuild.
- Tall grass (E1): cells whose behaviour is MB_TALL_GRASS (and not a tree part) get the long grass tuft (tree atlas 80,32) as a leant card, 0.55 of its height, `EmitTallGrass` in `voxel_tree.c`, tree phase of the chunk worker, one `VoxelLighting_Sample` at the foot (`lightingConstant`). The ground pass is unchanged; `VoxelTree_Part` is unchanged (lighting, entities and battle read it). The GBA tall grass field effect is skipped where a tuft stands (`VoxelTree_TallGrassStands`, `voxel_entities.c`).
- Cap: `CHUNK_TREE_CAP` 768 to 3072 (staging 516 KB worst case; VBOs are sized to the actual count; uint16 vertex indexes top out at 21,504).
- Bend (D3): grass cards carry bit 0 of the sway weight byte (`builder->swayBend`; weight stored even). `VoxelEntities_GetWalkers` = player + nearest 3 visible objects within 4 tiles; uniforms `uBend0..3`, `uFrameOffset` (chunk draw offset, the walkers are in the current frame). Lean 0.6 tile away, sink 0.3, falloff (1 - d/0.9)^2 x weight. No CPU history/spring: recovery is the falloff as the walker moves on.
- Flowers (E2, procedural): the General metatiles that show tiles 508-511 (`VoxelWorld_IsFlowerCell`, read from the metatile entries once) get two small leant cards a cell (hash of the cell picks place and one of 4 colours). Art is painted by `deck_flowers.c` into the free atlas rows py 112..123 when `trees.rgba5551` loads (graded like the rest). Mauville and Ever Grande flowers are NOT covered (offsets not read, several animation phases). The ground keeps its flat animated flowers.
- Debug: `[GRASS] chunk x,y tufts N treeVerts M (built B, cap C) treesMs T` lines in `gl_errors.log` (first 150 chunks).

## Phase 5: rain (GPU streaks C2, wet ground C4, mist C5, splashes C3)

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase5-rain.md`. Code: `src/platform/deck/deck_rain.c/.h`, hooks in `voxel_renderer.c` (`Frame_ReadAtmosphere`, `Pass_Ground`, `Pass_Atmosphere3D`). Setting `voxel_rain` (0/1, default 1, row "RAIN EFFECTS" in EFFECTS); also off with `voxel_shaders=0` or when the programs failed: legacy rain particles and flat ground.
- Streaks: 2048 quads in one static VBO, positions computed in the vertex shader from the same wrapped box as `DrawWeather`; legacy `DrawWeather(rain)` is skipped when they draw. Wet ground: `Pass_Ground` runs a ground program while the wetness `sWet` > 0.01 (ramp 20 s in rain, 90 s to dry, outdoors only), normals from derivatives. Mist: `DrawFogBanks` again, grey-blue, `smoothstep(0.35,1,amount)*0.5*sWet`. Splashes: 1024 ring quads on the camera ground plane (`sCamera.ground + 0.03`), spot re-hashed each cycle in the vertex shader.
- Debug env (read once): `DECK_FORCE_WEATHER=rain|storm|sun|fog|sand` (Deck build only, overrides weather, amount, fog density and bolt in `Frame_ReadAtmosphere`; storm = bolt every 8 s; audio not forced); `DECK_RAIN_OFF=streaks,wet,mist,splash` (comma list). Log: `[RAIN]` lines in `gl_errors.log` (init, every 10 s amount and wet).
- Autoplay: `EXTRA_ENV="DECK_FORCE_WEATHER=rain" tools/deck/run_autoplay.sh tools/deck/autoplay/still.txt 75 <abs outdir>`; wet reaches 1.0 about 30 s after the world appears.
- Verified (Deck, `DECK_GL_DEBUG=1`, 0 GLERR in every run): streaks visible in the 60 s shot; wet ground darker (mean RGB 41/81/83 vs 45/89/91 with `wet` off, 34% of pixels differ) with bluer puddle patches; mist hazier (18% of pixels differ, mean +2 to +3); rings visible on the ground with splashes on; `voxel_rain=0` run for the fallback.
- Not checked: looks and amounts by eye, the storm flash (frame not on a bolt), performance (deferred), battles (rain is not drawn there), splashes on cliffs/water/roofs (they use the camera ground plane, so they float or hide there; relief heights are the follow-up), wet look on every tileset, puddle scale, sand/fog/snow (unchanged).

## Phase 6: particles (F2), water glints, character outline (G4), soft shadows (B4)

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase6-particles.md`. Code: `src/platform/deck/deck_particles.c/.h` (particles, glints), `deck_chars.c/.h` (outline, blobs); hooks in `voxel_renderer.c` (`Pass_Ground`, `Pass_CastShadows`, `Pass_Entities`, `Pass_Atmosphere3D`, entity shadow alpha) and `voxel_entities.c/.h` (`VoxelEntities_GetBlobs`, `VoxelEntities_CharacterRange`, behind PLATFORM_DECK). Settings (EFFECTS, default 1, also off with `voxel_shaders=0` or a failed program): `voxel_particles` ("PARTICLES": pollen, fireflies, leaves, glints), `voxel_outline` ("CHARACTER OUTLINE"), `voxel_shadow` ("SOFT SHADOWS").
- Debug env (read once): `DECK_FORCE_TIME=dawn|noon|dusk|night` (Deck only, `VitaDayNight_Update` overrides light and sun after its own computation, outdoors only; the clock and saves are not touched; consumers of `VitaDayNight_Get/GetSun` see the forced hour, including the ambient bed (it reads `VitaDayNight_Get`, so birds and crickets follow it), grade, glow, motes, sun for shadows). `DECK_PART_OFF=pollen,fire,leaf,glint` single effects off. Example: `EXTRA_ENV="DECK_FORCE_TIME=night DECK_FORCE_WEATHER=sun" tools/deck/run_autoplay.sh tools/deck/autoplay/still.txt 75 <abs outdir>`; `SETKEY="voxel_particles=0"` for the B shot.
- Particles: 288 quads in one static VBO (pollen 192, fireflies 64, leaves 32), seed in the vertex, motion in the vertex shader inside the mote box (22 x 18 around the camera target), wrapped like the rain; three draws (pollen and leaves alpha, fireflies additive). Pollen count follows `light.motes` (sun, daylight) and replaces the 32 legacy motes when drawn; fireflies when `glow` > 0.3 on route and town maps; leaves by the tree vertex count of the visible chunks (not anchored to trees). None in rain, none indoors, underground or in 3D battles; no pollen or leaves in fog, sand, ash or snow. `[PART]` line in gl_errors.log every 5 s under `DECK_GL_DEBUG`: counts per kind (the exit test's particle count).
- Glints: the ground uses a glint program (texture x colour as the fixed path) where the wet program is not bound; sparkles from a cell hash of the world position on blue texels (`b - max(r, 0.8 g)`), strength from sun and glow, none in rain. No builder change (no water flag in the vertices).
- Outline and rim: only the object cards' vertex range goes through the outline program (doors, effects and the occluded-player silhouette stay fixed pipeline). Dark one-texel outline on clear texels beside a sprite texel; warm rim on texels whose neighbour toward the sun is clear. The quad is the sprite rectangle, so the outline only shows inside it.
- Soft shadows: a radial blob (0.8 x 0.52 tiles, peak 0.20 x light of the card) under every outdoor, non-riding card, pushed 0.07 along the sun, drawn before the silhouette shadows whose alpha is x0.62 while blobs are on (peak 0.22; overlap about 0.38 vs 0.36 before).
- Verified: see HANDOFF "Phase 6 status".

## Settings levels and camera tilt (2026-11-14)
Every Deck effect has its own row in WORLD > EFFECTS (ambient volume stays in GENERAL > SOUND). Level encoding of the effect keys, stored by key name in settings.txt: 0 OFF, 1 AUTO (follows `voxel_quality`: LOW/MEDIUM/HIGH), 2 LOW, 3 MEDIUM, 4 HIGH; any other value counts as AUTO. AUTO is 1 on purpose: old files stored 1 for "on", so old 0/1 keys keep their meaning with no migration. Only `voxel_bloom` and `voxel_grade` used 2 for AUTO: `MigrateLevelSettings` (vita_extras.c) maps 2 to 1 when the file has no `fx_levels` key (then writes `fx_levels=1`). The old `voxel_rain=0` also switches off the keys split from it (wet, mist, splash), and `voxel_particles=0` the glints. `voxel_shaders` stays the master switch.

| Row | Key | OFF | LOW | MEDIUM and HIGH |
|---|---|---|---|---|
| COLOR GRADE | voxel_grade | none | preset 0 strength | preset of AUTO/level (HIGH = strongest tint, vignette) |
| BLOOM | voxel_bloom | none | gain 1.5 | gain 3.0 / 5.0 (old presets) |
| DIORAMA BLUR (DOF) | voxel_blur | none | band blur x0.5 | x1 |
| WIND SWAY | voxel_sway | fixed trees (also no grass bend) | amplitude x0.5 | x1 |
| GRASS & FLOWERS | voxel_grass | flat ground | 50% of the cells (hash of the cell) | 100% |
| RAIN STREAKS | voxel_rain | legacy rain | count x0.5 | x1 |
| WET GROUND | voxel_wet | dry | puddles x0.5 | x1 |
| RAIN MIST | voxel_mist | none | opacity x0.5 | x1 |
| RAIN SPLASHES | voxel_splash | none | count x0.5 | x1 |
| PARTICLES | voxel_particles | legacy motes | count x0.5 | x1 |
| WATER GLINTS | voxel_glint | none | strength x0.5 | x1 |
| CHARACTER OUTLINE | voxel_outline | none | strength x0.5 | x1 |
| SOFT SHADOWS | voxel_shadow | silhouette only | blob alpha x0.5 | x1 |
| 3D RESOLUTION | voxel_res | 960x544 | 960 wide | MEDIUM 1120x636, HIGH 1280x726 (AUTO follows the preset) |
| TILT-SHIFT | voxel_tiltshift | the old band blur | radius x0.6, 8 taps | x1 12 taps / x1.4 16 taps |
| LIGHT SHAFTS | voxel_shafts | none | gain 0.35 | gain 0.65 / 0.9 |
| RELIEF (BUMP) | voxel_bump | the old picture (old programs and paths) | multiplier +-10% | +-15% / +-20% |
| LUT GRADE | voxel_lut | the procedural grade | S-curve x0.5 | x1 / x1.3 |
| SUN SHADOWS | voxel_sunshadow | baked cast rays (the old picture) | map 1024, 2x2 PCF | MEDIUM map 2048, 3x3 PCF; HIGH 2048, 5x5 at 1.25 texel spacing |
| BATTLE BACKDROP | voxel_battledim | the crisp battle stage | 1 blur pass, overlay 0.25 | MEDIUM 2 passes, 0.40; HIGH 3 passes at 1.6x step, 0.55 |
| GROUND DETAIL | voxel_groundrelief | none | multiplier amplitude 0.20 | 0.35 / 0.50 |
| UI DEPTH GLOW | voxel_uiglow | none | 8 taps, radius 2 px, 0.12 each | 12 taps (2 rings) 0.10 / 16 taps 0.085 |
| CAMERA TILT | voxel_pitch | CLASSIC 40, 36, 32 (default), 28, 24 degrees (own values, not levels) | | |

MEDIUM equals the look before the levels, HIGH equals MEDIUM except for bloom and grade (which had presets). Bloom LOW is a weak glow instead of none (the LOW preset used to have no bloom).

Camera tilt: `CtrSettings_VoxelPitch()` (voxel_platform stub header, Deck branch) reads `voxel_pitch` every frame through `AdaptDistance`, so a change applies live. The horizontal camera distance is multiplied by cos(pitch)/cos(40) to keep the eye-to-target length (tile size on screen); the chunk rows north of the target grow with the pitch (the top ray of a low camera reaches further): `northTiles` in the chunk scan, max 46. Battle pitch (30) and the swoop (lands at 45 from whatever pitch it starts) are untouched. The DoF band rows (226 top, 127 bottom, tuned at 40) are left as they are.

## Phase 7: tilt-shift, scene resolution, light shafts, LUT grade
Plan: docs/superpowers/plans/2026-11-13-deck-phase7-post2.md. All in `deck_post.c`. The composite shader is one body with `#ifdef POST_X`; it is built with POST_X first and, if that fails, without (the phase 1 composite). Env `DECK_POST_NO_X=1` forces the plain composite (fallback test), `DECK_POST_FORCE_FAIL` the whole legacy path. Texture units: 0 scene, 1 bloom, 2 blur or tilt copy, 3 scene depth, 4 LUT, 5 shafts.
- Tilt-shift: circle of confusion from the linearised scene depth (`2nf/(f+n-(2d-1)(f-n))`, near 1, far 62) around the eye to target distance, `smoothstep(0.06, 0.40, |z-zf|/zf)` with the near side x1.4; gather of 8/12/16 golden-angle taps on the half-res copy weighted by `1 + 3 lum^2`, radius up to 1.4 percent of the height. Because it uses the real depth it follows `voxel_pitch` (no screen rows). Replaces the band blur when on and the scene target has a depth texture, outdoors; indoors both are off.
- 3D resolution: scene target 960 / 1120 / 1280 wide (height 544 / 636 / 726), recreated by `DeckPost_WantScene` when `voxel_res` changes (a failed create goes back to the old size and is not retried). The 3D viewport takes the target size, the projection aspect stays 960/544, so every mapping of the logical 960x544 (UI mirrors, tags) and `deck_shot.c` (reads the window) are unchanged. At 960 the composite samples NEAREST, above that LINEAR. The half-res blur steps scale with the width. UI and 2D stay 960x544.
- Light shafts: half-res radial march (24 taps, decay 0.95, bright part above luminance 0.62, jittered start) toward a fixed point above the picture (x = 0.5 - sun dx x 0.25, y = 1.3), added warm in the composite before the grade. Amount = max(sun strength, glow) x clamp(daylight / 0.25); zero in rain, fog (density 0.05), sand, ash, indoors, underground and at night. The true sun position is above and beside the frustum at these pitches, so a projection would be unstable.
- LUT grade: 256x16 RGB8 strip (16 slices of blue) built on the CPU: the procedural grade (tint by luminance, saturation, strength of the preset) plus an S-curve (0.20 day, 0.32 dusk, 0.14 night, x level); rebuilt when daylight or glow moved more than 0.004 or a level changed. Sampled with two bilinear taps; the fade stays a mix in the shader; the vignette stays analytic. `voxel_lut` off: the old grade code, unchanged.
- Verified: see HANDOFF "Phase 7 status".

### Adding a setting
A leveled row is three edits: (1) a `FAST_EXTRA_VOXEL_<NAME>` id in `include/platform/vita_extras.h` (Deck block), (2) one `if (strcmp(key, "voxel_<name>") == 0) return ...` in `LookupFastId` (vita_extras.c, case 'v'), (3) one row in `gVitaExtras` with `5, 1, sVoxelLevelValues` (count 5, default AUTO). In the reader include `deck_levels.h` and use `DeckLevel(id)` (-1 off, else 0..2) or `DeckLevelPick(id, low, medium, high)` (0 when off). Labels: at most about 18 characters (the label column is 156 px of the 320 px menu); values at most 8. Tab row arrays hold 48 rows (vita_ui_options.c) and 64 (ui_scr_options.c).


## Phase 9: relief (voxel_bump)
Pseudo normal map, Deck only. The height is the luminance of the albedo texel; a 3x3 Sobel on the texel neighbours (nearest, integer offsets, so the pixel art stays crisp) tilts a per pixel normal built from the screen derivatives; the sun from `gVoxelSun` (toward-the-sun = -dx, up, -dz, 45 degrees) lights it, a cavity term adds light independent depth, and the result is a multiplier around 1.0 on the colour each program already computes (LOW 0.10, MEDIUM 0.15, HIGH 0.20). Dusk and night keep a third of the directional term. Where the baked vertex colour is dark the directional part fades (no double darkening with cast shadows); in rain the relief is scaled by 1 - 0.4 x wet and the wet sheen is untouched. Shared GLSL and the plain program for buildings: `deck_bump.c`. Surfaces: ground (wet and glint programs, plus the wet program at wetness 0 for dry ground without glints), building pages (plain bump program), trees and grass (sway program, amplitude 0 when sway is OFF), character cards (outline program, outline alpha 0 when the outline is OFF). Not done: parallax (sub-texel offsets tear nearest edges and move alpha tested edges; not judgeable without tuning rounds). Plan: docs/superpowers/plans/2026-11-14-deck-phase9-bump.md. Building pages have no slot (a clear neighbour takes the centre height), so seams between pieces of building art can show faint lines (the gradient is soft clamped).

## Phase 8: sun shadow map (voxel_sunshadow)
Plan: docs/superpowers/plans/2026-11-14-deck-phase8-sunshadow.md. Code: `deck_sunshadow.c/.h`, `POST_SUN` block in the composite of `deck_post.c`, `Pass_SunShadowMap` in `voxel_renderer.c`, `VoxelLighting_SetNoCast` in `voxel_lighting.c`. Casters are drawn depth only (fixed pipeline, alpha test for crowns and cut-out building pages, character cards) from the sun into a texel-snapped 64 x 64 tile box around the camera target. The receivers are all resolved in the composite from the scene depth texture (unit 3) and the map (unit 6): world position from the inverse of P * MV, normal from the derivatives (forced to face the viewer), slope scaled bias and normal offset, PCF, edge fade, multiplier = ambient / 0.9 inside shadow. A face turned from the sun (n.L <= 0.05) is skipped (its baked face shade is already dark). Not drawn at sun strength < 0.04 (night), in battles, indoors, underground.
- Double darkening: while on (>= LOW and the composite built with `POST_SUN`), `Lit()` returns true at once, so the chunk builder skips every baked cast shadow ray; contact AO and the sun angle face term stay. The flag is part of `VoxelLighting_Hash` and a change bumps `sWorldEpoch` (worker idle first), so the chunks rebuild when the setting changes. OFF is the old baked picture.
- Older shadows: the flat character cast silhouettes (`EmitCastShadow`) are skipped while the map of the last frame was drawn (the characters cast through the map); the soft blob (`voxel_shadow`) and decal shadows stay.
- Constants (`deck_sunshadow.c`): SS_HALF 32, SS_DIST 48, SS_RANGE 96, SS_FACE_REF 0.90, SS_MIN_SUN 0.04; bias `tw * (1 + 3 sinT / max(n.L, 0.25))` capped at 10 texels (composite). Fallbacks: no `POST_SUN` composite or no depth texture -> off and the baked rays come back; env `DECK_SUNSHADOW_OFF=1`. Log: `[SUNSHADOW]` lines (every 300 frames the map pass CPU ms and draw counts).
- Limits: ground and cliffs are not casters; no sway in the map; tall grass tufts share the tree range so they cast too; one box, no cascade; the bloom and tilt-shift copies are taken from the unshadowed scene.

## Phase 10: battle backdrop blur and dim (voxel_battledim)
In a 3D battle the 3D stage is the backdrop only: the battlers, the HP boxes and the text come from the GBA overlay (`Pass_Overlay2D`), drawn after the post pass, so they stay sharp. `DeckPost_WantScene(..., battle)` now lets the scene go through the post target in a battle when `voxel_battledim` is on; `DeckPost_Run` with `in->battle` turns every other effect off (no grade, bloom, tilt-shift, shafts, LUT, sun) and only blurs the scene copy (half resolution, 1 / 2 / 3 passes, step 1 / 1 / 1.6) and lays black over it (0.25 / 0.40 / 0.55), both scaled by the screen fade. Uniform `uBack` in the composite. Constants `kBackPasses`, `kBackStep`, `kBackDim`. OFF = the crisp stage as before.

## Phase 11: stronger relief on models (voxel_bump)
Amplitudes 0.10 / 0.15 / 0.20 -> 0.18 / 0.30 / 0.45; in the GLSL block the sun term gain 1.6 -> 2.8, the cavity 3.0 -> 4.5, the tilt of the normal 3.0 -> 5.0, the night floor of the lit factor 0.4 -> 0.55. OFF unchanged.

## Phase 12: ground micro-relief (voxel_groundrelief)
`deckGroundMul` in the shared block (deck_bump.c), called by the two ground programs (wet and glint; `DeckRain_BumpBegin` also runs for this setting alone). The ground kind comes from the texel colour (grass: green over red and blue; sand: bright and red over blue; path: the rest; blue texels = water get none): grass soft clumps (noise at 1.5 and 3.6 per tile), sand ripples (a warped sine), path grain (9 and 17 per tile) plus a low frequency undulation (0.16 and 0.07 per tile) that tilts the light slowly. Finite differences of the height give a normal, lit by `uBumpSun` (world space), fades on cliffs and walls by the derivative normal. Shading only, no vertex change. Amplitude 0.20 / 0.35 / 0.50. No tile or slot id exists in the ground vertices, so the colour classifies it.

## Phase 13: animated sun and moon rays (voxel_shafts)
The radial march over the scene is gone. `kShaftFs` draws procedural beams at half resolution: 3 / 4 / 6 layers of slanted bands (constant along x + slant * y, the slant from the sun focus), each with its own density, drift speed and direction, breathing (a sine) and dust sparkles inside the beams, strongest at the top. Colour: warm by day, golden at dawn and dusk (glow), pale blue for the moon at night with motion x0.45. Weather factor `wf` (renderer): sun 1.0, clear 0.8, fog or fog density >= 0.05 1.4, shade 0.45, sand 0.5, rain 0, others 0.35; the moon amount is (1 - daylight) x 0.5 x wf. Gain by level as before (0.35 / 0.65 / 0.9).

## Phase 14: UI depth glow (voxel_uiglow)
`deck_uiglow.c`: black, faint copies of the UI drawn first around a short offset (1, 2.5 px) in 1 or 2 rings. Two hooks: `Ui_Render` (the new UI list: menus, HUD; a black copy of the vertex list with the element's alpha x the tap opacity, drawn N times with a modelview shift) and the GBA overlay quad in `voxel_renderer.c` (dialogue, battle: `glColor4f(0,0,0,alpha)` on the same quad). The 3D scene and the UI itself are untouched. The battle overlay also holds the battlers' sprites, so they get the halo too. Env `DECK_UIGLOW_OFF=1`.


## Intro and menu effects (voxel_flatfx, deck-title branch)
Row INTRO & MENU EFFECTS (OFF / AUTO / LOW / MEDIUM / HIGH), env `DECK_FLATFX_OFF=1`. Before: every non-3D frame was drawn by `VDraw` + `RenderGameTexture` (sdl2.c) straight into framebuffer 0; the post chain (`DeckPost_WantScene` / `DeckPost_Run`) runs only inside `VoxelRenderer_RenderFrame` (`f.postActive`), so the intro, title and menus never saw it. Now `DeckFlat_Begin` (after the clear in `RenderGameTexture`) binds a capture target the size of the letterboxed picture in window pixels; the game quad and the wide margin art (`VitaStage_*`, margin fades) draw into it unchanged; `DeckFlat_End` composites 1:1 with NEAREST (pixel art stays sharp) and `Ui_Render` still draws on top, unprocessed. `deck_flatfx.c`: bright pass, half-res bloom (2 blur passes), quarter-res wide glow of bright and saturated texels (stands in for a sprite glow, there is no sprite mask), split-tone grade, saturation, S-curve, vignette, film grain, a slow light pulse. Bloom and glow only add what lies beyond the base (`max(bloom - base, 0)`), after the first run washed the white menu boxes out (docs/screens/flatfx_mainmenu_first_bloom_washout.png). Level tables at the top of the file: bloom 0.18/0.28/0.40, threshold 0.80/0.76/0.72, glow 0/0.12/0.20, vignette 0.14/0.22/0.32, grain 0/0.010/0.018, pulse 0/0.010/0.018. Not applied: the new UI (Ui_Render), the native menu path (`ShowNativeUi`), 3D frames, battles. No CRT scanlines (they would break the pixel grid). Fallback: shader or target failure logs `[FLATFX]` and draws straight as before.
Intro slivers (repeated tile slivers at the left and right edges of the leaves scene, noted above around line 116): still present in this build (docs/screens/flatfx_intro_leaves_no_margins_slivers.png) because `assets/ui/intro_margins.bin` was not in `dist/app0/ui` (`tools/ui_gen/gen_intro_margins.py` was never run: fetch_assets step 3 needed Pillow). Generated in the container with python3 and copied to the dist, the slivers become continuation art (flatfx_intro_leaves_with_margins_off.png); left over: a thin dark vertical band at the lower left (window x 95-130) and a few leaf shapes cut at the right edge. The Deck build should run the generator once (build_vita.sh does for the Vita).

## Title screen (voxel_title, deck-title branch)
Row TITLE SCREEN (ORIGINAL / BRAVELY, default BRAVELY), env `DECK_TITLE_ORIGINAL=1`. The original composition stays (docs/screens/title_original.png vs title_bravely_2dhd_phase3.png).
- Hook points: `title_screen.c` exports `gDeckTitlePhase` (0 off, 1 logo shine from the start of the fade-in, 2 version banner, 3 PRESS START, 4 leaving); the state machine, input, music and exits are untouched. `VDraw` (sdl2.c) asks `DeckTitle_Wanted()`; when true it hides BG0 and BG1 (`gFastDrawDispcntClear`), keys the backdrop to magenta 0x7C1F (`gFastDrawBackdropOverride`) and clears the backdrop blend bits, so the frame texture holds only BG2 (POKEMON logo, shine window effect) and the sprites (EMERALD VERSION, PRESS START, copyright) exactly as the game draws them, with its fades and blinking. `RenderGameTexture` then calls `DeckTitle_Draw` instead of drawing the quad.
- `deck_title.c` reads the layers the game loaded into emulated VRAM plus the unfaded palette when the title starts (BG0 charbase 2 / screenbase 26: sky bands are bank 14 indexes 3..10, silhouette 11, eyes 2, animated markings 15; BG1 clouds charbase 3 / screenbase 27). Back to front: smoothed sky gradient, far clouds (mip bias blur, slower), Rayquaza as a sprite (sharp-bilinear sampling: crisp texels with sub-pixel drift, warm rim light, markings take the live palette colour so the pulse stays), near clouds, light rays from the upper right, motes and bokeh discs (sines of the clock), the game's palette fade, the BRAVELY and 2D-HD lettering, then the front layer: soft glow behind, rim highlight on upper left edges, a shine sweep every 7 s. Rayquaza and the cloud overlay appear with PRESS START like the original.
- Lettering: the game's own font (`gFontNormalLatinGlyphs`, raw 2bpp rows: high byte = left four pixels, leftmost pixel in the top bits; 1 face, 2 shadow, 0 and 3 clear), emboldened, scaled with nearest sampling, outlined: BRAVELY in the EMERALD VERSION look (white, grey underside, navy outline) at x4 left of EMERALD, 2D-HD in the POKEMON logo look (yellow, blue outline) at x3 right of VERSION. The logo art has no B or Y, so letters could not be reused for the whole word; one font gives one consistent look.
- Test: `tools/deck/autoplay/title.txt` (one START at 9 s skips the intro), shots 13 s (phase 1, shine), 19 s, 34 s (phase 3); ORIGINAL run with `SETKEY='voxel_title=0 voxel_flatfx=0'`. Fallback: build failure or art not found in VRAM logs `[TITLE]` and draws the original. Run scripts relaunch a game from the worktree dist afterwards: kill it by /proc cwd so only the main clone game stays.
