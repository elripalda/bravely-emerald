# Deck visual phase 3: procedural ambient audio bed (H3, procedural layers first) plan

Spec: `docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md` section 3.h and the phase 3 row of section 5.
Scope: Deck and PC only. Everything new lives in `src/platform/deck/` (excluded from non-Deck builds) or behind `PLATFORM_DECK`.
No audio files, no game audio: every sound is synthesised at run time (spec Decision "fully procedural").

## Where it hooks (read, not guessed)

`Platform_QueueAudio` (`src/platform/sdl2.c:1058`) is called once per game frame from `music_player.c:784` on the game
thread with interleaved stereo float (`floatCount` = bytes / 4, channel = i & 1). The mixer adds into `sAdjustedAudio`
after the master volume loop and before the DC blocker. Sample count never changes, so the VBlank PLL is untouched.
It sits after the "queue too full, drop the newest frame" early return, so a dropped frame mixes nothing. In fast
forward (`sFastForward || speedUp`) the mixer is muted (state reset to silence, no DSP) and fades back in over 1 s.

## Design

Two files, so the DSP can be tested on its own:

- `deck_ambient.c/.h` (pure C, no game headers): `DeckAmbient_Init(rate, seed)`, `DeckAmbient_SetTargets(const DeckAmbientTargets*)`,
  `DeckAmbient_Mix(float *stereo, int frames, float gain)`, `DeckAmbient_Mute()`. Targets (0..1): wind, birds, crickets,
  water, rain, cave, plus `thunder` (a one-shot request, delayed 0.3 to 1.5 s). Each layer has its own slow slew (about 2.5 s,
  rain 1.5 s), the master has a 1 s fade. Layers: wind (two-pole low-passed noise with a gust LFO moving the cutoff and level),
  rain (high-passed noise bed plus Poisson droplet ticks), waves (low-passed noise under a swell envelope, 6 to 10 s, per-channel
  phase offset), crickets (4 voices of 3-5 pulse chirp trains on a 3.6 to 5.2 kHz carrier, panned), birds (up to 3 voices, sparse
  random calls of 1 to 4 swept sine notes with a smooth envelope, panned), cave (low noise plus beating 52/53.3 Hz drone,
  random drips with a pinged echo), thunder (low-passed noise burst; envelope tau 1.2 s, so about a 7.5 s audible tail, kept on purpose: a long rolling rumble; a bolt during a rumble queues one more after it). Sum goes through a soft limiter
  `x / (1 + |x|)` and is scaled so the loudest combination peaks near -22 dBFS before the volume.
- `deck_ambient_game.c`: `DeckAmbient_FromGame(float *stereo, int frames, float masterVolume, bool mute)` called from `sdl2.c`.
  Reads game state on the game thread: `VitaGame_IsOverworld()`, `gMapHeader.mapType`, `VitaDayNight_Get()->daylight`,
  `VoxelWorld_Weather/WeatherAmount/Lightning`, the player's neighbourhood for water (a 9x9 metatile scan every 20 frames,
  `MetatileBehavior_IsSurfableWaterOrUnderwater`). Context table:

| Context | Layers |
|---|---|
| Overworld, town/city/route, day | birds (by daylight), light wind |
| Dusk (daylight 0.25 to 0.6) | birds fading out, crickets fading in |
| Night outdoors | crickets, low wind |
| Rain, thunderstorm | rain by weather amount, wind up, thunder on the bolt edge |
| Sandstorm | wind strong |
| Ocean route, or water within 4 tiles | waves |
| Underground | cave tone and drips |
| Indoors, battles, menus, `CB2` other than overworld | off (all targets 0, master fades in 1 s) |

Setting: `ambient_volume`, 0 to 10, default 6, 0 = hard off (no DSP, no state change), `FAST_EXTRA_AMBIENT_VOLUME`,
row "AMBIENT VOLUME" in the GENERAL tab, group SOUND, clamped at use (settings.txt is not range-checked on load).
Final gain = `ambient_volume/10 * master volume`.

## Tasks

1. `deck_ambient.c/.h` and `tools/deck_tests/test_ambient.c` (host-style test run on the Deck in the distrobox, `gcc -m32`):
   renders 60 s per context, checks no NaN/Inf, peak below a bound, RMS in a band, no clip, loop seam continuity (max sample step),
   silence after Mute, off switch (gain 0 gives exact zeros), thunder trigger. Commit.
2. Setting key, menu row, `deck_ambient_game.c`, hook in `sdl2.c` (log context changes and a 5 s line with queue size, peak, RMS). Commit.
3. Build on the Deck (-j4), run the test, one `still.txt` autoplay (75 s) with `DECK_GL_DEBUG=1`: 0 GLERR, no crash, ambient log lines, queue level in the log.
4. HANDOFF decisions and status, DECK_NOTES section, ledger, commit and push to the Deck remote.

## Not in this phase

Bird/owl recordings (H1 files): none chosen, procedural birds only. Real listening tune: the user's pass. Underwater map type is
treated as off (the water filter is a separate effect).
