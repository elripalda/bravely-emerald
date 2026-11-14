# Deck phase 8: real-time sun shadow map (voxel_sunshadow)

Date: 2026-11-14. Setting `voxel_sunshadow`, label SUN SHADOWS, levels OFF / AUTO / LOW / MEDIUM / HIGH. Deck only (`PLATFORM_DECK`, GLSL 1.20). OFF is the picture of today. This reverses the earlier decision to skip phase 8 (HANDOFF run summary): the user asked for it explicitly.

## Design

- Sun direction: the one the baked rays use. A ray climbs one tile for every tile along (-dx, -dz) (`RayPoint` in `voxel_lighting.c`), so the unit vector toward the sun is normalize(-dx, 1, -dz), `gVoxelSun` from `VitaDayNight_GetSun`.
- Casters (new pass `Pass_SunShadowMap`, depth only, fixed pipeline, no shader): chunk tree ranges (crown cut-outs with the alpha test; tall grass tufts share the range, so they cast a little too), building spans per page (alpha test on pages that need it), the character cards (`VoxelEntities_CharacterRange`). No sway offset in the map (crowns do not move there). Ground and cliffs are not casters (they only receive).
- Map: orthographic box of 64 x 64 tiles around the camera target, eye 48 tiles along the sun direction, depth range 96, centre snapped to whole texels along the light axes (no shimmer while walking). LOW 1024, MEDIUM and HIGH 2048.
- Receivers: one screen space resolve inside the composite (`deck_post.c`, `POST_SUN`). It rebuilds the world position of each pixel from the scene depth texture (inverse of P * MV captured after the camera is set), normal from the derivatives of that position, slope scaled bias and normal offset, PCF (LOW 2x2, MEDIUM 3x3, HIGH 5x5 at 1.25 texel spacing), fade over the last 8 percent at the map edge, and multiplies the colour by the ambient over 0.9 (`SS_FACE_REF`) inside shadow. Ground, trees, buildings and characters all receive through this one shader (instead of the shared GLSL block in four programs and the fixed pipeline paths: far less risk, the same result). Faces turned from the sun (n.L <= 0.05) are skipped: the baked face term already darkens them.
- Fade: the map is not drawn below sun strength 0.04 (night), and the multiplier follows the sun strength (dawn and dusk shadows are weaker). Not drawn in 3D battles, indoors or underground.
- Double darkening: while the setting is >= LOW and the post chain can resolve the map (`DeckSunShadow_BakedOff`), `VoxelLighting_SetNoCast(true)`: `Lit()` returns true at once, so the chunk builder skips every baked cast shadow ray (contact AO and the sun angle face shading stay). The flag is in `VoxelLighting_Hash` and the change bumps `sWorldEpoch` (worker idle first, like `VoxelLighting_SetSun`), so the chunks rebuild when the setting changes.
- Relation to the older shadows: the character cast silhouettes (`EmitCastShadow`, flat picture laid on the ground) are skipped while the map of the last frame was drawn (the characters cast through the map). The soft blob under the feet (`voxel_shadow`) stays: it is contact shading, the map gives a directional shadow. Decal shadows (surf) stay.
- Fallbacks (`[SUNSHADOW]` and `[POST]` lines in `gl_errors.log`): composite with `POST_SUN` fails to build -> POST_X alone (no sun shadows, baked rays back); target not complete -> off; env `DECK_SUNSHADOW_OFF=1` forces off for A/B shots.

## Steps

1. `deck_sunshadow.c/.h` (target, matrices, texel snap, level, resolve data), `DeckGl_SetMat4`.
2. `deck_post.c`: `POST_SUN` composite block, uniforms, texture units 3 (scene depth, now also for the sun) and 6 (map), `DeckPost_SunReady`, `DeckPost_WantScene` also true for the sun alone.
3. Setting row (3 edits), lighting flag and hash, entity silhouette gate, renderer hooks.
4. A/B at noon and dusk (OFF / LOW / HIGH), chunk build ms per chunk, frame cost, 0 GLERR.

## Not covered

Cliff and ground relief as casters, sway in the map, shadows of tall grass tufts are not suppressed, no cascade (one box of 64 tiles).
