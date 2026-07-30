# GitHub Release Workflow

This file documents how this project publishes GitHub releases for the 3DS
port. It is intentionally procedural so future releases keep the same shape.

## Public Policy

- Publish completed builds directly to GitHub unless the user explicitly asks
  for a local-only build.
- Do not use Catbox for normal releases.
- The QR code must point directly to the GitHub `.cia` asset URL.
- Keep public GitHub releases consolidated to important milestones when needed.
- Do not commit release artifacts, old experiment builds, QR images, `.cia`,
  `.3dsx`, or packaged source zips to the repository.
- Stage release files in a temporary local directory outside git, such as
  `/tmp/zelda3-release-vX.Y/` or ignored `release-staging/vX.Y/`.
- If a broken release is replaced, update or delete the public GitHub release
  and clean up its tag as needed; do not keep obsolete artifacts in the repo.

## Release Page Format

The release title must be:

```text
vX.Y
```

The release body must contain only:

```text
Link to the Past 3DS

QR
![QR-vX.Y-github.png](https://github.com/EstebanPdN/zelda-alttp-3ds/releases/download/vX.Y/QR-vX.Y-github.png)

Legal Notice

Changelog
```

The QR section must embed the QR image with Markdown, not only mention the
asset name. Use the public GitHub release asset URL for the QR image. The QR
image itself must encode the direct GitHub `.cia` asset URL:

```text
https://github.com/EstebanPdN/zelda-alttp-3ds/releases/download/vX.Y/zelda3-3ds-vX.Y.cia
```

Do not include user reports, dump reviews, file-by-file source diffs, hashes,
technical notes, or unchanged behavior in the public release body.

## Public Assets

Upload only these files:

```text
zelda3-3ds-vX.Y.cia
zelda3-3ds-vX.Y.3dsx
zelda3-3ds-vX.Y-source.zip
QR-vX.Y-github.png
```

GitHub also provides automatic source archives for each tag. Internal notes,
hashes, test reports, detailed build info, and technical notes may stay in an
ignored temporary staging folder, but must not be committed.

The uploaded `zelda3-3ds-vX.Y-source.zip` must contain only the source tree for
that exact tagged version. It must not contain historical `Release/`,
`Releases/`, `Experimental/`, `build-3ds/`, `.git/`, dumps, ROMs, generated
CIAs/3DSX files from other versions, or any old testing packages.

## Legal Rules

- Never upload or commit a ROM.
- Never upload or commit `zelda3_assets.dat` or extracted Nintendo assets.
- Each user must provide their own legally obtained compatible ROM.
- The CIA may include the port code, redistributable project assets, banner
  assets, icon assets, configuration, and the upstream patch needed for local
  extraction.

Before publishing, scan tracked files for forbidden content:

```sh
git ls-files | rg '(\.sfc$|\.smc$|zelda3_assets\.dat$|zelda3_assets\.tmp$|ram\.bin$|vram\.bin$|sram\.bin$)'
```

The command should print nothing.

## Version Metadata

Every release must keep the visible 3DS HOME Menu metadata stable so updates
replace the same installed application:

- `platform/3ds/CMakeLists.txt`
- `platform/3ds/build.sh`

The generated SMDH metadata must remain:

```text
The Legend of Zelda
A Link to the Past 3DS port
EstebanPdN
```

Do not put release numbers, experimental labels, or changelog text in HOME Menu
metadata. Version labels belong in filenames, release notes, tags, and the
in-game diagnostics overlay only.

After building, parse the generated icon metadata and confirm the stable strings
are present and version/experimental strings are absent.

## New 3DS / Old 3DS Isolation Rules

New 3DS is the stable profile. Old 3DS is the experimental optimization profile.
Do not let an Old 3DS optimization change New 3DS behavior.

- Treat `v2.0` as the visual/performance baseline for the New 3DS renderer.
- Any risky renderer, PPU, timing, worker, cache, sprite, color math, or
  presentation experiment must live behind an explicit Old 3DS-only backend or
  wrapper selected after `Platform3DS_IsNew3DS()` detection.
- Do not place Old 3DS experiments directly in shared files such as
  `app/jni/src/snes/ppu.c` unless the shared file only dispatches to separate
  New/Old implementations and the New branch remains byte-for-byte equivalent
  in behavior to the stable baseline.
- Prefer duplicated implementation files for risky code paths, for example a
  stable New 3DS PPU path and a separate Old 3DS experimental PPU path, instead
  of clever shared fast paths.
- Before publishing or tagging an Old 3DS experiment, compare the New 3DS path
  against the stable baseline and verify that no Old-only flag, cache, scanline
  shortcut, frame pacing change, or worker split is active on New 3DS.
- Never use visual degradation such as half scanlines, reduced vertical
  resolution, or duplicated rows as an Old 3DS performance strategy.

## 3DS Screen Mode Rules

- Keep the Screen menu as two rows:
  - `Mode`: cycles `Original`, `Stretch`, and `Wide`.
  - `Wide`: cycles `Standard` and `Fixed`; it only changes the wide renderer
    while `Mode` is `Wide`.
- `Wide` + `Standard` must preserve native sprite spawn/despawn/gameplay logic;
  do not enable `kFeatures0_ExtendScreen64`.
- `Wide` + `Fixed` must also preserve native sprite spawn/despawn/gameplay
  logic; do not enable `kFeatures0_ExtendScreen64`. Fixed mode may adjust only
  render-time camera/OAM presentation state and must restore it before the
  frame ends.
- A rollback/rebuild release based on v1.8 may use the conservative bundled
  default `DisplayMode = Stretch` plus `WideMode = Standard`; do not silently
  default to the experimental fixed edge mode.
- The 3DS build should use `extend_y` / 240-line rendering so the top screen
  does not leave unused black rows at the top and bottom.
- Do not mutate `BG*_HOFS_copy*`, OAM memory, sprite/ancilla bounds, dungeon
  camera bounds, or transition target coordinates from Screen menu changes.

## 3DS Bottom Screen UI Rules

- Text on the 320x240 bottom screen must be readable on real hardware. Do not
  shrink text merely to fit more rows; use pages, scrolling, or submenus.
- Avoid dense two-column settings layouts for long labels such as button remap
  entries. Prefer one readable column with a visible page/scroll control.
- Developer diagnostics should use a dedicated readable panel when possible,
  not tiny always-on text that is hard to read during gameplay.

## 3DS HOME Menu Close Rules

- Do not use `svcExitProcess()` to close the title after HOME Menu ->
  Close Software; it can bypass libctru/SDL teardown and cause the 3DS system
  error screen.
- Detect system close cooperatively with APT (`aptMainLoop()`,
  `aptShouldClose()`, and the `APTHOOK_ONEXIT` flag), stop the main loop, and
  return normally from `main`.
- Shutdown paths must not wait forever on worker threads. Use bounded joins for
  3DS render/second-screen workers and log timeouts instead of blocking on
  `threadJoin(..., U64_MAX)`.
- During a system close, avoid shutdown-only GPU waits that can stall the HOME
  Menu close path; finish/end active frames, then let normal service teardown
  run.

## Changelog Rules

- Compare the new release against the previous release.
- List only changes made in this release.
- Do not mention behavior that was merely kept unchanged.
- If the user reported specific bugs, map bullets directly to those bugs.
- Keep implementation detail out of the GitHub release body unless it explains
  a user-visible fix.
- Put detailed implementation notes in `TECHNICAL_NOTES-vX.Y.md`.

## Release Staging Folder

Each release may use a temporary staging folder outside tracked source control:

```text
release-staging/X.Y/
  zelda3-3ds-vX.Y.cia
  zelda3-3ds-vX.Y.3dsx
  zelda3-3ds-vX.Y-source.zip
  QR-vX.Y-github.png
  SHA256SUMS.txt
  README.md
  INSTALL-vX.Y.md
  CHANGELOG-vX.Y.md
  TECHNICAL_NOTES-vX.Y.md
  TEST_REPORT-vX.Y.md
  BUILD_INFO-vX.Y.md
  GITHUB_RELEASE.txt
```

This folder is ignored by git and must not be pushed. After the GitHub release
is verified, the staging folder can be deleted.

Create the source zip from a clean tracked snapshot, not from the full working
directory. Preferred method:

```sh
git archive --format=zip --prefix=zelda3-3ds-vX.Y-source/ \
  -o release-staging/X.Y/zelda3-3ds-vX.Y-source.zip vX.Y
```

If the tag has not been created yet, create the zip from `HEAD` only after
checking that `git status --short` contains no unintended changes:

```sh
git archive --format=zip --prefix=zelda3-3ds-vX.Y-source/ \
  -o release-staging/X.Y/zelda3-3ds-vX.Y-source.zip HEAD
```

## Publishing Commands

Use the configured remote named `github`:

```sh
git tag -a vX.Y -m "Link to the Past 3DS vX.Y"
git push github HEAD:main
git push github vX.Y
```

Create the release:

```sh
gh release create vX.Y \
  "/path/to/release-staging/X.Y/zelda3-3ds-vX.Y.cia" \
  "/path/to/release-staging/X.Y/zelda3-3ds-vX.Y.3dsx" \
  "/path/to/release-staging/X.Y/zelda3-3ds-vX.Y-source.zip" \
  "/path/to/release-staging/X.Y/QR-vX.Y-github.png" \
  --repo EstebanPdN/zelda-alttp-3ds \
  --title "vX.Y" \
  --notes-file "/path/to/release-staging/X.Y/GITHUB_RELEASE.txt" \
  --latest
```

If replacing a bad public release:

```sh
gh release delete vOLD --repo EstebanPdN/zelda-alttp-3ds --yes --cleanup-tag
```

## Post-Publish Check

Verify the public state:

```sh
gh release list --repo EstebanPdN/zelda-alttp-3ds --limit 20
gh release view vX.Y --repo EstebanPdN/zelda-alttp-3ds \
  --json name,tagName,url,assets,targetCommitish \
  --jq '{name, tagName, targetCommitish, url, assets: [.assets[].name]}'
git ls-remote --heads github main
git ls-remote --tags github 'refs/tags/vX.Y'
```

The assets list should contain only the CIA, 3DSX, source zip, and QR image.
