# Screens from the Deck (shots via DECK_SHOT, read by eye)

All taken on the Steam Deck, branch deck-port, DECK_GL_DEBUG=1, 0 GLERR.

- sunshadow_noon_high_first_run_stripes_on_far_walls.png: first sun shadow run (noon, voxel_sunshadow HIGH). Horizontal stripes on the far houses = the bug fixed afterwards (normal now forced to face the viewer). Tree shadows on the ground are not clearly visible.
- final_A_noon_allhigh_t1_beams.png and final_A_noon_allhigh_t2_beams_moved_uiglow_sign.png: final run A, noon, every effect HIGH incl. the 4 new keys, 2 s apart. Faint light columns at the top (x about 280 in t1, about 820 in t2) with sparkles: the beams moved. t2 shows the LITTLEROOT TOWN sign with its dark halo (UI depth glow). Roof grid and ground speckle are crisper (stronger relief).
- final_A_noon_allhigh_t0_unusable_black.png: 15 s shot, mostly black (fade at start); ignore.
- final_B_battle_allhigh_*.png: final run B, wild battle, all HIGH. Backdrop clearly blurred and darkened, battlers, HP boxes and text box sharp, faint halo around the boxes. intro = first message, attack = TORCHIC used SCRATCH.

Not captured (single test rule): OFF vs HIGH pairs for sun shadows, relief crops (player, tree, house at voxel_bump 0 vs 4), ground detail at dusk, dusk and night beams, menus with the new rows.
- rays_dawn_from_right_gain5.png, rays_noon_vertical_gain5.png, rays_dusk_from_left_gain5.png, rays_night_moon_gain5.png: DECK_FORCE_TIME dawn, noon, dusk, night with DECK_SHAFT_GAIN=5 (only to make the angle readable): beams lean from the right at dawn, vertical at noon, from the left at dusk, pale blue near vertical at night. rays_dusk_final_gain.png: dusk at the final gain (0.325 at HIGH), soft.
- ground_*_before_after_3x.png: ground texture crops enlarged 3x, before (left) and after (right) the finer ground relief; ground_indoor_lab_before_stripes.png / ground_indoor_lab_after_fine_grain.png: the lab floor, ripple stripes then fine grain.
- battlefx_high_crit_motes.png: wild battle with voxel_battlefx HIGH after a critical hit: blurred backdrop with gold motes, UI crisp.
