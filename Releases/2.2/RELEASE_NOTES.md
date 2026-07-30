## Link to the Past 3DS v2.2

Hotfix for the first-run setup issue reported in issue #2.

### Fixed

- Fixed the tiny purple `.ini` parse warnings/errors shown after first-run extraction.
- The shared engine config parser now accepts 3DS-specific settings that are parsed by the 3DS runtime:
  - `DisplayMode`
  - `WideMode`
  - `WideEdgeMode`
  - `CStickMode`
  - `CStickTurboMultiplier`

### Diagnosis

The bundled `zelda3.ini` was valid for the 3DS runtime, but the shared engine parser did not know about several 3DS-only keys. After extraction, the game parsed the same file again and printed “Can't parse” for those lines. This was not a ROM/header problem.

### From v2.1

- Old 3DS / Old 2DS optimized profile remains included.
- New 3DS keeps the stable rendering profile.
- L + R + B diagnostic overlay shows version, detected profile, and current visual FPS.

### Old 3DS recommendation

Use **Original** screen mode on Old 3DS / Old 2DS for best performance. Wide mode remains available, but it can reduce FPS significantly on Old hardware and is recommended mainly for New 3DS.
