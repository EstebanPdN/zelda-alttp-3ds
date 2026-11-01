#ifndef ZELDA3_MODS_H_
#define ZELDA3_MODS_H_

#include <stddef.h>
#include "types.h"

// Mods change the game's data, never its code: they replace or patch assets
// of zelda3_assets.dat (graphics, palettes, dialogue, and for a whole quest
// its rooms and maps), and for a quest, the tables and values of the game's
// code its cartridge changes, which the C code asks for where it keeps them.
//
// A mod file, little endian like zelda3_assets.dat:
//    0  "Z3MOD\r\n\x1a"
//    8  version, 1 or 2                   uint32
//   12  number of entries n               uint32
//   16  the 48 bytes that open the zelda3_assets.dat it was made for
//   64  n entries, then the data of each, in entry order, each at a
//       4-aligned offset.
// Version 1 entries are 8 bytes, asset index and size, and replace whole
// assets. Version 2 entries are 16 bytes: asset, element, kind, size.
//   kind 0  the whole asset (element unused)
//   kind 1  one element of an asset made of packed elements
//   kind 2  tiles of one sheet of kSprGfx or kBgGfx: uint16 tile size,
//           uint16 count, the tile numbers (uint16 each, then padding to 4
//           bytes), then the tiles. The rest of the sheet stays as it is, so
//           two mods changing different tiles of a sheet both show.
//   kind 3  a table of the C code, the asset field giving which (kModTable_*,
//           element unused): the data replaces it whole, at its exact size,
//           or for the tables of any size, a multiple of 4 bytes.
// Entries apply in order, each on the result of the previous ones, and a
// file applies whole or not at all.
//
// The French Canadian cartridge draws a few sheets itself (localized.c).
// There, tiles (kind 2) only reach the assets' copy, which the game does not
// show: a mod made for that cartridge carries those sheets whole (kind 1),
// its tiles over the cartridge's.

// A version 1 ZSPR of Link is a skin as it stands: Mods_ApplyAssetOverlay takes
// the file directly, its graphics as asset 57 and its colors as
// kModTable_LinkPalette.
enum { kZsprHeaderSize = 29, kZsprPixelBytes = 0x7000, kZsprPaletteBytes = 124 };

// Tables of the C code a mod can replace (kind 3).
enum {
  // The flute's eight spots: x low, x high, y low, y high bytes, 8 of each.
  kModTable_BirdTravel = 0,
  // Bytes of the hack's cartridge, where the C code keeps a value of the
  // game's code or one of its tables (Mods_RomByte), and for a quest the
  // routines of its own that the C code runs (the overworld's overlays,
  // overworld.c): 4 bytes each, the SNES address in the low 24 bits and the
  // byte in the top 8. Any number. A GAME mod gives the menu's item names
  // for the USA ROM this way (hud.c).
  kModTable_RomBytes = 1,
  // Bytes of the French Canadian cartridge's copy, in the same form: the
  // screens it draws itself (localized.c) read them, so a mod for that ROM
  // can change the menu's names or the ending's captions.
  kModTable_LocRomBytes = 2,
  // Four ZSPR armor/bunny palettes (120 bytes), then two glove colors.
  // Kept as the author's data; no base-game palette bytes in skin packages.
  kModTable_LinkPalette = 3,
  kModTable_Count,
};

// The game language whose translated cartridge Mods_ApplyAssetOverlays lays
// under the mods (language_cart.h). NULL for the USA text.
void Mods_SetLanguage(const char *language);

// The LZ decompressor of the graphics sheets, kept inside both buffers:
// the decompressed size, or -1 past either end.
int Mods_DecompressSheet(uint8 *dst, size_t cap, const uint8 *src, size_t src_size);

// Applies one mod file. Its data is copied: the file can be freed after.
bool Mods_ApplyAssetOverlay(const uint8 *file, size_t size);

// Whether a file of `file_size` bytes opening with `head` is such a ZSPR, and
// where its graphics and colors are. Its checksum is not needed to use it.
bool Mods_ZsprLayout(const uint8 *head, size_t head_size, size_t file_size,
                     uint32 *pixels, uint32 *palette);

// Called right after the assets file is loaded: forgets the sheet marks and
// the buffers of a previous run, then applies the mod files of a comma
// separated list (NULL for none), in order, so a later one wins where two
// change the same thing. Returns how many were applied; the others are named
// on stderr and skipped whole.
int Mods_ApplyAssetOverlays(const char *list);

// The data a mod gave for table `id` (kModTable_*), or NULL for the game's,
// and its size in bytes (0 without).
const uint8 *Mods_Table(int id);
uint32 Mods_TableSize(int id);

// What the hack's cartridge has at SNES address `addr` (either bank form,
// $0D:B173 or $8D:B173) where the C code keeps one of the game's values: the
// byte or the little endian word kModTable_RomBytes gives, or `def`.
uint8 Mods_RomByte(uint32 addr, uint8 def);
uint16 Mods_RomWord(uint32 addr, uint16 def);
// A table of the C code that the cartridge keeps at `addr`: `def` (`size`
// bytes, laid out as in the ROM) with the bytes kModTable_RomBytes changes,
// or `def` itself where it changes none.
const void *Mods_RomTable(uint32 addr, const void *def, size_t size);
// Writes kModTable_LocRomBytes over the French Canadian cartridge's copy
// (`size` bytes, LoROM without header) once it is loaded.
void Mods_PatchLocRom(uint8 *rom, size_t size);
#define MODS_U8(t, addr) ((const uint8 *)Mods_RomTable((addr), (t), sizeof(t)))
#define MODS_I8(t, addr) ((const int8 *)Mods_RomTable((addr), (t), sizeof(t)))
#define MODS_U16(t, addr) ((const uint16 *)Mods_RomTable((addr), (t), sizeof(t)))
#define MODS_I16(t, addr) ((const int16 *)Mods_RomTable((addr), (t), sizeof(t)))

// True when a mod replaced sheet `index` of kSprGfx (asset 64) or kBgGfx
// (65) whole (kind 1): the French Canadian cartridge's own copy of that sheet
// then gives way to it (localized.c).
bool Mods_SheetFromMod(int asset, int index);

#endif  // ZELDA3_MODS_H_
