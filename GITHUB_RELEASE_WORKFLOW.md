# GitHub Release Workflow

This file documents how this project publishes GitHub releases for the 3DS
port. It is intentionally procedural so future releases keep the same shape.

## Public Policy

- Publish completed builds directly to GitHub unless the user explicitly asks
  for a local-only build.
- Do not use Catbox for normal releases.
- The QR code must point directly to the GitHub `.cia` asset URL.
- Keep public GitHub releases consolidated to important milestones when needed.
  Local `Releases/X.Y/` folders and git history must remain complete.
- If a broken release is replaced, delete that public GitHub release and clean
  up its tag, but do not delete local release folders or source history.

## Release Page Format

The release title must be:

```text
Link to the Past 3DS vX.Y
```

The release body must contain only:

```text
Link to the Past 3DS

QR

Legal Notice

Changelog
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
hashes, test reports, detailed build info, and technical notes stay inside the
local release folder and source zip.

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

Every release must update the visible 3DS HOME Menu metadata:

- `platform/3ds/CMakeLists.txt`
- `platform/3ds/build.sh`
- project README/version references

The long HOME Menu name should include the version, for example:

```text
A Link to the Past 3DS vX.Y
```

After building, parse the generated SMDH to confirm the new long name is
present. Do not rely only on filenames.

## Changelog Rules

- Compare the new release against the previous release.
- List only changes made in this release.
- Do not mention behavior that was merely kept unchanged.
- If the user reported specific bugs, map bullets directly to those bugs.
- Keep implementation detail out of the GitHub release body unless it explains
  a user-visible fix.
- Put detailed implementation notes in `TECHNICAL_NOTES-vX.Y.md`.

## Local Release Folder

Each release keeps a complete local folder:

```text
Releases/X.Y/
  source/
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
  "/path/to/Releases/X.Y/zelda3-3ds-vX.Y.cia" \
  "/path/to/Releases/X.Y/zelda3-3ds-vX.Y.3dsx" \
  "/path/to/Releases/X.Y/zelda3-3ds-vX.Y-source.zip" \
  "/path/to/Releases/X.Y/QR-vX.Y-github.png" \
  --repo EstebanPdN/zelda-alttp-3ds \
  --title "Link to the Past 3DS vX.Y" \
  --notes-file "/path/to/Releases/X.Y/GITHUB_RELEASE.txt" \
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

