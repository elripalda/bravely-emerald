# Phase 9: pseudo normal map (voxel_bump)

User ruling (2026-11-14): "a bit of normal maps so the models etc have some heights, some tiny bit of extrusion to make
them have more depth". Steam Deck and PC only (`PLATFORM_DECK`). No art assets exist, so the height field comes from the
albedo atlases.

## Method

- Height = luminance of the texel. Neighbours are fetched at integer texel offsets (`floor(uv * size) + o`, centre
  `+ 0.5`), so nearest filtering stays exact and the pixel art stays crisp. 3x3 Sobel gives the gradient per texel, soft
  clamped (`g / (1 + 2|g|)`) so a seam between two pieces of art cannot draw a hard line.
- Texture sizes are uniforms (GLSL 1.20 has no `textureSize`): ground atlas 512x256 with 16 px slots (neighbours are
  clamped inside the slot, so no bleed from the next tile), tree atlas 128x128, sprite atlas 512x256, building pages
  `VoxelBuildings_PageSize` (width x texture height). Trees, buildings and sprites have no slot: a clear (alpha 0)
  neighbour takes the centre height, so cutout edges make no false relief.
- Surface frame without attributes: normal from `cross(dFdx(eye), dFdy(eye))` facing the viewer, tangent and bitangent
  from the uv derivatives (cotangent frame). The tilt is `N - 3 (gx T + gy B)`. Cards (trees, grass, characters) are
  flat quads facing the camera, so the same code gives "the card normal tilted by the gradient".
- Light: world sun direction toward the sun = `(-dx, 1, -dz)` normalised (the shadow rays go the other way, 45 degrees
  up), from `gVoxelSun` each frame (`DeckBump_SetSun`). Weight `0.35 + 0.65 * strength`, so dusk and night keep a third.
  Plus a cavity term (texel minus the mean of its 8 neighbours) that does not depend on the light. Where the baked vertex
  colour is dark (cast shadow) the directional part fades to 0.4: no double darkening.
- Output is a multiplier around 1.0 on the colour the program already computes: `1 + amp * clamp(raw, -1, 1)`,
  amp LOW 0.10, MEDIUM 0.15, HIGH 0.20. OFF = amp 0: the function returns 1.0 before any texture tap.
- Rain: the ground program multiplies the relief by `1 - 0.4 wet`; the wet sheen math is untouched (uses the geometric
  normal), so the two compose and nothing is counted twice.

## Surfaces (one commit each, then the settings row and notes)

1. Ground: `deck_rain.c` wet program and `deck_particles.c` glint program get the block (shared GLSL in
   `deck_bump.c`). Dry ground with neither wet nor glints uses the wet program at wetness 0 (`DeckRain_BumpBegin`).
   One ground pass, no second pass.
2. Buildings: a plain textured program (`DeckBump_PlainBegin`), per page, in `Pass_Buildings`; the page alpha test stays
   the fixed-function one.
3. Trees and grass: the sway program (`deck_sway.c`); with sway OFF and bump on it runs with amplitude 0 and no bend.
4. Character cards: the outline program (`deck_chars.c`); with outline OFF and bump on it runs with outline alpha 0
   (clear texels discarded as before).
5. Settings: `voxel_bump`, row RELIEF (BUMP), OFF/AUTO/LOW/MEDIUM/HIGH, default AUTO (the 3-edit recipe).

## Fallbacks

Every program is built with the block first and without it if that fails (`DeckBump_Build`). `voxel_shaders=0` or bump
OFF: the old programs and paths run (buildings fixed function, trees fixed function when sway is also off). 2D overlay
and UI are not touched.

## Parallax: not done

A sub-texel view offset moves texel boundaries by up to half a texel, differently per texel (the height differs), which
tears the nearest-neighbour edges and moves alpha-tested edges. It cannot be judged without tuning rounds and
pixel-level crops, so it is skipped. The "extrusion" is the lighting and cavity relief only.

## Verification

Build on the Deck, `DECK_GL_DEBUG=1`, `still.txt` 75 s; A/B with `SETKEY voxel_bump=0 / 2 / 4` at noon, night
(`DECK_FORCE_TIME`) and rain (`DECK_FORCE_WEATHER=rain`); `shot_stats.py` OFF against the pre-phase build picture; pitch
24 and 40 runs for stability. Perf compare deferred (the ground program now takes 8 extra taps a pixel).
