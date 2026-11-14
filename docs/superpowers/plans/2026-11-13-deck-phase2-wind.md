# Deck visual phase 2: wind sway (D1) plan

Spec: `docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md` section 3.d (D1) and the phase 2 row of section 5.
Scope: Deck and PC only (the user dropped Vita). All Deck code stays under `PLATFORM_DECK` or in `src/platform/deck/`.

## Design

- Weight per vertex, set at build time. `VoxelVertex` cannot grow (building models are `fread` into it,
  `voxel_building.c:517`), so the builder keeps a parallel byte array: `VoxelBuilder.swayW` (NULL = off),
  `swayY0`, `swayRise`, `swayScale`. `VoxelBuilder_Tri` writes `round(190 * scale * clamp((y - y0) / rise))`
  from the input vertex height (before lift/base/stretch), so lighting subdivision cannot break it.
- The tree emitters (`EmitCrownCard`, `EmitSmallCell` in `voxel_tree.c`) set y0 = crown foot (`baseHeight`),
  rise = full crown height. Large crowns are two stacked row cards: the weight is a function of the height
  over the crown foot, so the shared edge of the two rows has the same weight (no tear). Scale 0.5 for
  trees (top weight 0.5), 1.0 for long grass. Ground quads (`EmitGround`) are emitted outside the setting: weight 0,
  so crowns meet their trunks.
- Packing: tree-phase `PackVoxelVertices` writes alpha = 255 - weightByte (range 65..255). 255 = still.
  The legacy fixed-function tree pass uses alpha test GREATER 0 with blending off, so the packed alpha is harmless
  when the shader is off or failed.
- Shader `deck_sway.c` (GLSL 1.20, built with the phase 1 loader): decodes the weight, wave phase from the
  WORLD position `gl_Vertex.xz + uChunkOffset` (so shared edges of neighbouring cards and chunks move
  identically), two sines plus a slow gust, wind from `uWind` (dirX, dirZ, strength; fed per frame by
  `DeckGl_SetFrameUniforms`: 0.25 clear, 0.6 to 1.0 rain, 0.8 to 1.0 sand). Strength is damped to
  `0.25 + (s - 0.25) * 0.5`. Fragment: texture x vertex colour, `discard` at alpha 0 (same as the alpha test).
- Chunk offset: chunks are drawn with a draw offset after map crossings, and the frame itself is re-based on
  each crossing. `uChunkOffset = drawOffset - total shifts` is constant in world terms, so no phase jump at crossings.
- Switch: key `voxel_sway` (0 off, 1 on, default 1), `FAST_EXTRA_VOXEL_SWAY`, row in the EFFECTS group,
  same pattern as `voxel_bloom`. Also off when `voxel_shaders` is 0. Fallback: program build failure leaves
  `DeckSway_Begin()` false and the old pass runs.

## Tasks

1. Builder fields, Tri weight write, tree emitters, renderer packing (scratch array `sSwayW`).
2. `deck_sway.c/.h`, hooks in `Pass_Trees` (begin, per-chunk offset, end), init/shutdown, shift bookkeeping.
3. Setting key and menu row.
4. Build on the Deck; run `field.txt` autoplay with `DECK_GL_DEBUG=1`; require 0 GLERR; read two screenshots a few
   seconds apart (crowns and grass shifted, no gaps at trunks or chunk seams).
5. Record decisions in `docs/HANDOFF.md`, `docs/DECK_NOTES.md` note; commit, push to the Deck remote.

## Not in this phase

Player bend (D3), standing tall grass (E1), flowers: phase 4. Amplitude tuning by eye: user's visual pass.
