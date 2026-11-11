#include "localized.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "load_gfx.h"
#include "hud.h"
#include "mods.h"
#include "language_cart.h"
#include "select_file.h"
#include "snes/snes_regs.h"
#include "android_logging.h"
#ifdef __3DS__
#include "platform_3ds.h"
#endif

const uint8 *g_loc_rom;
static uint8 *g_loc_rom_owned;
static uint16 *g_loc_map16;

// The code names each table at its address in the French Canadian
// cartridge. The official French and German releases are the same game on
// the same layout with some tables moved: each range of addresses the code
// names, [lo, hi), is that far from where they keep it.
typedef struct LocShift {
  uint32 lo, hi;
  int32 shift;
} LocShift;

// The French release: the menus' tables of bank $0D are 0xC0 bytes lower
// (0xD8 below the USA ones), and the file screens' lists of bank $0E from
// $0E:9B00 five bytes higher.
static const LocShift kLocShiftsFr[] = {
  { 0x8d8000, 0x8e0000, -0xc0 },
  { 0x8e9b00, 0x8eb000, 5 },
  { 0x8eba92, 0x8ebdee, 5 },     // attract illustrations
  { 0x8ae15d, 0x8ae17d, -0x17 }, // dungeon LEVEL stripes
  // The French ending moved to bank $0A. Verified against the operands in
  // its credit writer ($8A:ABB0) and scene-caption writer ($8A:B42B).
  { 0x8ed799, 0x8ee624, 0x8a981e - 0x8ed799 },
  { 0x8ee624, 0x8ee938, 0x8aa6a9 - 0x8ee624 },
  { 0x8eec40, 0x8ef36c, 0x8aacde - 0x8eec40 },
  { 0x8ef36c, 0x8ef38e, 0x8ab40a - 0x8ef36c },
};
// The German release: bank $0D as the French one, and the file screens'
// lists of bank $0E each moved by its own amount.
static const LocShift kLocShiftsDe[] = {
  { 0x8d8000, 0x8e0000, -0xc0 },
  { 0x8e932d, 0x8e9392, -8 },     // erase: the chosen game
  { 0x8e917a, 0x8e918a, -8 },     // copy: the fairy's places and the slot numbers
  { 0x8e982c, 0x8e98ac, 12 },     // the name keyboard
  { 0x8ea293, 0x8ea390, 0x23 },   // name stripes
  { 0x8ea481, 0x8ea57e, 0x43 },   // erase: which game
  { 0x8ea5f7, 0x8ea729, 0x37 },   // copy: which game, and to where
  { 0x8eba92, 0x8ebdee, 0x39 },  // attract illustrations
  { 0x8ae15d, 0x8ae17d, 9 },     // dungeon LEVEL stripes
  // German credit writer $8E:EBDB and scene-caption writer $8E:F3EA.
  { 0x8ed799, 0x8ee624, 0x115 },
  { 0x8ee624, 0x8ee938, 0xb7 },
  { 0x8eec40, 0x8ef36c, 0xc9 },
  { 0x8ef36c, 0x8ef38e, 0x5d },
};

// The cartridges whose screens are drawn this way, the language each serves,
// the file a profile keeps it in, where its font, sheet pointers and screen
// pointers are, and how its tables sit from the French Canadian ones.
typedef struct LocCart {
  const char *language;
  const char *file;
  uint32 crc32;
  uint32 font;
  uint32 gfx_table;  // graphics sheet pointers (see kLocGfxEntries)
  uint32 bg_table;   // the file screens' three pointer columns of 9 bytes
  const LocShift *shifts;
  uint8 shift_count;
  // The German release also copies 96 characters of its menu lettering
  // from $0E:FA00 to BG3 characters $200, beside the sheets.
  uint32 menu_chars;
} LocCart;
static const LocCart kLocCarts[] = {
  { "fr-c", LOCALIZED_CART_FILE_FR_C, 0x47760381, 0x8cd078, 0x80cf26, 0x80932a,
    NULL, 0, 0 },
  { "fr", LANGUAGE_CART_FILE, 0x1f8c30e8, 0x8cc6e8, 0x80cf00, 0x80932a,
    kLocShiftsFr, countof(kLocShiftsFr), 0 },
  { "de", LANGUAGE_CART_FILE, 0x6a6c2091, 0x8cc6e8, 0x80cf00, 0x80932e,
    kLocShiftsDe, countof(kLocShiftsDe), 0x8efa00 },
};
static const LocCart *g_loc_cart;

static uint32 LocMap(uint32 addr) {
  for (int i = 0; g_loc_cart && i < g_loc_cart->shift_count; i++) {
    const LocShift *s = &g_loc_cart->shifts[i];
    if (addr >= s->lo && addr < s->hi)
      return addr + s->shift;
  }
  return addr;
}

enum {
  kLocCartSize = 0x100000,
  // Graphics sheet pointers, three columns of 223 bytes (bank, high, low):
  // kBgGfx sheets are entries 0..114, kSprGfx sheets 115..222.
  // USA $00:CF80, French Canadian $00:CF26, French and German $00:CF00.
  kLocGfxEntries = 223,
  kLocGfxBgEntries = 115,
};

// Sheets this cartridge draws differently from the USA one, found by
// decompressing all 223 of both. The others are the same picture and stay
// the engine's own.
//   bg  23: the title screen, TM where the USA one has (R)
//   bg  57: the title screen, a squarer colon
//   bg  77, 83: the compass around the Kakariko weathervane, whose W is an O
//              (Ouest) there, with the two map16 tiles below
//   bg 113, 114: the menu lettering, the same data as sprite sheets 106, 107
//   spr   0: the name keyboard's row marker, dotted
//   spr   8: "Nintendo presente"
//   spr  45: "Fin" at the end of the credits, where the USA one has "The End"
//   spr  97: the dungeon map's floor names (RC, 1er...)
//   spr 105, 106, 107: the HUD and menu lettering (PLAN, NIVEAU, VIES,
//                     EQUIPEMENT, the item names)
static const uint8 kLocBgSheets[] = { 23, 57, 77, 83, 113, 114 };
static const uint8 kLocSprSheets[] = { 0, 8, 45, 97, 105, 106, 107 };

static void LocLog(const char *message) {
#ifdef __3DS__
  Platform3DS_LogRuntime("%s", message);
#else
  fprintf(stderr, "%s\n", message);
#endif
}

static uint32 LocCrc32(const uint8 *data, size_t size) {
  uint32 crc = 0xffffffff;
  for (size_t i = 0; i < size; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xedb88320 & -(int32)(crc & 1));
  }
  return ~crc;
}

static void LocRelease(void) {
  free(g_loc_rom_owned);
  g_loc_rom_owned = NULL;
  g_loc_rom = NULL;
  g_loc_cart = NULL;
  free(g_loc_map16);
  g_loc_map16 = NULL;
}

bool Localized_SetLanguage(const char *language) {
  LocRelease();
  const LocCart *cart = NULL;
  for (size_t i = 0; language && i < countof(kLocCarts); i++)
    if (!strcmp(language, kLocCarts[i].language))
      cart = &kLocCarts[i];
  if (!cart)
    return false;
  size_t size = 0;
#ifdef __3DS__
  // The profile keeps it in its own folder, apart from the ROMs.
  char path[512];
  Platform3DS_FormatProfilePath(cart->file, path, sizeof(path));
#else
  const char *path = cart->file;
#endif
  char message[160];
  uint8 *data = ReadWholeFile(path, &size);
  if (!data) {
    snprintf(message, sizeof(message), "Localized screens: %s is not in the profile, "
             "the USA screens are used", cart->file);
    LocLog(message);
    return false;
  }
  if (size != kLocCartSize || LocCrc32(data, size) != cart->crc32) {
    free(data);
    snprintf(message, sizeof(message), "Localized screens: %s is not the %s cartridge, "
             "the USA screens are used", cart->file, cart->language);
    LocLog(message);
    return false;
  }
  Mods_PatchLocRom(data, size);
  g_loc_rom_owned = data;
  g_loc_rom = data;
  g_loc_cart = cart;
  snprintf(message, sizeof(message), "Localized screens: %s cartridge in use", cart->language);
  LocLog(message);
  return true;
}

bool Loc_UsesFrenchFloorNames(void) {
  return g_loc_cart && strcmp(g_loc_cart->language, "de") != 0;
}

bool Localized_HasCart(const char *language) {
  for (size_t i = 0; language && i < countof(kLocCarts); i++)
    if (!strcmp(language, kLocCarts[i].language))
      return true;
  return false;
}

uint32 Loc_FontAddr(void) {
  return g_loc_cart ? g_loc_cart->font : 0x8cd078;
}

static const uint8 *LocRomRaw(uint32 snes_addr) {
  return g_loc_rom + (((snes_addr >> 16) & 0x7f) << 15 | (snes_addr & 0x7fff));
}

const uint8 *Loc_Rom(uint32 snes_addr) {
  return LocRomRaw(LocMap(snes_addr | 0x800000));
}

const uint8 *Loc_BgTilemap(int index) {
  const uint8 *columns = LocRomRaw(g_loc_cart->bg_table);
  return LocRomRaw(columns[18 + index] << 16 | columns[9 + index] << 8 | columns[index]);
}

const uint8 *Loc_FileSelectStripes(int *size) {
  // LDY #size-2 / LDA table,Y at $8E:8D5A in all of them ($8E:8D58).
  const uint8 *code = LocRomRaw(0x8e8d5a);
  *size = (code[1] | code[2] << 8) + 2;
  return LocRomRaw(0x8e0000 | code[4] | code[5] << 8);
}

int Loc_StripesSize(const uint8 *stripes, int max) {
  int i = 0;
  while (i < max && !(stripes[i] & 0x80)) {
    int len = ((stripes[i + 2] & 0x3f) << 8 | stripes[i + 3]) + 1;
    i += 4 + ((stripes[i + 2] & 0x40) ? 2 : len);
  }
  return i < max ? i + 1 : max;
}

void Loc_UploadMenuChars(uint16 *vram) {
  if (!g_loc_cart || !g_loc_cart->menu_chars)
    return;
  const uint8 *src = LocRomRaw(g_loc_cart->menu_chars);
  for (int i = 0; i < 96 * 8; i++)
    vram[0x200 * 8 + i] = src[i * 2] | src[i * 2 + 1] << 8;
}

const uint16 *Loc_Words(uint16 *dst, uint32 snes_addr, int count) {
  const uint8 *src = Loc_Rom(snes_addr);
  for (int i = 0; i < count; i++)
    dst[i] = src[i * 2] | src[i * 2 + 1] << 8;
  return dst;
}

static const uint8 *LocSheet(int entry) {
  const uint8 *table = LocRomRaw(g_loc_cart->gfx_table);
  uint32 addr = table[entry] << 16 | table[kLocGfxEntries + entry] << 8 |
                table[2 * kLocGfxEntries + entry];
  return LocRomRaw(addr);
}

const uint8 *Loc_BgSheet(int index) {
  if (!g_loc_rom || Mods_SheetFromMod(65, index))
    return NULL;
  for (size_t i = 0; i < sizeof(kLocBgSheets); i++)
    if (kLocBgSheets[i] == index)
      return LocSheet(index);
  return NULL;
}

const uint8 *Loc_SprSheet(int index) {
  if (!g_loc_rom || Mods_SheetFromMod(64, index))
    return NULL;
  for (size_t i = 0; i < sizeof(kLocSprSheets); i++)
    if (kLocSprSheets[i] == index)
      return LocSheet(kLocGfxBgEntries + index);
  return NULL;
}

// The map16 tiles of that compass the cartridge composes differently: 1056
// is its O, four turns of the tile the USA E used, and 1085 its E, drawn from
// two others. kMap16ToMap8 is at $8F:8000 in both. All the tiles involved are
// plain ground, so only the picture changes.
static const uint16 kLocMap16[] = { 1056, 1085 };

const uint16 *Loc_Map16ToMap8(const uint16 *table, size_t size) {
  if (!g_loc_rom)
    return NULL;
  if (!g_loc_map16) {
    g_loc_map16 = malloc(size);
    if (!g_loc_map16)
      return NULL;
    memcpy(g_loc_map16, table, size);
    for (size_t i = 0; i < countof(kLocMap16); i++)
      if ((kLocMap16[i] + 1) * 8u <= size)
        Loc_Words(g_loc_map16 + kLocMap16[i] * 4, 0x8f8000 + kLocMap16[i] * 8, 4);
  }
  return g_loc_map16;
}

// SRAM $701FF0, past the three games and their backups and before the slot
// byte at $701FFE, which is all the cartridge uses of that end: the names in
// this SRAM are in the French Canadian codes.
static const uint8 kLocSramMark[4] = { 'F', 'R', 'C', 1 };
enum { kLocSramMarkOffs = 0x1ff0 };

// USA name code -> French Canadian one, for the characters the two
// keyboards put on different font tiles. Everything else is the same tile in
// both fonts. The USA keyboard types I and l as the same tile (0xaf), so that
// one is decided by the letter before it.
static uint16 LocConvertNameChar(uint16 c, uint16 prev) {
  if (c == 0xaf) {
    bool prev_lower = (prev >= 0x2a && prev <= 0x2f) || (prev >= 0x40 && prev <= 0x4f) ||
                      (prev >= 0x60 && prev <= 0x63) || prev == 0xc0;
    return prev_lower ? 0x45 : 0x08;
  }
  if (c == 0xc0) return 0x42;                          // i
  if (c == 0xc1) return 0x6e;                          // !
  if (c >= 0xe6 && c <= 0xef) return c - 0xe6 + 0x64;  // 0-9
  return c;
}

static void LocConvertName(uint8 *name) {
  uint16 prev = 0xa9;
  for (int i = 0; i < 6; i++) {
    uint16 c = WORD(name[i * 2]);
    WORD(name[i * 2]) = LocConvertNameChar(c, prev);
    prev = c;
  }
}

void Localized_AfterSramLoad(void) {
  uint8 *sram = g_zenv.sram;
  if (!g_loc_rom || !memcmp(sram + kLocSramMarkOffs, kLocSramMark, 4))
    return;
  for (int slot = 0; slot < 3; slot++) {
    for (int copy = 0; copy < 2; copy++) {
      uint8 *s = sram + slot * 0x500 + copy * 0xf00;
      if (WORD(s[0x3e5]) != 0x55aa || !Intro_CheckCksum(s))
        continue;
      LocConvertName(s + kSrmOffs_Name);
      Intro_FixCksum(s);
    }
  }
  // The game being played keeps its own copy, written back when it saves.
  LocConvertName(&g_ram[0xf000 + kSrmOffs_Name]);
  memcpy(sram + kLocSramMarkOffs, kLocSramMark, 4);
  LocLog("Localized screens: player names brought over to the French Canadian keyboard");
}

// BG3 of a snapshot taken with the other layout. USA: characters at $7000,
// tilemap 64x64 at $6000 of which the left column is used (top $6000, bottom
// $6800) plus the top right on the file screens. French Canadian: characters
// at $6000, tilemap at $7800, 32x64 in a game and 64x32 otherwise. In a game
// the characters are the HUD lettering, which is drawn again from the sheets
// of the screens now in use, and the HUD drawn again with them: the moved
// ones are the other cartridge's.
static void LocMoveBg3(bool to_loc) {
  uint16 *v = g_zenv.vram;
  static uint16 tilemap[0x800];
  bool in_game = main_module_index >= 5 && main_module_index != 0x14;
  if (to_loc) {
    memcpy(tilemap, v + 0x6000, 0x400 * 2);
    memcpy(tilemap + 0x400, v + (in_game ? 0x6800 : 0x6400), 0x400 * 2);
    memmove(v + 0x6000, v + 0x7000, 0x1000 * 2);
    memcpy(v + 0x7800, tilemap, 0x800 * 2);
    BG3SC_copy = in_game ? 0x7a : 0x79;
    if (in_game) {
      LoadHudGraphics();
      Hud_Rebuild();
    }
    if (word_7E0219 == 0x6040)
      word_7E0219 = 0x7840;
    if (text_msgbox_topleft >= 0x6000 && text_msgbox_topleft < 0x6400)
      text_msgbox_topleft += 0x1800;
    if (text_msgbox_topleft_copy >= 0x6000 && text_msgbox_topleft_copy < 0x6400)
      text_msgbox_topleft_copy += 0x1800;
  } else {
    memcpy(tilemap, v + 0x7800, 0x800 * 2);
    memmove(v + 0x7000, v + 0x6000, 0x1000 * 2);
    memcpy(v + 0x6000, tilemap, 0x400 * 2);
    memcpy(v + ((BG3SC_copy & 2) ? 0x6800 : 0x6400), tilemap + 0x400, 0x400 * 2);
    BG3SC_copy = 0;
    zelda_ppu_write(BG3SC, 0x63);
    if (in_game) {
      LoadHudGraphics();
      Hud_Rebuild();
    }
    if (word_7E0219 == 0x7840)
      word_7E0219 = 0x6040;
    if (text_msgbox_topleft >= 0x7800 && text_msgbox_topleft < 0x7c00)
      text_msgbox_topleft -= 0x1800;
    if (text_msgbox_topleft_copy >= 0x7800 && text_msgbox_topleft_copy < 0x7c00)
      text_msgbox_topleft_copy -= 0x1800;
  }
}

void Localized_AfterStateLoad(void) {
  // $9F is only ever written with the French Canadian screens in use, from
  // the first frame of the intro on.
  if (g_loc_rom && BG3SC_copy == 0) {
    LocMoveBg3(true);
    LocLog("Localized screens: snapshot from the USA screens, BG3 moved");
  } else if (!g_loc_rom && BG3SC_copy != 0) {
    LocMoveBg3(false);
  }
  Localized_AfterSramLoad();
}
