# Documentation

Start with [ARCHITECTURE.md](ARCHITECTURE.md), then read the document for the area you are
changing. Contribution rules are in [../CONTRIBUTING.md](../CONTRIBUTING.md).

| Document | Covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layers, boot, one frame, where things are, runtime files |
| [THREADS.md](THREADS.md) | Cores, pacing, who owns which data, known races |
| [RENDERING.md](RENDERING.md) | The three presentation paths, voxel passes, GBA overlay, GPU rules |
| [SCENES_16_9.md](SCENES_16_9.md) | How GBA screens fill 960×544, and the planned Wide Scene system |
| [VOXEL_ENGINE.md](VOXEL_ENGINE.md) | The 3D overworld modules, chunks, atlas, map changes |
| [VITA_UI.md](VITA_UI.md) | The native Emerald 3DS-style interface and the features behind it |
| [BUILD.md](BUILD.md) | Build overview: shared host-tool and asset steps, CMake options |
| [BUILD_DECK.md](BUILD_DECK.md) | For users: build the Steam Deck version with one script from your own ROM |
| [WINDOWS_BUILD.md](WINDOWS_BUILD.md) | The 32-bit Windows build (MSYS2) |
| [BUILDER.md](BUILDER.md) | The desktop builder app: install, what it runs, security model, packaging |
| [RECOMPLAUNCHER.md](RECOMPLAUNCHER.md) | RecompLauncher support: recomplauncher.json, the `--prepare` build step, how to test it |
| [FEATURES.md](FEATURES.md) | Every feature of the Deck and Windows builds, and the full settings reference |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Known issues and what has not been checked |
| [PERFORMANCE.md](PERFORMANCE.md) | Measured frame timing |
| [VITA.md](VITA.md) | The PS Vita port, as the original project describes it |
| [CREDITS.md](CREDITS.md) | Who built what, fonts, AI assistance, legal notes |
| [BUILDER_HELP.md](BUILDER_HELP.md) | The text shown by the builder's help link |
| [DECK_NOTES.md](DECK_NOTES.md) | Deck and Windows effects: constants, settings levels, container and SDL2 notes |
| [HANDOFF.md](HANDOFF.md) | Working notes of the Deck and Windows port: state, decisions, open items |
| [ASSETS.md](ASSETS.md) | Where every data file comes from and how it is generated |
| [DEBUGGING.md](DEBUGGING.md) | Logs, per-frame telemetry, crash dumps |

Licences of the ported code are in [../NOTICE.md](../NOTICE.md).

The design mocks of the new UI are in [../design/stitch/](../design/stitch/README.md).
