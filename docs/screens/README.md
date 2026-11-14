# Screens from the Deck (shots via DECK_SHOT, read by eye)

All taken on the Steam Deck, branch deck-port, DECK_GL_DEBUG=1, 0 GLERR.

- sunshadow_noon_high_first_run_stripes_on_far_walls.png: first sun shadow run (noon, voxel_sunshadow HIGH). Horizontal stripes on the far houses = the bug fixed afterwards (normal now forced to face the viewer). Tree shadows on the ground are not clearly visible.
- final_A_noon_allhigh_t1_beams.png and final_A_noon_allhigh_t2_beams_moved_uiglow_sign.png: final run A, noon, every effect HIGH incl. the 4 new keys, 2 s apart. Faint light columns at the top (x about 280 in t1, about 820 in t2) with sparkles: the beams moved. t2 shows the LITTLEROOT TOWN sign with its dark halo (UI depth glow). Roof grid and ground speckle are crisper (stronger relief).
- final_A_noon_allhigh_t0_unusable_black.png: 15 s shot, mostly black (fade at start); ignore.
- final_B_battle_allhigh_*.png: final run B, wild battle, all HIGH. Backdrop clearly blurred and darkened, battlers, HP boxes and text box sharp, faint halo around the boxes. intro = first message, attack = TORCHIC used SCRATCH.

Not captured (single test rule): OFF vs HIGH pairs for sun shadows, relief crops (player, tree, house at voxel_bump 0 vs 4), ground detail at dusk, dusk and night beams, menus with the new rows.

## deck-title branch (Deck, DECK_GL_DEBUG=1, 0 GLERR)
- title_original.png: the original title (voxel_title=0, voxel_flatfx=0), phase 3.
- title_bravely_2dhd_phase3.png and title_bravely_2dhd_phase1_shine.png: the new title with BRAVELY and 2D-HD lettering; the phase 1 shot shows the shine sweep on the logo.
- flatfx_intro_leaves_no_margins_slivers.png (slivers at both edges, no intro_margins.bin) and flatfx_intro_leaves_with_margins_off.png (margins art present, flatfx off).
- flatfx_mainmenu_first_bloom_washout.png (first bloom values, NEW GAME unreadable), flatfx_mainmenu_high_retuned.png (after the retune), flatfx_title_original_high.png (original title through the flat pass at HIGH).
