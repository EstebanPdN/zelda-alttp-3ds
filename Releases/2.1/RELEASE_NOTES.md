## Link to the Past 3DS v2.1

This release adds an optimized Old 3DS / Old 2DS profile while keeping the stable v2.0 behavior on New 3DS.

### Changes

- Automatically detects New 3DS vs Old 3DS at startup.
- New 3DS keeps the normal v2.0 rendering profile.
- Old 3DS / Old 2DS now uses 60 Hz game logic with adaptive visual frame pacing so render pressure does not slow down gameplay.
- Bottom screen rendering can update independently during Old 3DS top-screen skips, improving settings, map, and touch responsiveness.
- L + R + B shows an in-game diagnostic overlay with:
  - version 2.1
  - detected profile, New 3DS or Old 3DS
  - current visual FPS
- The L + R + B overlay can be hidden with L + R + B again and auto-closes after five seconds.

### Recommended Old 3DS setting

On Old 3DS / Old 2DS, use **Original** screen mode for the best performance.

Wide mode remains available, but it can drop FPS significantly on Old 3DS because it renders a larger 400px-wide scene in software. Wide is recommended mainly for New 3DS.

### Install note

The CIA keeps the same app metadata as v2.0, so installing v2.1 should update the same HOME Menu app instead of creating a duplicate icon.
