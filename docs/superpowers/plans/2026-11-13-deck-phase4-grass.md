# Deck visual phase 4: standing tall grass (E1), player bend (D3), flowers (E2) plan

Spec: `docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md` section 3.e (E1/E2), 3.d (D3), phase 4 row of section 5.
Scope: Deck and PC only. Code under `PLATFORM_DECK` or in `src/platform/deck/`. Grass first, flowers a separate commit.

## Design (grass, E1 + D3)

- Classification by behaviour, not metatile id: `VoxelWorld_IsTallGrassCell` (`MetatileBehavior_IsTallGrass`), skipping cells that
  are already a tree part (`VoxelTree_Part >= 0`, which includes long grass 0x015 and its Fortree variants). The ground pass is
  untouched, so the flat tall grass tile (with its art) stays under the tuft. Metatile behaviour, encounters, `VoxelTree_Part`
  (read by lighting, entities, battle) stay exactly as they are.
- Art: the long grass tuft, 16x32 texels at (80,32) of the tree atlas, one leant card per cell, 0.55 of the long grass's height
  (the GBA's grass is knee high). No tint per tileset (the tuft is green; ash grass has its own behaviour and is not tall grass).
- Emitted in the tree phase of `ComputeChunkJob` by `VoxelTree_EmitInstance` (`EmitTallGrass`): same worker, same single scratch
  (`sScratch`, `sSwayW`); no new builder, no new thread.
- Lighting: one `VoxelLighting_Sample` at the foot, `lightingConstant` for the card (as signs do), not rays per corner.
- Sway: weight bytes of grass (long and tall) are stored even with bit 0 set (`builder->swayBend`); packed alpha = 255 - byte
  (64..255), alpha test GREATER 0 unaffected. Shader: `bend = raw & 1`, `w = (raw - bend) / 190`. Trees keep bit 0 clear.
- Bend (D3): `VoxelEntities_GetWalkers` returns the player and the nearest visible objects within 4 tiles (follower, NPCs), up to
  4, in the current frame. `DeckSway_SetWalkers` -> uniforms `uBend0..3`. Vertex shader: per walker, falloff `(1 - d/0.9)^2 * w`,
  push away 0.6 tile and sink 0.3. Chunk draw offset goes in as `uFrameOffset` so the vertex frame equals the walker frame after
  map crossings. No CPU history trail: the falloff over a walker's step is the recovery (cost: grass snaps up with distance, no
  spring overshoot).
- GBA grass field effect (`FLDEFFOBJ_TALL_GRASS`) is skipped where a tuft stands (`VoxelTree_TallGrassStands`), as long grass
  already does; the own-cell tuft stands in front of the walker.
- Cap: `CHUNK_TREE_CAP` 768 to 3072. Worst case (every cell tuft, 6 vertices each, plus a wood) is far below; sizes follow
  (`CHUNK_VERTS_PER_SLOT`: staging 21,504 x 24 bytes = 516 KB, one malloc; VBOs are sized to the actual count, so no per-slot
  growth). `treeFirstVert`/`totalVertCount` are `uint16_t`: max 6,144 + 3,072 + 12,288 = 21,504 < 65,535.
- Setting `voxel_grass` (0/1, default 1, EFFECTS "STANDING GRASS"). Off when `voxel_shaders` is 0 or the sway program failed
  (`DeckGrass_Enabled`): the old flat look. The flag is XORed into the chunk content hash and a change bumps `sWorldEpoch`, so
  chunks rebuild on toggle.

## Tasks (grass)

1. Builder bend bit, `EmitTallGrass`, `VoxelTree_TallGrassStands`, cap raise, hash/epoch. 2. Walkers + shader bend.
3. Setting and menu row. 4. Build on the Deck; run `still.txt` and a grass walk with `DECK_GL_DEBUG=1`: 0 GLERR; `[GRASS]`
log lines (tufts, tree vertices, built vs cap, treesMs per chunk); read screenshots. 5. Docs, commit, push.

## Flowers (E2), decided after reading the code

See the flowers section appended below when written.

## Not in this phase

Amplitude/bend tuning by eye, perf compare (deferred by ruling), per-tileset tuft tint.
