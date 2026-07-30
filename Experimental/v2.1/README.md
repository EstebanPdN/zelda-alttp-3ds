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
- Old 3DS / Old 2DS: automatically switches to an Old 3DS profile. It keeps game logic at 60 Hz, uses a steadier top-screen presentation target, lowers audio callback pressure, and keeps the bottom screen asynchronous. Original, Stretch, and Wide are all selectable; Wide remains experimental on Old hardware because it asks the software PPU to draw a larger 400px-wide scene.

Version popup:

- Press L + R + B in game to show or hide the internal version window.
- It also closes automatically after five seconds.

Touch fix:

- The 3DS bottom screen now uses one direct hardware touch path, avoiding duplicate SDL + HID taps.
- Touch coordinates are mapped against the fixed 320x240 bottom-screen layout used by the 3DS renderer.
- Touches request an immediate bottom-screen redraw, and the bottom renderer now prepares frames during Old 3DS top-frame skips so menus and the overworld map feel more responsive.

The QR points to the CIA in this tag:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/v2.1-experimental/Experimental/v2.1/zelda3-3ds-v2.1-experimental.cia

This repository does not include a ROM or extracted `zelda3_assets.dat`.
Please test on real Old 3DS hardware before promoting this experimental tag to a GitHub Release.
