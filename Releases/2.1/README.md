# Link to the Past 3DS v2.1

Official 3DS build based on the published v2.0 line, with added Old 3DS / Old 2DS optimizations.

This release keeps the installed application metadata from v2.0:

- SMDH title: Link to the Past 3DS v2.0
- CIA title: ALttP 3DS v2.0
- ProductCode: CTR-P-Z3DS
- UniqueId: 0x5A20D

That means installing this CIA should update the same HOME Menu app instead of creating a second icon.

## What's new in v2.1

- Automatic New 3DS vs Old 3DS detection at startup.
- New 3DS keeps the stable v2.0 rendering profile.
- Old 3DS / Old 2DS uses an optimized profile with 60 Hz game logic and adaptive top-screen frame pacing.
- Bottom screen rendering can update independently during Old 3DS top-screen skips, improving touch menus, settings, and map/status responsiveness.
- L + R + B shows an in-game diagnostic overlay with the version, detected 3DS profile, and current visual FPS. Press L + R + B again to hide it; it also closes automatically after five seconds.

## Recommended Old 3DS settings

For Old 3DS / Old 2DS, the recommended screen mode is **Original**.

Wide mode is still selectable, but it asks the software PPU to draw a larger 400px-wide scene and can drop FPS heavily on Old 3DS hardware. Wide mode is recommended mainly for New 3DS.

## Downloads

- CIA: `zelda3-3ds-v2.1.cia`
- 3DSX: `zelda3-3ds-v2.1.3dsx`
- QR: `QR-v2.1-github.png`

The QR points to:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/v2.1/Releases/2.1/zelda3-3ds-v2.1.cia

This repository does not include a ROM or extracted `zelda3_assets.dat`.
