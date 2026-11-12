# forge.store preview media

Public preview videos used by forge.store on Nintendo 3DS. These are display
media only, not game data or installation payloads. No game release is created
or changed by adding this directory.

The store downloads the complete FVID file in the background and verifies its
byte count and SHA-256 from manifest.json before playback. FVID contains MJPEG
at 20 fps and, when present, mono 32 kHz PCM. Downloads use commit-pinned raw
URLs so an existing store build always receives its reviewed bytes.
