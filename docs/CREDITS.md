# Credits

## Chain of work

- [pret/pokeemerald](https://github.com/pret/pokeemerald): the decompilation of Pokémon Emerald and its tools.
- [gradenGnostic/pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform):
  the SDL2 platform layer and voxel renderer this port starts from.
- [HctrRB/pokeemerald-vita-3ds](https://github.com/HctrRB/pokeemerald-vita-3ds): the PS Vita port,
  with the features of [Emerald 3DS](https://github.com/ZallaxDev/emerald-3ds).
- This repository: the Steam Deck and Windows builds, the visual upgrade and the builder app.

## People and projects

- pret and contributors: the pokeemerald decompilation and its tools.
- Zallax and the Emerald 3DS contributors: Emerald 3DS, the source of most of what the Vita port
  adds: the voxel world, the 3D battles, the followers, the extras and the look of the interface.
- gradenGnostic and contributors: pokeemerald-multiplatform.
- Rinnegatamante (vitaGL), the VitaSDK, SDL and Vita3K teams, Dave Gamble (cJSON) and Niels Lohmann
  (nlohmann/json).
- HctrRB: project lead of the Vita port, supervision and hardware testing.

## Fonts

- Cinzel, The Cinzel Project Authors, SIL Open Font License 1.1
  ([licence](licenses/Cinzel-OFL.txt)). The title lettering of the Deck and Windows builds.
- W95FA, Alina Sava, SIL Open Font License 1.1 ([licence](licenses/W95FA-OFL.txt)). An optional
  classic pixel font in the builder app.

## AI assistance

The Steam Deck and Windows work (platform code, effects, build scripts, the builder app and the
documentation) was written with an AI coding assistant (Claude Code, by Anthropic) under the
direction of marcoscodas. The Vita port was likewise written with an AI coding assistant under the
supervision of HctrRB.

## Third-party components and licences

The full table, with licences and where each component is used, is in [../NOTICE.md](../NOTICE.md).

## Legal

- Pokémon, Pokémon Emerald and all related names, characters, graphics, music and text are
  trademarks and copyrights of Nintendo, Creatures Inc. and GAME FREAK inc. This is a
  non-commercial fan project, not affiliated with or endorsed by them.
- This repository does not include the game's graphics, sounds or binary data, nor any ROM.
  `fetch_assets` checks a dump of the user's own cartridge and downloads those files from the
  public pret/pokeemerald decompilation (via pokeemerald-multiplatform); they remain the property
  of their owners. The decompiled code and text in this tree come from the same decompilation.
  Builds contain that material: do not upload, share or sell them. Use this project only with a
  legitimately owned copy of Pokémon Emerald.
- Code licences differ by origin: MIT for the multiplatform layer and the original Vita, Deck and
  Windows code ([LICENSE](../LICENSE)), CC BY-NC-SA 4.0 for everything derived from Emerald 3DS.
  The full list is in [../NOTICE.md](../NOTICE.md).
- Provided as is, without warranty. Rights holders can request changes or removal by opening an
  issue.
