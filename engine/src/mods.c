#include "mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "util.h"
#include "language_cart.h"

enum {
  kAssetLinkGfx = 57,
  kAssetSprGfx = 64,
  kAssetBgGfx = 65,
  kMaxSheets = 256,
  kMaxSheetBytes = 0x4000,
};

static const uint8 kModMagic[8] = { 'Z', '3', 'M', 'O', 'D', '\r', '\n', 0x1a };

// Buffers the mods made; the previous one of an asset goes when it changes.
static uint8 *g_mod_owned[kNumberOfAssets];
// Sheets of kSprGfx and kBgGfx a mod replaced whole (Mods_SheetFromMod).
static uint8 g_mod_sheets[2][kMaxSheets / 8];
// Tables of the C code the mods replaced (kind 3), NULL for the game's.
static uint8 *g_mod_tables[kModTable_Count];
static uint32 g_mod_table_sizes[kModTable_Count];
// The exact size of each table, 0 for any multiple of 4 bytes up to 1 MiB.
static const uint32 kModTableSizes[kModTable_Count] = {
  [kModTable_BirdTravel] = 32,
  [kModTable_RomBytes] = 0,
  [kModTable_LocRomBytes] = 0,
  [kModTable_LinkPalette] = 124,
};
// The tables of the C code made from kModTable_RomBytes (Mods_RomTable).
typedef struct ModRomTable {
  uint32 addr;
  const void *def;
  void *made;  // NULL: the bytes change nothing in it, def is used
} ModRomTable;
static ModRomTable g_mod_rom_tables[128];
static int g_mod_rom_tables_count;

typedef struct ModAsset {
  const uint8 *ptr;
  uint32 size;
  uint8 *owned;  // made by the file being applied
  bool touched;
} ModAsset;

static uint32 ModU16(const uint8 *p) {
  return p[0] | p[1] << 8;
}

static uint32 ModU32(const uint8 *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (uint32)p[3] << 24;
}

static void ModPut(uint8 *p, uint32 v, int bytes) {
  for (int i = 0; i < bytes; i++)
    p[i] = (uint8)(v >> (8 * i));
}

// Element count of a packed asset (the layout FindIndexInMemblk reads), or
// -1 when its offset table does not hold together.
static int PackedCount(MemBlk b) {
  if (b.size < 2)
    return -1;
  size_t end = b.size - 2, w = 2;
  uint32 mx = ModU16(b.ptr + end);
  if (mx >= 8192)
    mx -= 8192, w = 4;
  if ((uint64)mx * w > end)
    return -1;
  uint64 prev = 0;
  for (uint32 i = 0; i < mx; i++) {
    uint64 o = w == 2 ? ModU16(b.ptr + i * 2) : ModU32(b.ptr + i * 4);
    if (o < prev || mx * w + o > end)
      return -1;
    prev = o;
  }
  return (int)mx + 1;
}

static MemBlk PackedElement(MemBlk b, int i) {
  return FindIndexInMemblk(b, i);
}

// A copy of packed asset `b` with element `index` replaced by `data`, laid
// out the way zelda3's restool packs arrays.
static uint8 *PackedReplace(MemBlk b, int count, int index, const uint8 *data,
                            uint32 size, uint32 *out_size) {
  uint64 total = 0, last = 0;
  for (int i = 0; i < count; i++) {
    uint64 n = i == index ? size : PackedElement(b, i).size;
    total += n;
    last = n;
  }
  bool wide = total - last >= 65536 || count > 8192;
  size_t w = wide ? 4 : 2;
  uint64 n = (uint64)(count - 1) * w + total + 2;
  if (n > 0x7fffffff)
    return NULL;
  uint8 *out = malloc((size_t)n);
  if (!out)
    return NULL;
  uint8 *p = out + (count - 1) * w;
  uint32 off = 0;
  for (int i = 0; i < count; i++) {
    MemBlk e = i == index ? (MemBlk) { data, size } : PackedElement(b, i);
    if (i > 0)
      ModPut(out + (i - 1) * w, off, (int)w);
    if (e.size)
      memcpy(p, e.ptr, e.size);
    p += e.size;
    off += (uint32)e.size;
  }
  ModPut(p, (uint32)(count - 1 + (wide ? 8192 : 0)), 2);
  *out_size = (uint32)n;
  return out;
}

// load_gfx.c's Decompress, kept inside both buffers: -1 past either end.
static int ModDecompress(uint8 *dst, size_t cap, const uint8 *src,
                         size_t src_size) {
  size_t o = 0, i = 0;
  for (;;) {
    if (i >= src_size)
      return -1;
    uint8 cmd = src[i++];
    if (cmd == 0xff)
      return (int)o;
    size_t len;
    if ((cmd & 0xe0) != 0xe0) {
      len = (cmd & 0x1f) + 1;
      cmd &= 0xe0;
    } else {
      if (i >= src_size)
        return -1;
      len = src[i++] + ((cmd & 3) << 8) + 1;
      cmd = (cmd << 3) & 0xe0;
    }
    if (o + len > cap)
      return -1;
    if (cmd == 0) {
      if (i + len > src_size)
        return -1;
      memcpy(dst + o, src + i, len);
      i += len, o += len;
    } else if (cmd & 0x80) {
      if (i + 2 > src_size)
        return -1;
      size_t offs = src[i] | src[i + 1] << 8;
      i += 2;
      while (len--) {
        if (offs >= o)
          return -1;
        dst[o++] = dst[offs++];
      }
    } else if (!(cmd & 0x40)) {
      if (i + 1 > src_size)
        return -1;
      memset(dst + o, src[i++], len);
      o += len;
    } else if (!(cmd & 0x20)) {
      if (i + 2 > src_size)
        return -1;
      uint8 lo = src[i], hi = src[i + 1];
      i += 2;
      for (size_t k = 0; k < len; k++)
        dst[o++] = k & 1 ? hi : lo;
    } else {
      if (i + 1 > src_size)
        return -1;
      uint8 v = src[i++];
      while (len--)
        dst[o++] = v++;
    }
  }
}

int Mods_DecompressSheet(uint8 *dst, size_t cap, const uint8 *src, size_t src_size) {
  return ModDecompress(dst, cap, src, src_size);
}

// Sheet `index` of kSprGfx is stored as is when Decomp_spr takes it so.
static bool SheetIsRaw(int asset, int index, size_t size) {
  return asset == kAssetSprGfx && index < 103 && size == 0x600;
}

// The sheet with the tiles of a kind 2 entry, stored the way the game reads
// it back: as is, or with the LZ format's copy command only.
static uint8 *PatchSheet(int asset, MemBlk sheet, int index, const uint8 *d,
                         uint32 size, uint32 *out_size) {
  static uint8 buf[kMaxSheetBytes];
  if (size < 4)
    return NULL;
  uint32 tile = ModU16(d), count = ModU16(d + 2);
  uint32 head = (4 + 2 * count + 3) & ~3u;
  if (tile < 8 || tile > 64 || count == 0 || (uint64)head + (uint64)count * tile != size)
    return NULL;
  bool raw = SheetIsRaw(asset, index, sheet.size);
  int len = raw ? (int)sheet.size : ModDecompress(buf, sizeof(buf), sheet.ptr, sheet.size);
  if (len <= 0)
    return NULL;
  if (raw)
    memcpy(buf, sheet.ptr, len);
  for (uint32 k = 0; k < count; k++) {
    uint32 t = ModU16(d + 4 + 2 * k);
    if ((uint64)(t + 1) * tile > (uint64)len)
      return NULL;
    memcpy(buf + t * tile, d + head + k * tile, tile);
  }
  uint32 n = raw ? (uint32)len : (uint32)len + 2 * ((len + 1023) / 1024) + 1;
  uint8 *out = malloc(n);
  if (!out)
    return NULL;
  if (raw) {
    memcpy(out, buf, len);
  } else {
    uint8 *p = out;
    for (int j = 0; j < len; j += 1024) {
      int m = len - j < 1024 ? len - j : 1024;
      *p++ = (uint8)(0xe0 | (m - 1) >> 8);
      *p++ = (uint8)(m - 1);
      memcpy(p, buf + j, m);
      p += m;
    }
    *p = 0xff;
  }
  *out_size = n;
  return out;
}

static bool ModTableSizeOk(uint32 id, uint32 length) {
  return kModTableSizes[id] ? length == kModTableSizes[id]
                            : length % 4 == 0 && length <= 0x100000;
}

static int CompareRomBytes(const void *a, const void *b) {
  uint32 x = ModU32(a) & 0x7fffff, y = ModU32(b) & 0x7fffff;
  return x < y ? -1 : x > y;
}

// In address order, for RomBytesFind.
static void SortRomBytes(uint8 *t, uint32 length) {
  qsort(t, length / 4, 4, CompareRomBytes);
}

// A mod's cartridge bytes join those of the mods before it, its own winning
// at the same address: a GAME mod and an OTHER one can both change the code.
static uint8 *MergeRomBytes(uint8 *old, uint32 old_size, uint8 *t,
                            uint32 *size) {
  uint8 *m = malloc(old_size + *size);
  if (!m)
    return NULL;
  memcpy(m, t, *size);
  uint32 n = *size;
  for (uint32 i = 0; i < old_size; i += 4)
    if (!bsearch(old + i, t, *size / 4, 4, CompareRomBytes))
      memcpy(m + n, old + i, 4), n += 4;
  SortRomBytes(m, n);
  *size = n;
  return m;
}

static void ForgetRomTables(void) {
  for (int i = 0; i < g_mod_rom_tables_count; i++)
    free(g_mod_rom_tables[i].made);
  g_mod_rom_tables_count = 0;
}

bool Mods_ZsprLayout(const uint8 *head, size_t head_size, size_t file_size,
                     uint32 *pixels, uint32 *palette) {
  if (head_size < kZsprHeaderSize || memcmp(head, "ZSPR\x01", 5) != 0)
    return false;
  uint64 p = ModU32(head + 9), c = ModU32(head + 15);
  if (ModU16(head + 13) != kZsprPixelBytes || ModU16(head + 19) != kZsprPaletteBytes ||
      ModU16(head + 21) != 1 || p < kZsprHeaderSize ||
      p + kZsprPixelBytes > c || c + kZsprPaletteBytes > file_size)
    return false;
  *pixels = (uint32)p;
  *palette = (uint32)c;
  return true;
}

// A ZSPR is the two entries of a skin package, so it applies the same way.
static bool ApplyZspr(const uint8 *file, size_t size) {
  static const uint8 kAssetsSig[48] = { kAssets_Sig };
  uint32 pixels, palette;
  if (!Mods_ZsprLayout(file, size, size, &pixels, &palette))
    return false;
  enum { kHead = 64 + 2 * 16 };
  uint8 *mod = malloc(kHead + kZsprPixelBytes + kZsprPaletteBytes);
  if (!mod)
    return false;
  const uint32 head[] = { 2, 2 };
  const uint32 entries[] = { kAssetLinkGfx, 0, 0, kZsprPixelBytes,
                             kModTable_LinkPalette, 0, 3, kZsprPaletteBytes };
  memcpy(mod, kModMagic, 8);
  for (int i = 0; i < 2; i++)
    ModPut(mod + 8 + i * 4, head[i], 4);
  memcpy(mod + 16, kAssetsSig, 48);
  for (int i = 0; i < 8; i++)
    ModPut(mod + 64 + i * 4, entries[i], 4);
  memcpy(mod + kHead, file + pixels, kZsprPixelBytes);
  memcpy(mod + kHead + kZsprPixelBytes, file + palette, kZsprPaletteBytes);
  bool ok = Mods_ApplyAssetOverlay(mod, kHead + kZsprPixelBytes + kZsprPaletteBytes);
  free(mod);
  return ok;
}

bool Mods_ApplyAssetOverlay(const uint8 *file, size_t size) {
  static const uint8 kAssetsSig[48] = { kAssets_Sig };
  if (file && size >= 4 && memcmp(file, "ZSPR", 4) == 0)
    return ApplyZspr(file, size);
  if (!file || size < 64 || memcmp(file, kModMagic, 8) != 0 ||
      memcmp(file + 16, kAssetsSig, 48) != 0)
    return false;
  uint32 version = ModU32(file + 8), n = ModU32(file + 12);
  uint32 entry_size = version == 1 ? 8 : version == 2 ? 16 : 0;
  if (!entry_size || n == 0 || (size - 64) / entry_size < n)
    return false;
  ModAsset st[kNumberOfAssets];
  memset(st, 0, sizeof(st));
  uint8 *tables[kModTable_Count] = { 0 };
  uint32 table_sizes[kModTable_Count] = { 0 };
  uint8 sheets[2][kMaxSheets / 8];
  memset(sheets, 0, sizeof(sheets));
  uint64 offset = 64 + (uint64)n * entry_size;
  bool ok = true;
  for (uint32 i = 0; i < n && ok; i++) {
    const uint8 *e = file + 64 + i * entry_size;
    uint32 index = ModU32(e), element = 0, kind = 0, length;
    if (version == 1) {
      length = ModU32(e + 4);
    } else {
      element = ModU32(e + 4), kind = ModU32(e + 8), length = ModU32(e + 12);
    }
    offset = (offset + 3) & ~(uint64)3;
    if (offset + length > size || kind > 3 ||
        (kind == 3 ? index >= kModTable_Count || !ModTableSizeOk(index, length)
                   : index >= kNumberOfAssets)) {
      ok = false;
      break;
    }
    const uint8 *data = file + offset;
    offset += length;
    if (kind == 3) {
      uint8 *t = malloc(length ? length : 1);
      if (!t) {
        ok = false;
        break;
      }
      memcpy(t, data, length);
      if (index == kModTable_RomBytes || index == kModTable_LocRomBytes)
        SortRomBytes(t, length);
      free(tables[index]);
      tables[index] = t;
      table_sizes[index] = length;
      continue;
    }
    ModAsset *a = &st[index];
    if (!a->touched) {
      a->ptr = g_asset_ptrs[index];
      a->size = g_asset_sizes[index];
      a->touched = true;
    }
    uint8 *made = NULL;
    uint32 made_size = 0;
    if (kind == 0) {
      made = malloc(length ? length : 1);
      if (made && length)
        memcpy(made, data, length);
      made_size = length;
    } else {
      MemBlk cur = { a->ptr, a->size };
      int count = PackedCount(cur);
      if (count < 0 || element >= (uint32)count ||
          (kind == 2 && index != kAssetSprGfx && index != kAssetBgGfx)) {
        ok = false;
        break;
      }
      const uint8 *elem = data;
      uint32 elem_size = length;
      uint8 *sheet = NULL;
      if (kind == 2) {
        sheet = PatchSheet(index, PackedElement(cur, element), element,
                           data, length, &elem_size);
        elem = sheet;
      }
      if (elem)
        made = PackedReplace(cur, count, element, elem, elem_size, &made_size);
      free(sheet);
      if (made && kind == 1 && (index == kAssetSprGfx || index == kAssetBgGfx) &&
          element < kMaxSheets)
        sheets[index - kAssetSprGfx][element >> 3] |= 1 << (element & 7);
    }
    if (!made) {
      ok = false;
      break;
    }
    free(a->owned);
    a->owned = made;
    a->ptr = made;
    a->size = made_size;
  }
  if (!ok) {
    for (int i = 0; i < kNumberOfAssets; i++)
      free(st[i].owned);
    for (int i = 0; i < kModTable_Count; i++)
      free(tables[i]);
    return false;
  }
  for (int i = kModTable_RomBytes; i <= kModTable_LocRomBytes; i++) {
    if (tables[i] && g_mod_tables[i]) {
      uint8 *m = MergeRomBytes(g_mod_tables[i], g_mod_table_sizes[i],
                               tables[i], &table_sizes[i]);
      if (!m) {
        // Nothing has been committed yet: reject the entire overlay and keep
        // every preceding mod intact when merging would exceed memory.
        for (int j = 0; j < kNumberOfAssets; j++) free(st[j].owned);
        for (int j = 0; j < kModTable_Count; j++) free(tables[j]);
        return false;
      }
      free(tables[i]);
      tables[i] = m;
    }
  }
  for (int i = 0; i < kModTable_Count; i++) {
    if (tables[i]) {
      free(g_mod_tables[i]);
      g_mod_tables[i] = tables[i];
      g_mod_table_sizes[i] = table_sizes[i];
      if (i == kModTable_RomBytes)
        ForgetRomTables();
    }
  }
  for (int i = 0; i < kNumberOfAssets; i++) {
    if (!st[i].touched)
      continue;
    free(g_mod_owned[i]);
    g_mod_owned[i] = st[i].owned;
    g_asset_ptrs[i] = st[i].ptr;
    g_asset_sizes[i] = st[i].size;
  }
  for (int k = 0; k < 2; k++)
    for (int j = 0; j < kMaxSheets / 8; j++)
      g_mod_sheets[k][j] |= sheets[k][j];
  return true;
}

const uint8 *Mods_Table(int id) {
  return id >= 0 && id < kModTable_Count ? g_mod_tables[id] : NULL;
}

uint32 Mods_TableSize(int id) {
  return id >= 0 && id < kModTable_Count && g_mod_tables[id] ? g_mod_table_sizes[id] : 0;
}

// The first entry of kModTable_RomBytes at or after addr (the count when
// none).
static uint32 RomBytesFind(uint32 addr) {
  const uint8 *t = g_mod_tables[kModTable_RomBytes];
  uint32 lo = 0, hi = t ? g_mod_table_sizes[kModTable_RomBytes] / 4 : 0;
  while (lo < hi) {
    uint32 mid = (lo + hi) / 2;
    if ((ModU32(t + mid * 4) & 0x7fffff) < addr)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

uint8 Mods_RomByte(uint32 addr, uint8 def) {
  const uint8 *t = g_mod_tables[kModTable_RomBytes];
  if (!t)
    return def;
  addr &= 0x7fffff;
  uint32 i = RomBytesFind(addr);
  if (i < g_mod_table_sizes[kModTable_RomBytes] / 4 &&
      (ModU32(t + i * 4) & 0x7fffff) == addr)
    return t[i * 4 + 3];
  return def;
}

void Mods_PatchLocRom(uint8 *rom, size_t size) {
  const uint8 *t = g_mod_tables[kModTable_LocRomBytes];
  for (uint32 i = 0; t && i < g_mod_table_sizes[kModTable_LocRomBytes]; i += 4) {
    uint32 a = ModU32(t + i);
    size_t o = (size_t)((a >> 16) & 0x7f) << 15 | (a & 0x7fff);
    if (o < size)
      rom[o] = t[i + 3];
  }
}

uint16 Mods_RomWord(uint32 addr, uint16 def) {
  return Mods_RomByte(addr, def & 0xff) | Mods_RomByte(addr + 1, def >> 8) << 8;
}

const void *Mods_RomTable(uint32 addr, const void *def, size_t size) {
  const uint8 *language = LanguageCart_Table(addr, size);
  if (!g_mod_tables[kModTable_RomBytes] && !language)
    return def;
  addr &= 0x7fffff;
  for (int i = 0; i < g_mod_rom_tables_count; i++) {
    ModRomTable *m = &g_mod_rom_tables[i];
    if (m->addr == addr && m->def == def)
      return m->made ? m->made : def;
  }
  const uint8 *t = g_mod_tables[kModTable_RomBytes];
  uint32 n = g_mod_table_sizes[kModTable_RomBytes] / 4;
  uint8 *made = NULL;
  if (language) {
    // The translation's words first; a mod's bytes still go over them.
    made = malloc(size);
    if (!made)
      return def;
    memcpy(made, language, size);
  }
  for (uint32 i = t ? RomBytesFind(addr) : n; i < n; i++) {
    uint32 a = ModU32(t + i * 4) & 0x7fffff;
    if (a >= addr + size)
      break;
    if (!made) {
      made = malloc(size);
      if (!made)
        return def;
      memcpy(made, def, size);
    }
    made[a - addr] = t[i * 4 + 3];
  }
  if (g_mod_rom_tables_count == (int)(sizeof(g_mod_rom_tables) / sizeof(g_mod_rom_tables[0]))) {
    fprintf(stderr, "Mods: too many tables of the C code\n");
    free(made);
    return def;
  }
  g_mod_rom_tables[g_mod_rom_tables_count++] = (ModRomTable){ addr, def, made };
  return made ? made : def;
}

bool Mods_SheetFromMod(int asset, int index) {
  if ((asset != kAssetSprGfx && asset != kAssetBgGfx) || index < 0 ||
      index >= kMaxSheets)
    return false;
  return (g_mod_sheets[asset - kAssetSprGfx][index >> 3] >> (index & 7)) & 1;
}

static const char *g_mods_language;

void Mods_SetLanguage(const char *language) {
  g_mods_language = language;
}

int Mods_ApplyAssetOverlays(const char *list) {
  // After a new assets file is loaded, last run's buffers are no longer the
  // assets: they go. One an asset still points at stays.
  for (int i = 0; i < kNumberOfAssets; i++) {
    if (g_mod_owned[i] && g_asset_ptrs[i] != g_mod_owned[i]) {
      free(g_mod_owned[i]);
      g_mod_owned[i] = NULL;
    }
  }
  memset(g_mod_sheets, 0, sizeof(g_mod_sheets));
  for (int i = 0; i < kModTable_Count; i++) {
    free(g_mod_tables[i]);
    g_mod_tables[i] = NULL;
    g_mod_table_sizes[i] = 0;
  }
  ForgetRomTables();
  // The translation's own lettering, under the player's mods.
  LanguageCart_ApplyToAssets(g_mods_language);
  int applied = 0;
  while (list && *list) {
    const char *end = strchr(list, ',');
    size_t n = end ? (size_t)(end - list) : strlen(list);
    while (n && (*list == ' ' || *list == '\t'))
      list++, n--;
    while (n && (list[n - 1] == ' ' || list[n - 1] == '\t' || list[n - 1] == '\r'))
      n--;
    char path[512];
    if (n && n < sizeof(path)) {
      memcpy(path, list, n);
      path[n] = 0;
      size_t size = 0;
      uint8 *file = ReadWholeFile(path, &size);
      if (file && Mods_ApplyAssetOverlay(file, size))
        applied++;
      else
        fprintf(stderr, "Mod skipped: %s\n", path);
      free(file);
    }
    list = end ? end + 1 : NULL;
  }
  return applied;
}
