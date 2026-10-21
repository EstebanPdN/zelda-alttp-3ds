# Zelda 3DS platform

Native dual-screen Nintendo 3DS frontend, public v3.0.

## Console installation

Install the CIA or launch the 3DSX. Place a legally obtained compatible `.sfc`
or `.smc` ROM in `sdmc:/3ds/Zelda 3DS/`. The preferred baseline is USA,
unheadered. Audio requires `sdmc:/3ds/dspfirm.cdc`; Rosalina can dump the
console's DSP firmware. Assets are extracted on the console.

ROM profiles retain their own saves and settings. Keep ROM filenames unchanged
when updating. The public CIA retains v2.9's title ID `0004000005a13e00`.

## Display and controls

- WIDE: native 400x240 gameplay. ORIGINAL: 256x224. STRETCH fills the display.
- WIDE camera: STANDARD or FIXED. WIDE/FIXED is applied once per profile;
  later saved display choices are honored.
- Bottom screen: 320x240 map, gear, touch inventory and settings.
- D-Pad/Circle Pad: movement. A/B/X/Y, L/R, Start/Select: game controls.
- Old 3DS X: tap for normal X; hold one second for configured turbo.
- New 3DS ZL/C-stick: hold for turbo when enabled. X remains immediate.
- L + R + A: create a diagnostic dump.
- Settings > Developer > Load State: confirm loading the newest dump's
  validated checkpoint. Another ROM profile's checkpoint is rejected.
- Settings > Developer > Show FPS: optional top-screen counter.
- Title-screen Triforce: display and turbo settings.

Both display paths use nearest-neighbor sampling. Old 3DS uses the PICA200
renderer for supported frames and the CPU path for unsupported effects;
New 3DS retains its CPU renderer. Logic runs on a fixed 60 Hz accumulator,
with bounded catch-up. Rendering performance is scene-dependent.

Old UI drawing is asynchronous. Damage/healing patches retained heart cells
without rebuilding the map. Automatic map jobs defer through door transitions;
explicit touch keeps priority. The APT notification thread uses priority0x19
so HOME/sleep requests can be received while gameplay is busy. Runtime logs
report the selected priority; other thread priorities are retained.

## MSU-1 audio

MSU-1 tracks replace the SPC music with streamed audio. No tracks ship with this
project; supply your own pack.

Put the pack in `sdmc:/3ds/Zelda 3DS/msu/` using the naming stock packs ship
with (`alttp_msu-1.pcm` … `alttp_msu-35.pcm`; `alttp_msu-0.pcm` is an optional
menu track). No renaming is needed.

`MSUPath` is a **prefix**, not a folder: the loader appends the track number and
`.pcm`. Startup probes these prefixes in order and adopts the first whose
`<prefix>1.pcm` exists:

1. `msu/alttp_msu-` — stock MSU pack naming (Snes9x/bsnes)
2. `msu/` — plain numbers in the subfolder
3. `alttp_msu-` — loose files in the working directory
4. (empty) — bare numbers

Finding one turns MSU on, points `MSUPath` at that prefix, and switches the
output to 44100 Hz. Nothing else is required.

Format is MSU-1 PCM: 44100 Hz, 16-bit signed, stereo interleaved, with an 8-byte
header (`"MSU1"` + `repeat_position` as little-endian uint32).

`tools/check_msu_tracks.py` validates a pack before it reaches the SD card:

```sh
python3 tools/check_msu_tracks.py path/to/msu
```

It checks the header, the 44100/16-bit/stereo sizing, duration plausibility and
gaps in the numbering, and exits non-zero on anything fatal.

Settings live in `[Sound]` of `zelda3.ini`:

| Key | Meaning |
|---|---|
| `EnableMSU` | `true`/`1` = PCM, `deluxe` = Deluxe variant. Detection fills this in when a track exists and it is not already set. |
| `MSUPath` | Track directory relative to the working directory. A missing trailing slash is added for you. |
| `MSUVolume` | Mix volume, `0%`–`100%`. |
| `ResumeMSU` | `1` = resume a track when returning to an area instead of restarting it. |

`AudioFreq` and `AudioChannels` are set by the platform layer at startup, so
editing them here has no effect. Opuz is not used: it needs 48000 Hz and this port
plays MSU at 44100 Hz, so an opuz flag is dropped with a log line.

Sample rate follows MSU: 44100 Hz when MSU is active or on New 3DS, otherwise the
previous 32000 Hz. The SPC700 interpreter's cost is fixed per frame, so the higher
rate only adds DSP resampling — and while MSU owns the output that pass is skipped.

Expected startup log:

```
Audio config: 44100 Hz stereo, MSU on (enable=0x01, tracks found)
MSU: loading track 1 from msu/1.pcm
```

`MSU off (tracks not found)` means the probe found nothing: check the path and the
file names. `MSU: track N not found` means the directory resolved but that track is
missing, and playback falls back to SPC for it.

Deluxe packs are not supported: they need an ASM patch applied to the ROM, and this
port runs the engine directly rather than emulating a patched cartridge.

## Diagnostics

Press `L + R + A` while the issue is visible and attach the dump from:

`sdmc:/3ds/Zelda 3DS/dumps/`

Folders use `000-dump-YYYYMMDD-HHMMSS`, `001-dump-...`, etc. Numbering continues
across restarts; an empty collection starts at zero. Legacy dumps remain
loadable. `DUMP SAVED` confirms a completed capture. Audio pauses during
capture and resumes afterward.

Dumps include physical top/bottom BMP and raw captures, RAM/VRAM/CGRAM/OAM,
scene/register context, a validated `load-state.bin`, and a checksum manifest.
Old recent timing history and audio/UI diagnostics help locate stalls. GPU
submissions and fallback reasons appear in `ppu.txt`; captured GPU output is
read through CPU-visible VRAM without submitting a separate GPU frame.
Timing spans include preemption and may overlap; they are not CPU-cycle counts.

## Building

Requirements: devkitARM, libctru, 3ds-cmake, makerom and bannertool. SDL2 is
vendored in `third_party/SDL2`.

```sh
bash platform/3ds/build.sh
```

Output: `build-3ds/game/zelda3-3ds-v3.0.cia` and `.3dsx`. Packages contain
configuration and the extraction patch, never ROMs or extracted game assets.

HOME Menu metadata:

```text
Short name:  The Legend of Zelda
Long name:   A Link to the Past 3DS port
Author:      EstebanPdN
ProductCode: CTR-P-Z3DE
UniqueId:    0x5A13E
```

The custom logo banner is prebuilt in `assets/banner.cgfx`. Technical GPU
implementation records are preserved in `PICA200-E12.md`, `PICA200-E13.md` and
`PICA200-E14.md`. Focused source-level regressions live in `tests/`.

## Updates

In Settings > Update, choose Stable or Pre-release. Tap the release name to
read its changelog on the top screen, with Prev/Next below for more pages.
Choose Download Update and confirm installation, then reopen the game.
Save in-game before installing. Startup checks also indicate newer releases.

Version [v3.2](https://github.com/EstebanPdN/zelda-alttp-3ds/releases/tag/v3.2)
is the latest stable release. Select the Stable channel to install it. It fixes Restart retaining stale game RAM after ROM
reselection, which could leave gameplay black and silent.

Settings is Screen, Turbo Speed, Developer, Update, Restart. Restart opens
the ROM selector and starts the selected ROM fresh; existing saves remain.

Downloads use verified HTTPS and the GitHub asset's SHA-256/size. CIA title ID
must match this port. Channel selection persists in update/channel.txt.
Versions use vMAJOR.MINOR[.PATCH] with optional -E<number>; matching assets use
zelda3-3ds-vVERSION.cia or .3dsx. Only newer versions are offered. The
pre-release list scans 100 release records; changelogs show up to 12 KiB.

Content tabs select their view without toggling back to Map. Touch targets
cover button borders and spaces between them. A completed UI worker result
is presented in the same frame when available; unfinished jobs stay asynchronous.
No additional periodic drawing, busy waiting or game pacing change is used.

Version 3.1 uses the native HID contact flag for touch press and release detection,
including startup and releases with residual coordinates. Physical coordinates
remain protected from SDL viewport transforms. The last 32 touch selections
are included in diagnostic dumps. Use the FBI QR if touch controls prevent
opening Update.
