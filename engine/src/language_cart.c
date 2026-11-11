#include "language_cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "mods.h"
#include "load_gfx.h"
#include "localized.h"
#include "select_file.h"
#include "variables.h"
#include "util.h"
#ifdef __3DS__
#include "platform_3ds.h"
#endif

enum {
  kCartSize = 0x100000,
  // Graphics sheet pointers, three columns of 223 bytes (bank, high, low):
  // kBgGfx sheets are entries 0..114, kSprGfx sheets 115..222.
  kGfxEntries = 223,
  kBgSheets = 115,
  kSprSheets = 108,
  kRawSprSheets = 12,  // stored uncompressed, 0x600 bytes each
  kRawSheetSize = 0x600,
  kAssetSprGfx = 64,
  kAssetBgGfx = 65,
};

// Tables of the C code that hold the words of the menus. A translation
// rewrites them in place with its own letters.
static const uint32 kLanguageTables[] = {
  0x0df1c9,  // kHudItemText
  0x0df449,  // kHudBottlesItemText
  0x0df549,  // kHudMushroomItemText
  0x0df569,  // kHudFluteItemText
  0x0df5a9,  // kHudMirrorItemText
  0x0df5c9,  // kHudBowItemText
  0x08f4ee,  // kGameOverText_Tab1
  0x08f565,  // kGameOverText_Tab2
  0x08f5b4,  // kGameOverText_Chars
};

// Where a cartridge keeps what the game draws from it. Translations of the
// USA cartridge keep its layout. The official German and French releases
// were rebuilt on another: their sheet pointers are at $00:CF00, the menus'
// tables of bank $0D sit 0xD8 bytes lower, and only their lettering sheets
// (HUD and menus, dungeon map floors) fit the USA screens; their file
// screens are drawn on a layout of their own and stay the USA ones.
typedef struct CartLayout {
  const char *language;  // NULL: any translation in the USA layout
  uint32 gfx_table;      // file offset of the sheet pointers
  int hud_shift;         // menus' tables of bank $0D, from the USA address
  bool usa_layout;       // file screens and their font follow the USA ones
  const uint8 *bg_sheets, *spr_sheets;  // sheets it gives (NULL: any it redrew)
  uint8 bg_count, spr_count;
  // The German release also copies 96 characters of its menu lettering from
  // $0E:FA00 to BG3 characters $200-$25F, past the USA ones. They go to $180
  // while the menu is open, where message text is drawn otherwise.
  uint32 menu_chars;
  // The Japanese cartridge moved some menu tables by their own amounts.
  const struct CartShift { uint32 lo, hi; int shift; } *shifts;
  uint8 shift_count;
} CartLayout;
static const uint8 kPalBgSheets[] = { 113, 114 };
static const uint8 kPalSprSheets[] = { 97, 105, 106, 107 };
static const struct CartShift kJapaneseShifts[] = {
  { 0x8df919, 0x8df9f9, 0x20 },  // gloves and abilities
  { 0x8dfe5f, 0x8dfec7, 0x20 },  // the HUD's top row
  { 0x8df5c9, 0x8df601, 0x20 },  // the bow's names
};
static const CartLayout kCartLayouts[] = {
  { "ja", 0x4fc0, 0, false, NULL, NULL, 0, 0, 0, kJapaneseShifts, 3 },
  { "fr", 0x4f00, -0xd8, false, kPalBgSheets, kPalSprSheets, 2, 4, 0 },
  { "de", 0x4f00, -0xd8, false, kPalBgSheets, kPalSprSheets, 2, 4, 0x8efa00 },
  { NULL, 0x4f80, 0, true, NULL, NULL, 0, 0, 0 },
};

enum {
  kMenuChars = 96,
  kMenuCharsFrom = 0x200,  // where the cartridge puts them
  kMenuCharsTo = 0x180,    // where they go while the menu is open
};

// The menus' tables of bank $0D (USA addresses, words), whose words a
// cartridge with its own menu characters renumbers.
static const struct { uint32 addr; uint16 words; } kMenuTables[] = {
  { 0x8df1c9, 320 }, { 0x8df449, 128 }, { 0x8df549, 16 }, { 0x8df569, 32 },
  { 0x8df5a9, 16 }, { 0x8df5c9, 48 }, { 0x8df959, 80 }, { 0x8df931, 20 },
  { 0x8de860, 90 }, { 0x8de914, 90 }, { 0x8ded09, 16 },
};

static uint8 *g_cart;
static const CartLayout *g_layout;

static bool SheetListed(const uint8 *list, int count, int sheet) {
  if (!list)
    return true;
  for (int i = 0; i < count; i++)
    if (list[i] == sheet)
      return true;
  return false;
}

static void PutLE32(uint8 *p, uint32 v) {
  p[0] = (uint8)v, p[1] = (uint8)(v >> 8), p[2] = (uint8)(v >> 16), p[3] = (uint8)(v >> 24);
}

static size_t CartOffset(uint32 snes_addr) {
  return (size_t)((snes_addr >> 16) & 0x7f) << 15 | (snes_addr & 0x7fff);
}

// Bytes of the LZ stream at src that decompress into one sheet, or 0 when it
// does not end inside the buffer.
static size_t CompressedLength(const uint8 *src, size_t avail) {
  size_t i = 0;
  while (i < avail) {
    uint8 cmd = src[i++];
    if (cmd == 0xff)
      return i;
    size_t len;
    if ((cmd & 0xe0) != 0xe0) {
      len = (cmd & 0x1f) + 1;
      cmd &= 0xe0;
    } else {
      if (i >= avail)
        return 0;
      len = src[i++] + ((cmd & 3) << 8) + 1;
      cmd = (cmd << 3) & 0xe0;
    }
    if (cmd == 0)
      i += len;          // literal
    else if (cmd & 0x80)
      i += 2;            // copy
    else if (!(cmd & 0x40))
      i += 1;            // byte fill
    else if (!(cmd & 0x20))
      i += 2;            // word fill
    else
      i += 1;            // increasing fill
  }
  return 0;
}

static bool SheetsDiffer(const uint8 *a, size_t a_size, const uint8 *b, size_t b_size,
                         bool raw) {
  if (raw)
    return a_size != b_size || memcmp(a, b, a_size) != 0;
  static uint8 da[0x4000], db[0x4000];
  int na = Mods_DecompressSheet(da, sizeof(da), a, a_size);
  int nb = Mods_DecompressSheet(db, sizeof(db), b, b_size);
  return na < 0 || nb < 0 || na != nb || memcmp(da, db, na) != 0;
}

static void Release(void) {
  free(g_cart);
  g_cart = NULL;
  g_layout = NULL;
}

static bool Load(const char *language) {
  Release();
  // The cartridges with screens of their own are drawn by localized.c.
  if (!language || !*language || !strcmp(language, "en") || Localized_HasCart(language))
    return false;
#ifdef __3DS__
  char path[512];
  Platform3DS_FormatProfilePath(LANGUAGE_CART_FILE, path, sizeof(path));
#else
  const char *path = LANGUAGE_CART_FILE;
#endif
  size_t size = 0;
  uint8 *data = ReadWholeFile(path, &size);
  if (!data)
    return false;
  if (size != kCartSize) {
    free(data);
    return false;
  }
  g_cart = data;
  g_layout = &kCartLayouts[countof(kCartLayouts) - 1];
  for (size_t i = 0; i + 1 < countof(kCartLayouts); i++)
    if (!strcmp(language, kCartLayouts[i].language))
      g_layout = &kCartLayouts[i];
  if (g_layout->menu_chars) {
    for (size_t t = 0; t < countof(kMenuTables); t++) {
      uint8 *p = g_cart + CartOffset(kMenuTables[t].addr + g_layout->hud_shift);
      for (int i = 0; i < kMenuTables[t].words; i++, p += 2) {
        unsigned tile = (p[0] | p[1] << 8) & 0x3ff;
        if (tile >= kMenuCharsFrom && tile < kMenuCharsFrom + kMenuChars) {
          unsigned w = (p[0] | p[1] << 8) - kMenuCharsFrom + kMenuCharsTo;
          p[0] = (uint8)w, p[1] = (uint8)(w >> 8);
        }
      }
    }
  }
  return true;
}

int LanguageCart_ApplyToAssets(const char *language) {
  if (!Load(language))
    return 0;
  // One mod file in memory: a kind 1 entry for each sheet the translation
  // redrew, carrying its own compressed bytes.
  enum { kMaxEntries = kBgSheets + kSprSheets };
  uint32 index[kMaxEntries], element[kMaxEntries], offset[kMaxEntries], length[kMaxEntries];
  int count = 0;
  for (int e = 0; e < kGfxEntries; e++) {
    bool spr = e >= kBgSheets;
    int sheet = spr ? e - kBgSheets : e;
    if (!(spr ? SheetListed(g_layout->spr_sheets, g_layout->spr_count, sheet)
              : SheetListed(g_layout->bg_sheets, g_layout->bg_count, sheet)))
      continue;
    const uint8 *table = g_cart + g_layout->gfx_table;
    uint32 addr = table[e] << 16 | table[kGfxEntries + e] << 8 | table[2 * kGfxEntries + e];
    size_t start = CartOffset(addr);
    if (start >= kCartSize)
      continue;
    bool raw = spr && sheet < kRawSprSheets;
    size_t n = raw ? kRawSheetSize : CompressedLength(g_cart + start, kCartSize - start);
    if (!n || start + n > kCartSize)
      continue;
    int asset = spr ? kAssetSprGfx : kAssetBgGfx;
    MemBlk own = FindIndexInMemblk((MemBlk){ g_asset_ptrs[asset], g_asset_sizes[asset] }, sheet);
    if (!own.ptr || !SheetsDiffer(own.ptr, own.size, g_cart + start, n, raw))
      continue;
    index[count] = asset, element[count] = sheet;
    offset[count] = (uint32)start, length[count] = (uint32)n;
    count++;
  }
  if (!count)
    return 0;
  size_t total = 64 + (size_t)count * 16;
  for (int i = 0; i < count; i++)
    total = ((total + 3) & ~(size_t)3) + length[i];
  uint8 *file = calloc(1, total);
  if (!file)
    return 0;
  static const uint8 kMagic[8] = { 'Z', '3', 'M', 'O', 'D', '\r', '\n', 0x1a };
  static const uint8 kSig[48] = { kAssets_Sig };
  memcpy(file, kMagic, 8);
  PutLE32(file + 8, 2);
  PutLE32(file + 12, count);
  memcpy(file + 16, kSig, 48);
  size_t data = 64 + (size_t)count * 16;
  for (int i = 0; i < count; i++) {
    uint8 *entry = file + 64 + i * 16;
    PutLE32(entry, index[i]);
    PutLE32(entry + 4, element[i]);
    PutLE32(entry + 8, 1);
    PutLE32(entry + 12, length[i]);
    data = (data + 3) & ~(size_t)3;
    memcpy(file + data, g_cart + offset[i], length[i]);
    data += length[i];
  }
  bool ok = Mods_ApplyAssetOverlay(file, total);
  free(file);
  return ok ? count : 0;
}

const uint8 *LanguageCart_Table(uint32 addr, size_t size) {
  if (!g_cart)
    return NULL;
  addr &= 0x7fffff;
  for (size_t i = 0; i < sizeof(kLanguageTables) / sizeof(kLanguageTables[0]); i++) {
    if (kLanguageTables[i] == addr) {
      // Only the menus' tables are at known places in the other layouts.
      if (addr >> 16 != 0x0d && !g_layout->usa_layout)
        return NULL;
      uint32 at = addr + g_layout->hud_shift;
      for (int k = 0; k < g_layout->shift_count; k++)
        if ((addr | 0x800000) >= g_layout->shifts[k].lo && (addr | 0x800000) < g_layout->shifts[k].hi)
          at += g_layout->shifts[k].shift;
      size_t o = CartOffset(at);
      return o + size <= kCartSize ? g_cart + o : NULL;
    }
  }
  return NULL;
}

bool LanguageCart_Active(void) {
  return g_cart != NULL;
}

const uint16 *LanguageCart_Words(uint16 *dst, uint32 snes_addr, int count) {
  if (((snes_addr >> 16) & 0x7f) == 0x0d) {
    uint32 a = snes_addr | 0x800000;
    snes_addr += g_layout->hud_shift;
    for (int i = 0; i < g_layout->shift_count; i++)
      if (a >= g_layout->shifts[i].lo && a < g_layout->shifts[i].hi)
        snes_addr += g_layout->shifts[i].shift;
  }
  size_t o = CartOffset(snes_addr);
  for (int i = 0; i < count; i++, o += 2)
    dst[i] = o + 1 < kCartSize ? g_cart[o] | g_cart[o + 1] << 8 : 0;
  return dst;
}

bool LanguageCart_UsaLayout(void) {
  return g_cart && g_layout->usa_layout;
}

bool LanguageCart_Japanese(void) {
  return g_cart && g_layout->shift_count != 0;
}

// A name's words in this SRAM are tiles of the font of the language it was
// typed in, and the Japanese font's are others. The mark at $701FF4 says which.
static uint16 UsaNameToJapanese(uint16 c) {
  static const uint8 kLetters[26] = { 0, 1, 2, 3, 4, 5, 6, 7, 0xaf, 9, 10, 11, 12, 13, 14, 15,
                                      0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29 };
  for (int i = 0; i < 26; i++) {
    uint8 lower = 26 + i;  // the keyboard's code of the small letter, as a tile
    if (c == kLetters[i] || c == (uint16)((lower & 0x70) * 2 + (lower & 0xf))) {
      uint8 g = 0xaa + i;
      return (g & 0xf) | (g & 0xf0) << 1;
    }
  }
  if (c >= 0xe6 && c <= 0xef) {
    uint8 g = 0xa0 + c - 0xe6;
    return (g & 0xf) | (g & 0xf0) << 1;
  }
  return 0x18c;
}

static uint16 JapaneseNameToUsa(uint16 c) {
  uint8 g = (c & 0xf) | (c >> 1 & 0xf0);
  static const uint8 kLetters[26] = { 0, 1, 2, 3, 4, 5, 6, 7, 0xaf, 9, 10, 11, 12, 13, 14, 15,
                                      0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29 };
  if (g >= 0xaa && g < 0xaa + 26)
    return kLetters[g - 0xaa];
  if (g >= 0xa0 && g <= 0xa9)
    return 0xe6 + g - 0xa0;
  return 0xa9;
}

// Kana have no letters in the other fonts: such a name becomes LINK, not nothing.
static void ConvertName(uint8 *name, bool to_japanese) {
  static const uint16 kLink[4] = { 11, 0xaf, 13, 10 };
  uint16 *w = (uint16 *)name;
  bool any = false, was_blank = true;
  for (int i = 0; i < 6; i++) {
    was_blank &= w[i] == (to_japanese ? 0xa9 : 0x18c);
    w[i] = to_japanese ? UsaNameToJapanese(w[i]) : JapaneseNameToUsa(w[i]);
    any |= w[i] != (to_japanese ? 0x18c : 0xa9);
  }
  if (!any && !was_blank && !to_japanese)
    memcpy(w, kLink, sizeof(kLink));
}

void LanguageCart_AfterSramLoad(uint8 *sram, uint8 *wram_save) {
  static const uint8 kMark[4] = { 'J', 'P', 'N', 1 };
  enum { kMarkOffs = 0x1ff4 };
  bool now = LanguageCart_Japanese();
  bool was = !memcmp(sram + kMarkOffs, kMark, 4);
  if (now == was)
    return;
  for (int slot = 0; slot < 3; slot++) {
    for (int copy = 0; copy < 2; copy++) {
      uint8 *s = sram + slot * 0x500 + copy * 0xf00;
      if (WORD(s[0x3e5]) != 0x55aa || !Intro_CheckCksum(s))
        continue;
      ConvertName(s + kSrmOffs_Name, now);
      Intro_FixCksum(s);
    }
  }
  ConvertName(wram_save + kSrmOffs_Name, now);
  if (now)
    memcpy(sram + kMarkOffs, kMark, 4);
  else
    memset(sram + kMarkOffs, 0, 4);
}

const uint8 *LanguageCart_RawBytes(uint32 snes_addr, size_t size) {
  if (!g_cart)
    return NULL;
  size_t o = CartOffset(snes_addr);
  return o + size <= kCartSize ? g_cart + o : NULL;
}

const uint8 *LanguageCart_Bytes(uint32 snes_addr, size_t size) {
  if (!LanguageCart_UsaLayout())
    return NULL;
  size_t o = CartOffset(snes_addr);
  return o + size <= kCartSize ? g_cart + o : NULL;
}

void LanguageCart_UploadMenuChars(uint16 *vram) {
  if (!g_cart || !g_layout->menu_chars)
    return;
  const uint8 *src = g_cart + CartOffset(g_layout->menu_chars);
  uint16 *dst = vram + kMenuCharsTo * 8;
  for (int i = 0; i < kMenuChars * 8; i++)
    dst[i] = src[i * 2] | src[i * 2 + 1] << 8;
}
