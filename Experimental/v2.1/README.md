# Link to the Past 3DS v2.1 experimental

This experimental build is based on the published v2.0 source.

It keeps the installed application metadata from v2.0:

- SMDH title: Link to the Past 3DS v2.0
- CIA title: ALttP 3DS v2.0
- ProductCode: CTR-P-Z3DS
- UniqueId: 0x5A20D

That means installing this CIA should update the same app instead of creating a second HOME Menu icon.

Runtime profile:

- New 3DS: keeps the normal v2.0 rendering profile.
- Old 3DS / Old 2DS: automatically switches to an Old 3DS profile. It keeps game logic at 60 Hz and uses an adaptive top-screen render pacer measured against the 60 Hz frame budget. Original and Stretch start at 30 Hz visuals; Wide is selectable again and starts at 20 Hz visuals because it asks the software PPU to draw a larger 400px-wide scene. If a mode repeatedly misses budget, the pacer temporarily lowers only the visual refresh instead of slowing game logic; if there is sustained headroom, it climbs back toward that mode's target.
- The bottom screen can now present independently during Old 3DS top-frame skips, so touch menus and map/status redraws are not forced to wait for the next top-screen render.

Version popup:

- Press L + R + B in game to show or hide the internal version window.
- It also closes automatically after five seconds.

Touch fix:

- The 3DS bottom screen now uses one direct hardware touch path, avoiding duplicate SDL + HID taps.
- Touch coordinates are mapped against the fixed 320x240 bottom-screen layout used by the 3DS renderer.
- Touches request an immediate bottom-screen redraw, and the bottom renderer now prepares and presents frames during Old 3DS top-frame skips so menus and the overworld map feel more responsive.

The QR points to the CIA in this tag:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/v2.1-experimental/Experimental/v2.1/zelda3-3ds-v2.1-experimental.cia

This repository does not include a ROM or extracted `zelda3_assets.dat`.
Please test on real Old 3DS hardware before promoting this experimental tag to a GitHub Release.
