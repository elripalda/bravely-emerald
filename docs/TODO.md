# To do

Things we know about and have not done yet.

## Fullscreen is hard to find

Some players cannot easily make the game fullscreen. The Windows build starts in a resizable
1280x720 window, and the only fullscreen switch we know of is the environment variable
`DECK_FULLSCREEN=1` (read in `src/platform/deck/deck_video.c`). There is no option for it in the
in-game settings, and we have not checked whether a hotkey works.

Ideas, not decided: a "fullscreen" row in the settings menu, F11 or Alt+Enter as a toggle, and a
`fullscreen` key in `userdata/settings.txt` so the choice is remembered. When this is done, mention
it in the builder's Help page too.
