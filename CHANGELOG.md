# Changelog

## v4.1-E5

Experimental pre-release.

- Fixed the Japanese title logo: its colors and the subtitle plate.

## v4.1-E4

Experimental pre-release.

- Fixed the game freezing in German and French when pressing SELECT in the item menu: Link and the buttons stopped while the music and the bottom screen kept going.
- Fixed a possible hang when moving the X, L or R ring up or down from an empty slot.
- The Quality of Life fairy option now restores 10 hearts instead of 8.
- Fixed broken HUD and save-box letters when loading a state saved in another language.
- Restored the six character skins built into v4.0, missing from the earlier experimental builds. Credits: see CREDITS.md.

## v4.1-E3

Experimental pre-release.

- Character skins: copy any Link `.zspr` to the `mods` folder and choose it in Mods. No conversion, in every language, with its own name, author and portrait.
- The Mods menu now lists up to 512 mods instead of 32.

## v4.1-E2

Experimental pre-release.

- Added Japanese: the complete game from the official Super Famicom cartridge, with its dialogue, title, file screens, name keyboard, item names, inventory, maps, intro and ending.
- Translated every menu of the port into Japanese, with a new pixel font, and selected it automatically on Japanese consoles.
- Saves stay when changing language. A name in kana shows as LINK in other languages once saved.
- Fixed pressing up from the first file on the copy screen, which could select a slot that does not exist.

## v4.1-E1

Experimental pre-release.

- Fixed the invisible item menu with Expanded Inventory in WIDE, which also looked like Link freezing.
- Fixed the German name keyboard, which typed the wrong letters and functions.
- Fixed the German copy screen's slot numbers and fairy position.
- Fixed the Russian Game Over title.
- Reviewed all 396 messages in every language. Corrected typos, untranslated words, wrong hints and missing letters in Spanish, Portuguese, Italian and Russian.
- Fixed text cut off by the dialogue box in Portuguese, Italian and Russian.
- Added missing accented letters to Portuguese and Italian.
- Translated the last English message in Italian and the remaining English file-screen words.
- Fixed item names, truncated ending captions and broken file-screen words in Portuguese and Italian.
- Changed "COGE" to "TOMA" in the Spanish item menu.

## v4.0

- Added English, Spanish, Portuguese, Italian, French, German and Russian game and menu translations.
- Added a startup home with an animated title, automatic system language, Mods and Quality of Life settings.
- Reworked menus, touch navigation, item grids, equipment icons and the bottom-screen HUD; hearts resize and fill in Zelda-style rows.
- Added character skin mods with previews, automatic SD discovery and separate quest saves.
- Added on-device randomizer generation with Open, Standard, Inverted, Retro and Triforce Hunt presets, saved seeds and separate progress.
- Added exact ALttPR full-spoiler JSON imports for supported rules and goals.
- Added L/R item cycling, improved Pegasus Boots, sword pickups and repeated block pushing.
- Added reduced flashes, muted low-health beeps, brighter dark rooms and eight-heart fairies.
- Added 99-bomb/arrow limits, a 9,999-rupee wallet, expanded inventory, permanent shovel, two-way Mirror and the Kakariko shortcut.
- Extended WIDE menus and dungeon scenery with neighboring room tiles and matching floor patterns.
- Fixed room-transition walls, scrolling seams, duplicated sprites, palette mismatches and bottom-edge gaps; disconnected cave chambers now fade between views.
- Fixed map transitions, camera alignment and returning from dungeon and overworld maps.
- Fixed lingering lamp masks when leaving illuminated dark rooms.
- Added coordinated startup, Continue and Triforce fades; removed loading screens and added a missing-ROM notice with the required SD folder.
- Added title-animation and menu sound effects, preserved stereo panning and stopped previous music carrying into restarted intros.
- Fixed Game Over fairy behavior and synchronized bottom-screen death colors and fades.
- Fixed translated file menus, confirmation/cancellation and startup or language-switch input handling.
- Improved save recovery, autosave eligibility and isolation between normal games, mods and randomizer seeds.
- Fixed bottom-screen buffer corruption and refresh issues during uploads, restarts and menu changes.
- Expanded diagnostic dumps with complete screen/memory captures and reloadable checkpoints; updated dump progress presentation.
- Disabled developer test controls and B+L scenario cycling in release builds.

## v3.2

- Optimized update downloads and installation.
- Fixed black screens and missing audio after Restart and ROM reselection.

## v3.1

- Added in-game GitHub updates with Stable/Pre-release channels and verified downloads.
- Added paginated changelogs with matching screen styles.
- Fixed bottom-screen startup, touch accuracy and navigation delay.
- Simplified Settings and combined Restart with ROM selection.
- Fixed stale world colors after mirror and portal transitions.
- Added touch history to diagnostic dumps.

## v3.0

- Added PICA200 GPU rendering for Old 3DS with automatic software fallback.
- Improved Old 3DS rendering, frame pacing, audio and bottom-screen performance.
- Fixed WIDE/FIXED map alignment, terrain corruption and screen-edge gaps.
- Added native 400x240 WIDE rendering and WIDE/FIXED defaults.
- Fixed incorrect colors, screen flickering and Original-mode borders.
- Reduced slowdowns during door and room transitions.
- Improved bottom-map refresh and touch responsiveness; added the mirror-return marker.
- Fixed delayed heart updates and gameplay stalls after taking damage.
- Fixed bottom-menu borders, HUD spacing and the map's +/− buttons.
- Added title-screen display/turbo settings and improved Triforce rendering.
- Fixed per-ROM settings persistence and configuration migration.
- Fixed valid ROMs being rejected and cleaned up ROM-switch handling.
- Fixed Old 3DS X taps while preserving hold-to-turbo controls.
- Improved HOME Menu responsiveness and startup/shutdown handling.
- Added Load State, an optional FPS counter and `DUMP SAVED` feedback.
- Improved numbered diagnostic dumps, screen captures and graphics/audio reports.
- Fixed dump freezes and audio playback during captures.
- Updated the custom 3D HOME Menu logo and banner orientation.
