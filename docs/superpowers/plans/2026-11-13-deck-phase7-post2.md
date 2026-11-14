# Phase 7 plan: tilt-shift (G2), scene resolution (G5), light shafts (B3), LUT grade (B2)

Spec: docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md. All four live in `src/platform/deck/deck_post.c` (Deck only). One commit per feature, in this order: tiltshift, res, shafts, lut. Each has a leveled row (OFF/AUTO/LOW/MEDIUM/HIGH, default AUTO) added by the 3-edit recipe in DECK_NOTES "Adding a setting": keys `voxel_tiltshift`, `voxel_res`, `voxel_shafts`, `voxel_lut`.

## Shared structure
- The composite shader source is one body with `#ifdef POST_X` blocks. `BuildPrograms` builds it twice if needed: first with `POST_X` (all new features), and if that fails to compile, without it (the phase 1 composite: bloom, blur bands, grade, vignette). Uniforms that the plain variant lacks return -1 and the setters ignore them. So a broken new shader costs the new features only, never the whole chain.
- `DECK_POST_FORCE_FAIL` still forces the whole legacy path. A new env `DECK_POST_NO_X` forces the plain composite (fallback test).
- Texture units in the composite: 0 scene, 1 bloom, 2 blur / tilt copy, 3 scene depth (texture24 only), 4 LUT strip, 5 shafts.
- `DeckPostFrame` gets: `zNear zFar focusDist` (camera), `sunScreenX`, `sunAmount` (0..1, already gated by time of day, weather and indoor by the renderer).
- `DeckPost_WantScene` is true when any of grade, bloom, blur (outdoors), tilt-shift (outdoors), shafts or LUT is on.

## G2 tilt-shift
- CoC from the linearised scene depth: `z = 2nf / (f + n - (2d - 1)(f - n))` (view-space depth, near 1, far 62, the values of `Voxel_gluPerspective`). Focus = eye-to-target distance (`sqrt` of target minus eye, from `sCamera`), so the plane follows the camera and the pitch (24..40): the depth itself carries the tilt, no screen rows are tuned.
- `coc = smoothstep(0.06, 0.40, rel)` with `rel = |z - zf| / zf`, near side scaled 1.4 (the near side of the picture has the smaller relative depth range). Dead zone 0.06 keeps the player and neighbours sharp.
- Bokeh gather: up to 16 golden-angle taps on the half-res copy of the scene (`sHalf[2]`, bilinear), radius `coc * 0.014 * level scale` of the picture height, each tap weighted `1 + lum^2 * 3` (highlights bloom into discs). Result `mix(sharp, gathered, coc)`.
- Level: radius x0.6 / 1 / 1.4 and 8 / 12 / 16 taps (LOW / MEDIUM / HIGH).
- Active when `voxel_tiltshift` on, outdoors, the scene target has a depth texture. Then it replaces the band blur (`voxel_blur` look). Else the old band blur runs as before. Indoors both are off (legacy rule).

## G5 scene resolution
- The scene target (and the four half-res targets) is `W x H` with W 960 / 1120 / 1280 and H = W x 544 / 960 rounded up to even (544, 636, 726; the Deck letterbox is 1280 x 725 at native). OFF and LOW = 960, MEDIUM = 1120, HIGH = 1280; AUTO follows `voxel_quality`.
- `DeckPost_WantScene` compares the wanted size with the target and recreates it (`DeckPost_Resize`: delete, create, log). If the create fails it retries 960x544, and if that fails too the chain goes to the legacy path. No size mismatch fallback any more.
- The 3D viewport uses the scene target size; the projection aspect stays 960/544, so the framing and every screen mapping of the logical 960x544 (UI mirrors, name tags) are the same. The composite maps the whole target to the letterbox rect with GL_NEAREST at 960, and LINEAR above 960 (the target is not an integer multiple of the letterbox, NEAREST would shimmer). UI and 2D stay at 960x544; `deck_shot.c` reads the window, not the target.

## B3 light shafts
- Half-res pass (`sHalf[3]`): 24 taps radial blur from each pixel toward a point above the picture, luminance above 0.55 as the source, decay 0.94, exposure by level (0.5 / 1 / 1.4 of a base).
- The real sun direction lies above and beside the frustum at these pitches (the top ray is 14.5 degrees below the horizon at 32), so the projection is unstable. The focus point is fixed: x = 0.5 - dx x 0.25 (sun east = screen right, `VitaDayNight_GetSun`), y = 1.3 (above the top edge). Shafts are vertical-ish rays that lean with the time of day.
- Amount: sun strength x daylight, plus a dusk and dawn share from `glow`; 0 at night, indoors, in 3D battles (the post chain is field only), and in rain, fog, sand, ash. Added in the composite before the grade, warm tint.

## B2 LUT grade
- A 256 x 16 RGB8 strip (16 x 16 x 16) built on the CPU: each texel runs the procedural grade (tint by luminance, saturation) at the preset strength plus a gentle S-curve (contrast per time of day, stronger at dusk). Rebuilt with `glTexSubImage2D` when daylight, glow or level change by more than 0.005 (4096 texels, microseconds). The fade (`keep`) stays in the shader: `mix(c, lut(c), keep)`.
- Trilinear in the shader from two bilinear taps on the strip (half-texel centres). Vignette stays analytic.
- `voxel_lut` off: the old procedural grade path, byte for byte as before.

## Verification (Deck, one run at a time)
`DECK_GL_DEBUG=1`, `still.txt` at 75 s, shots in `shots/p7/<name>` (not tracked): tiltshift on vs off at pitch 32 and 24; res 960 / 1120 / 1280 (target size in the `[POST]` log, shot size); shafts at noon and dusk vs off, rain off; LUT on vs off; `DECK_POST_NO_X` and `DECK_POST_FORCE_FAIL`. 0 GLERR every run. Not done by ruling: visual tuning rounds, perf compare.
