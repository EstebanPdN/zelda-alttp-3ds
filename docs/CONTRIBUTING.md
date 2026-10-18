# Contributing

Use the build and installation guidance linked from the project README.
Preserve the upstream attribution, existing save formats, device behavior and
component licenses. Keep ROMs, game data, saves and private diagnostics local.

## Repository organization

Read the [repository map](REPOSITORY.md) before moving files. Keep new public text in English.
Group source, platform adapters, resources, documentation and generated output
by responsibility. Update build manifests, scripts and documentation links
together. Preserve existing version tags and released package bytes.

Run `python3 scripts/check-repository.py` from the repository root before
submitting a change. The check validates the declared root layout, current
documentation links and source/private-file boundaries; it does not replace
the game-specific build, host regressions or physical-console validation.

Use the issue templates for reproducible reports and feature requests. Review
diagnostic captures before sharing and never attach ROMs, extracted game data,
credentials or personal saves to a public report.
