# Deck visual phase 5: rain (C2 streaks, C4 wet ground, C5 mist, C3 splashes) plan

Spec: `docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md` section 3.c (C2 to C5), phase 5 row of section 5.
Scope: Deck and PC only. All new code in `src/platform/deck/deck_rain.c/.h`, hooks in `voxel_renderer.c` under `PLATFORM_DECK`.
One commit per effect, in this order: C2, C4, C5, C3 (the first commit also carries the plan, the setting and the debug env).

## Common

- Setting `voxel_rain` (0/1, default 1, EFFECTS row "RAIN EFFECTS"), `FAST_EXTRA_VOXEL_RAIN` after `FAST_EXTRA_VOXEL_GRASS`.
  `DeckRain_Enabled()` = setting on, `voxel_shaders` on, rain programs built. Off or failed: the legacy rain particles and flat
  ground, nothing else changes.
- Each effect also has a runtime off switch for A/B shots: env `DECK_RAIN_OFF=streaks,wet,mist,splash` (comma list, read once).
- Debug weather: env `DECK_FORCE_WEATHER=rain|storm|sun|fog|sand` read once in `Frame_ReadAtmosphere` (Deck build only). It
  overwrites `f->weather`, `f->weatherAmount`, `f->fogDensity`, `f->bolt` before `LightFor`. `storm` = rain at amount 1 and a
  synthetic bolt every 8 s (0.35 s decay). The audio layer reads `VoxelWorld_*` itself and is not forced.
- State in `deck_rain.c`: `sWet` (0..1) ramps up over 20 s of rain (times amount) and dries over 90 s, only advanced outdoors.
  `DeckRain_Update` runs once a frame from `Frame_ReadAtmosphere` (before the ground pass).
- Frame geometry is the one `DrawWeather` uses: box 24 x 8.5 x 28 around the camera target (`voxel_tuning.h:70-77`), same
  `fmodf` wrap, so GPU streaks and legacy particles agree in volume. All programs are GLSL 1.20 on fixed-function arrays
  (`gl_Vertex`, `gl_MultiTexCoord0`), drawn from a static VBO, depth test on, depth write off, alpha blended, in
  `Pass_Atmosphere3D` (inside the post scene target, so the 2D overlay and UI stay clean and fades stay untinted).

## C2 streaks

- Static VBO: 2048 quads, vertex = seed xyz in `gl_Vertex`, corner (x -1..1 across, y -1..1 along) in texcoord.
- VS: wrapped position per drop (as `DrawWeather`), velocity (-2.2,-15,0.6), billboard across `cross(velDir, toCamera)`, width
  `max(0.035, dist*0.0022)` (never thinner than about a pixel, alpha scaled back when widened), per-drop length 0.8..1.25, fade at the
  box edges and within 2 to 4.5 units of the camera. FS: soft profile across and along. Colour = rain colour x day/night light on the
  CPU (as `DrawWeather`), alpha `0.45*(0.5+0.5*amount) + 0.16*bolt`. Drops drawn = `amount * 2048`, min 25 percent.
- Legacy `DrawWeather(&sRainKind)` is skipped when the streaks drew.

## C4 wet ground

- `Pass_Ground` binds a ground program while `sWet > 0.01`, outdoors. VS passes eye-space position, chunk-local position, uv, colour.
  FS: texture x colour, then wet look weighted by how upward the surface faces (normal from `dFdx/dFdy` of the eye-space position, no
  normal attribute): albedo x (1 - 0.32 wet), saturation x (1 + 0.25 wet), value-noise puddles (world position from the chunk draw
  offset) darken more and reflect more, fresnel sheen with a pale sky colour scaled by the day light. Cliff sides barely change.

## C5 mist

- `DrawFogBanks(sFogBanks, ...)` again in rain, colour grey-blue x day light, amount `smoothstep(0.35, 1, amount) * 0.5 * rainRamp`.
  No new geometry. Gated by `DeckRain_Enabled` and the `mist` switch.

## C3 splashes

- Static VBO of 1024 ring quads on the camera ground plane (`sCamera.ground + 0.03`, the cheap option of the spec). VS: each quad has
  a period of 0.8 to 1.3 s and re-picks its x,z in a box 20 x 24 around the target from a hash of its cycle number; the ring is alive for
  the first 0.35 s. FS: ring and two droplets drawn from the uv, no texture. Count = `amount * 1024`. Cost if wrong: floats over
  ledges, water, roofs; heights from relief (`VoxelRelief_*`) are the follow-up, not this phase.

## Verification (Deck, one run at a time)

Build `-j4`; `still.txt` 75 s, `EXTRA_ENV="DECK_FORCE_WEATHER=rain"`, with and without each effect (`DECK_RAIN_OFF=...`), and one
`storm` run. Read `gl_errors.log` (0 GLERR with `DECK_GL_DEBUG=1`) and the PNGs. Not in this phase: tuning by eye, perf compare,
relief heights for splashes, rain on the battle scene.
