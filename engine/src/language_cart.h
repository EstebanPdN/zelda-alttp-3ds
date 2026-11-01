#ifndef ZELDA3_LANGUAGE_CART_H_
#define ZELDA3_LANGUAGE_CART_H_

#include <stddef.h>
#include "types.h"

// A language patch turns the USA cartridge into a translated one, which the
// profile keeps beside its assets. The game draws the translation's own
// lettering from it: the graphics sheets it redrew (HUD, menus, title) and
// the C code's tables of names that hold its words. Translations of the USA
// cartridge (Spanish, Italian, Portuguese, Russian) give their file screens
// too; the official German and French releases give their menus.
// The game itself stays the USA one; the official French Canadian cartridge
// has its own, fuller path (localized.h).

// The file a translated profile carries, next to zelda3_assets.dat.
#define LANGUAGE_CART_FILE "zelda3_language.sfc"

// Loads the profile's translated cartridge for `language` (NULL, English
// and fr-c have none) and lays its redrawn sheets over the assets as a mod
// would. Called after the assets are loaded, before the player's mods, so a
// mod still wins. Returns how many sheets changed.
int LanguageCart_ApplyToAssets(const char *language);

// The cartridge's bytes for a table of the C code at `addr` when the
// translation is in use and the table is one of the language's (names in
// the menus), else NULL.
const uint8 *LanguageCart_Table(uint32 addr, size_t size);

// True while the Japanese cartridge is in use: its file screens, keyboard and
// names are its own (select_file.c).
bool LanguageCart_Japanese(void);

// Brings the names in this SRAM and the game in `wram_save` to the font in use.
void LanguageCart_AfterSramLoad(uint8 *sram, uint8 *wram_save);

// Its bytes at a LoROM address, whatever the layout.
const uint8 *LanguageCart_RawBytes(uint32 snes_addr, size_t size);

// True while a translated cartridge is in use: the menus read its words.
bool LanguageCart_Active(void);

// True while that cartridge keeps the USA layout: the file screens are drawn
// from it too, with the language's font. The official German and French
// releases give their menus only.
bool LanguageCart_UsaLayout(void);

// Copies `count` little-endian words of the translated cartridge at a LoROM
// address: the HUD and menus read their lettering tables this way, at the
// addresses the USA cartridge keeps them (hud.c, Hud_Text).
const uint16 *LanguageCart_Words(uint16 *dst, uint32 snes_addr, int count);

// `size` bytes of the translated cartridge at a LoROM address, or NULL. The
// file screens follow their tables there when the translation moved them
// (select_file.c).
const uint8 *LanguageCart_Bytes(uint32 snes_addr, size_t size);

// Copies the menu characters a cartridge keeps apart (the German release)
// to BG3 characters $180 of `vram`, the BG3 character base. Called when the
// item menu opens; message text redraws that area when it next appears.
void LanguageCart_UploadMenuChars(uint16 *vram);

#endif  // ZELDA3_LANGUAGE_CART_H_
