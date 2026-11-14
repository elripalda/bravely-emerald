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
- banding_far_house_switch_comparison.png: far house wall 3x at default settings; rows: default, bump off, sun shadow off (lines gone), tilt-shift off, res 4 (gone), LUT/grade off. banding_far_house_fixed_default.png: the same scene after the normal fix (no lines). banding_far_house_before_after_tapfix_note.png: default before, default after the rotated gather only (lines unchanged), all HIGH (none).
- floor_indoor_cmp_8aa6f41_vs_head_vs_default.png: lab floor 3x: commit 8aa6f41 all HIGH, head all HIGH, head default.

## UI polish (deck-ui branch)
- ui_sign_off_default_high_2x.png: the map name popup (GBA overlay) cropped and enlarged 2x, OFF / default / HIGH stacked. Same size and the same crisp glyphs in all three (bbox x 101-526, y 42-190 at 1280, as in the pre-UI-work screen).
- ui_sign_high_full.png: the full frame with every UI setting and every effect at HIGH.
- ui_hub_off_default_high.png: the new UI START menu (ui_v2=1), OFF / default / HIGH. Original art; HIGH only adds a 1 px shadow under the white text on the red row.
- ui_battle_off_default_high.png: new UI battle HUD and text box (ui_v2=1), OFF / default / HIGH (different wild Pokemon, the encounter is random). Same HUD, original HP bar colours.
- ui_settings_high.png: port options menu (VitaUI canvas) at HIGH: side margins get a soft shadow; no footer text line exists, the group label is darker.
- ui_party_high_after_fix.png: new UI party info at HIGH after the text shadow threshold fix (muted grey labels are clean).
