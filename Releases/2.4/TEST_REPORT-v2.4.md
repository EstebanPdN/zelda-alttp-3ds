# Test Report v2.4

- Built the Nintendo 3DS CIA and 3DSX successfully with `./platform/3ds/build.sh`.
- Verified generated SMDH metadata contains the stable application text only:
  - `The Legend of Zelda`
  - `A Link to the Past 3DS port`
  - `EstebanPdN`
- Verified SMDH metadata does not contain `2.4`, `2.3`, or `experimental`.
- Verified tracked files do not include ROMs, extracted assets, or dump binaries.
- Verified release SHA256 checksums.
