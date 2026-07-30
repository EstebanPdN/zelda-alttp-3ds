# Link to the Past 3DS v2.3 Experimental

This is a pre-release focused on Old 3DS stability and performance testing.

## What's new

- Added Old 3DS optimizations that keep the game logic running at the intended 60 Hz while using a lighter Old 3DS display profile.
- Restored the safe parallel PPU renderer path after experimental Core 1 settings in E6 were rejected on some Old 3DS systems.
- Added immediate bottom-screen redraws for critical state changes such as health, map/area, dungeon/interior state, equipped item, magic, keys, bombs, arrows, and rupees.
- Improved HOME menu and app close handling:
  - HOME and sleep are now explicitly allowed.
  - APT suspend/restore/sleep/wakeup/exit events are handled.
  - Active GPU frames are closed before yielding to HOME/suspend.
- Improved first-run/setup error handling:
  - Fatal setup or asset-loading errors should now show a readable 3DS error screen instead of appearing as a silent black screen.
  - Errors are also written to `setup-error.txt` and/or `runtime.log` in `sdmc:/3ds/Zelda 3DS/`.

## Recommended Old 3DS settings

Use Original display mode on Old 3DS for best performance. Wide mode is much heavier and is still recommended mainly for New 3DS.

## Known testing focus

Please test:

- Opening the HOME menu from the game.
- Closing the app from HOME without needing to power off the console.
- Fresh install / first-run extraction flow.
- Whether black screen reports now become readable error screens with logs.
- Old 3DS FPS and dumps, especially:
  - `Parallel PPU renderer`
  - `Core 1 PPU budget`
  - `Average PPU draw`
  - `Measured presentation rate`

This is intentionally marked as a pre-release while Old 3DS performance and first-run reports are still being validated.
