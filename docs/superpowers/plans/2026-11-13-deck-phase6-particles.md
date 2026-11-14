# Phase 6 plan: particles (F2), water glints, outline (G4), soft shadows (B4)

Spec: docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md (section 3.f, 3.g, B4, phase 6 row). Deck and PC only; all new
code in src/platform/deck, shared files only behind PLATFORM_DECK. Pattern: phase 5 (deck_rain.c): static quad VBO, the vertex shader
does the motion, one draw per kind, fallbacks to the old look when a program fails or the setting is off.

## Settings (vita_extras.h/.c, EFFECTS tab, default 1, off = old look)
- voxel_particles: pollen, fireflies, leaves and water glints. Env DECK_PART_OFF=pollen,fire,leaf,glint for single A/B shots.
- voxel_outline: character outline and sun rim.
- voxel_shadow: soft blob shadow under characters.
All three are also off when voxel_shaders is 0 or their program failed.

## Debug env
- DECK_FORCE_TIME=dawn|noon|dusk|night (Deck only, read once, in vita_daynight.c): VitaDayNight_Update overrides the light and sun
  after its normal computation, outdoors only. Every scene consumer reads VitaDayNight_Get/GetSun, so the grade, motes, glow and
  the sun for shadows follow. Documented in docs/DECK_NOTES.md.

## Task 1 particles (deck_particles.c/.h)
- One program, one VBO of 3 regions (pollen 192, fireflies 64, leaves 32 quads); seed in vertex xyz, corner in texcoord; uKind picks
  the motion. Box = the mote box (22 x 18 around the camera target). Wrapped by fract((t*vel - target)/span), so nothing is stored.
- Pollen: alpha blend, density by light.motes; replaces the legacy 32 motes when drawn. Fireflies: additive, blink, glow > 0.3, on
  route and town maps. Leaves: alpha blend, tumbling, falling in the wind, count by the tree vertex count of the visible chunks (no
  per-chunk tree list, see decisions).
- Culled: indoors, underground, battles, rain (weather RAIN) and fog/sand weather for leaves and pollen.
- Log line `[PART]` every 5 s under DECK_GL_DEBUG: counts drawn per kind.
- Call site: Pass_Atmosphere3D, after the fog banks where DrawMotes was.

## Task 2 water glints (same file)
- Ground program used in Pass_Ground when the wet program is not: texture * colour as the fixed path, plus sparkles on blue (water)
  texels from a cell hash of the chunk-offset world position, strength from the sun and the glow. No builder change.

## Task 3 outline and rim (deck_chars.c/.h)
- Billboard fragment shader over the character range of the entity vertices only (VoxelEntities_CharacterRange): neighbours at
  +-1 texel give a dark outline on transparent texels, a warm rim on texels with transparent texel toward the sun. Effects and doors
  keep the fixed path; the occluded-player silhouette pass is untouched; UI is drawn after, never touched.

## Task 4 soft shadows (deck_chars.c)
- Blob list from voxel_entities (VoxelEntities_GetBlobs, filled where the cast shadow is emitted, outdoor objects only), drawn as
  ground quads with a radial gradient before the silhouette shadows. Silhouette alpha x0.62 while on, blob peak 0.20, overlap about
  0.38 vs 0.36 before: no double darkening.

## Verify
Build -j4, runs `still.txt` with DECK_GL_DEBUG=1 and DECK_FORCE_TIME noon/night (and DECK_PART_OFF=all or setkey 0 for B pairs),
0 GLERR, read PNGs, perf compare deferred. Record results in docs/DECK_NOTES.md "Phase 6" and HANDOFF.
