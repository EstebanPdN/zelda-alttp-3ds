# Link to the Past 3DS v2.2

Official hotfix for the first-run setup issue reported in GitHub issue #2.

This release keeps the installed application metadata from v2.0:

- SMDH title: Link to the Past 3DS v2.0
- CIA title: ALttP 3DS v2.0
- ProductCode: CTR-P-Z3DS
- UniqueId: 0x5A20D

Installing this CIA should update the same HOME Menu app instead of creating a second icon.

## What's fixed in v2.2

- Fixed the first-run `.ini` parser warnings/errors for 3DS-specific settings.
- The general engine config parser now accepts the 3DS runtime keys that are handled by `platform_3ds.c`:
  - `DisplayMode`
  - `WideMode`
  - `WideEdgeMode`
  - `CStickMode`
  - `CStickTurboMultiplier`
- This addresses the tiny purple “Can't parse” text reported after first extraction.

## Still included from v2.1

- Automatic New 3DS vs Old 3DS detection.
- Optimized Old 3DS / Old 2DS profile.
- L + R + B diagnostic overlay showing version, detected profile, and visual FPS.

## Recommended Old 3DS settings

For Old 3DS / Old 2DS, the recommended screen mode is **Original**.

Wide mode remains selectable, but it can drop FPS heavily on Old 3DS because it renders a larger 400px-wide scene in software. Wide mode is recommended mainly for New 3DS.

## Downloads

- CIA: `zelda3-3ds-v2.2.cia`
- 3DSX: `zelda3-3ds-v2.2.3dsx`
- QR: `QR-v2.2-github.png`

The QR points to:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/v2.2/Releases/2.2/zelda3-3ds-v2.2.cia

This repository does not include a ROM or extracted `zelda3_assets.dat`.
