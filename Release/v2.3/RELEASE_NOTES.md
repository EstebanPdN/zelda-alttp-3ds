![QR-v2.3-github.png](https://github.com/EstebanPdN/zelda-alttp-3ds/releases/download/v2.3/QR-v2.3-github.png)

## Note

I'm still working on improving **Old 3DS performance**, and a new version focused on further optimization should be available soon. For now, **Original** screen mode is still recommended on Old 3DS / Old 2DS for the best performance. Wide mode remains available, but it is heavier on Old 3DS because it renders a larger 400px-wide scene in software.

## Changelog

- Promoted the corrected v2.3.1 startup fix to the official v2.3 release.
- Fixed the first-run setup/config issue where valid 3DS-only `.ini` keys could be reported as parse errors after extraction.
- Fixed the black screen reported after setup on some systems by negotiating the Core 1 CPU time budget with APT before starting the Core 1 PPU worker.
- Avoids creating a Core 1 PPU worker if the system reports 0% CPU budget for that core, preventing a startup deadlock.
- Keeps Core 2 active on New 3DS systems when available.
- Improved HOME Menu / software close behavior.
- Keeps automatic New 3DS / Old 3DS detection and the Old 3DS optimized runtime profile.
- Keeps the L + R + B diagnostic overlay for checking version, detected profile, and FPS.
- Preserves clean install metadata: The Legend of Zelda / A Link to the Past 3DS port / EstebanPdN.
