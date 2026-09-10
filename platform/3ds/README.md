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
vendored in `app/jni/SDL2`.

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

## Local v3.1-E1 updater

Settings contains Screen, Turbo Speed, Developer, Restart and Update.
Restart always opens the ROM selector and starts the selected ROM fresh;
SRAM and per-ROM saves remain intact. Automatic state restoration is skipped
for that restart.

Update checks the selected Stable or Pre-release channel from
EstebanPdN/zelda-alttp-3ds on GitHub. Checks also run in the background at
startup; available updates are indicated on the title/menu card. Choose the
channel, tap the release name to read its changelog above, and use Prev/Next
below to turn pages. Download Update opens an installation confirmation.
A completed installation closes the application; reopen it from HOME or HBL.
Save your game before installing.

Downloads use verified HTTPS, GitHub asset size and SHA-256, and the CIA title
ID must match this port. Channel selection is stored in update/channel.txt.
Only newer versions are offered: version tags use vMAJOR.MINOR[.PATCH] or
vMAJOR.MINOR[.PATCH]-E<number>; assets must be named
zelda3-3ds-vVERSION.cia / .3dsx and include GitHub's sha256 digest.
Pre-release checks scan the latest 100 release records. Changelogs display
up to 12 KiB as wrapped ASCII text with Markdown links/images simplified.

This is a local hardware-test build. The current public v3.0 and older
pre-releases do not supersede v3.1-E1. No release was published for this build.
