# Repository map

This guide describes `zelda-alttp-3ds`. Start with the [project README](../README.md)
for downloads, supported devices, installation, controls and upstream credits.
The root keeps discoverable build entry points, project metadata and legal
notices. Implementation, resources and supporting documents are grouped below.

## Source ownership

| Location | Responsibility |
| --- | --- |
| `engine` | Shared Zelda3 C engine, software PPU, Opus source and upstream notices. |
| `third_party/SDL2` | Vendored SDL2 used to build its Nintendo 3DS backend. |
| `platform/3ds` | 3DS device code, updater, build entry point, packaging and host regressions. |
| `tools` | Second-screen data producers and development utilities. |
| `scripts` | Repository checks. |
| `docs` | Current guides, original/presentation artwork and contribution guidance. |

Source file moves must preserve include paths, generated inputs, toolchain
flags, runtime data locations and package contents. Update every consumer
together and run the affected build and regressions. A source relocation is
not a gameplay change or a new performance claim.

## Find the right task

- Installation and diagnostics: use the README's current instructions.
- Game-specific development: follow the build/test guides linked there.
- File placement: use the table above and the checked
  [layout manifest](repository-layout.json).
- Repository check: run `python3 scripts/check-repository.py` at the root.
- Release history: use the existing GitHub Releases and immutable version tags.

## Names and labels

Public documentation, new comments, commit messages, issue templates and
metadata are written in English. Keep source symbols and names that are
compatibility contracts. Preserve upstream notices and original quoted
evidence in their original form.

Repository topics describe the actual project and technology. Issue labels
classify work: `bug`, `documentation`, `enhancement`, `question`, `build`,
`performance`, `graphics`, `audio` and `compatibility`. Existing specialized
labels remain available. Version tags identify released source; do not move
them to an organizational commit or replace published binaries for a file move.

## Keep the source tree reproducible

Keep generated builds, SDK installations, caches, game inputs, saves and private
diagnostics outside tracked source. Generated source and redistributable
resources that are required by the build remain tracked with their producer
and license context. Imported dependencies keep their own provenance and
formatting. Private operating instructions stay outside this repository.

The repository check covers current entry-point documentation and declared
file placement. Historical investigations retain their version scope and do
not establish current hardware performance or compatibility.


## Supported source scope

This branch maintains the Nintendo 3DS application and the host tools needed to
build, generate data for, or verify it. Independent Android, desktop and other
console application shells have been retired where they were separate from the
3DS dependency graph. Required shared engines and upstream dependencies retain
their source and license context. A platform name in a shared dependency is not
by itself evidence that the file is unused.

The cleanup preserves game algorithms, save formats, controls, display policies
and device lifecycle behavior. Existing release tags and downloadable packages
remain version records. Source organization does not establish new hardware
performance or compatibility guarantees.

`engine/src/platform/linux/second_screen_sdl.c` is shared second-screen code
compiled into the 3DS target. Its inherited directory name is retained so
relative headers and generated tables remain stable. SDL2 is kept as an intact
upstream dependency; the removed Android application is not needed to build it.
The required asset extraction patch lives in `platform/3ds/romfs/`.
