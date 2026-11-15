# Known issues

## Steam Deck and Windows

- `Fatal error while decompressing LZ file.` is printed once per wild encounter. The game continues.
- At camera tilt 24 with TILT-SHIFT on HIGH, the front row of trees is very blurred and covers the
  player's legs.
- Water tiles get the wet tint in rain.
- Mauville and Ever Grande flowers are not covered by the flower effect.
- The intro and title margins at 16:9 need `assets/ui/intro_margins.bin`, which `build_deck.sh` makes.
- The Linux and SteamOS builder package has not been run on real hardware yet.

## Not measured or not checked

- Frame timing with the effects on, real vsync and the 30 FPS mode.
- Changing `voxel_res` while playing.
- Dawn, water glints on real water, caves and indoors, crossing a map border with the effects on.
- Audio, by ear.
- Windows: overworld and battle visuals, controller and performance. Windows has been tried on one
  GPU only (AMD).

Frame timing without the new effects is in [PERFORMANCE.md](PERFORMANCE.md).

## PS Vita

The Vita issues are listed in [VITA.md](VITA.md) and apply to the Vita build only.

Please [open an issue](https://github.com/marcoscodas/bravely-emerald/issues/new/choose) if you find
something that is not listed.
