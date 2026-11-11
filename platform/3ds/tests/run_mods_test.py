"""Host test for mods: the engine's loader (engine/src/mods.c) and the
3DS side that lists them and keeps each profile's choice (platform_3ds.c).

Mods loader: version 1 files replace whole assets; version 2 files also
replace one element of a packed asset, patch tiles of one sheet (stored as
is, or LZ, and read back the way the game reads it), replace a table of the
C code (Mods_Table), or give bytes of a quest's cartridge, which the C code
reads as single values or over its own tables (Mods_RomByte, Mods_RomWord,
Mods_RomTable: the game's own value where the quest gives none). A file
that does not fit this build (magic,
version, assets signature, asset index, kind, element, tile numbers, table,
sizes) changes nothing, even after entries that did fit. Only whole sheets
give way over the French Canadian cartridge's copy (Mods_SheetFromMod). A
list applies its files in order and starts from a clean slate.

3DS side, sliced out of platform_3ds.c: the ROM language (the French Canadian
cartridge, with or without a copier header), the mods found for it, their
categories and order (QUEST and GAME first, SKIN last), one mod on per
category and never a QUEST with a GAME mod (in the menu and in a mods.txt
edited by hand), the choice saved in the profile and turned into the list
the engine reads, in the order the categories apply, and a QUEST's own save
folder. Mods built into the RomFS are listed with those of the SD card, a
mod of the SD card replacing the built-in one of the same folder name for
the ROMs it has a file for, and the list the engine reads takes each mod
from where the menu found it.
"""

from pathlib import Path
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
ENGINE = ROOT / 'engine'
PLATFORM = ROOT / 'platform/3ds/source/platform_3ds.c'

LOADER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "assets.h"
#include "mods.h"
bool Intro_CheckCksum(const uint8 *s) { return false; }
void Intro_FixCksum(uint8 *s) {}
const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];
// This loader fixture has no official localized cartridge.
bool Localized_HasCart(const char *language) { (void)language; return false; }
uint8 *ReadWholeFile(const char *name, size_t *length) {
  FILE *f = fopen(name, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8 *d = malloc(n + 1);
  if (fread(d, 1, n, f) != n) { free(d); fclose(f); return NULL; }
  fclose(f); if (length) *length = n; return d;
}
@FIND@
typedef struct E { uint32 asset, element, kind, size; const uint8 *data; } E;
static uint8 g_file[1 << 16];
static size_t Build(uint32 version, const E *e, int n) {
  static const uint8 kSig[48] = { kAssets_Sig };
  memset(g_file, 0, sizeof(g_file));
  memcpy(g_file, "Z3MOD\r\n\x1a", 8);
  uint32 c = n; memcpy(g_file + 8, &version, 4); memcpy(g_file + 12, &c, 4);
  memcpy(g_file + 16, kSig, 48);
  size_t es = version == 1 ? 8 : 16, o = 64 + es * n;
  for (int i = 0; i < n; i++) {
    uint32 v[4] = { e[i].asset, e[i].element, e[i].kind, e[i].size };
    if (version == 1) { memcpy(g_file + 64 + 8 * i, &v[0], 4); memcpy(g_file + 68 + 8 * i, &v[3], 4); }
    else memcpy(g_file + 64 + 16 * i, v, 16);
  }
  for (int i = 0; i < n; i++) { o = (o + 3) & ~3; memcpy(g_file + o, e[i].data, e[i].size); o += e[i].size; }
  return o;
}
// Packed arrays as restool lays them out, 16-bit offsets.
static uint32 Pack(uint8 *out, const uint8 *const *el, const uint32 *sz, int n) {
  uint32 o = (n - 1) * 2, off = 0;
  for (int i = 0; i < n; i++) {
    if (i) { out[(i - 1) * 2] = (uint8)off; out[(i - 1) * 2 + 1] = (uint8)(off >> 8); }
    memcpy(out + o, el[i], sz[i]); o += sz[i]; off += sz[i];
  }
  out[o] = (uint8)(n - 1); out[o + 1] = 0;
  return o + 2;
}
// The game's LZ, the two commands used here (copy, byte fill), both forms.
static int Unlz(uint8 *dst, const uint8 *src) {
  int o = 0;
  for (;;) {
    uint8 cmd = *src++;
    if (cmd == 0xff) return o;
    int len, type;
    if ((cmd & 0xe0) == 0xe0) { type = (cmd >> 2) & 7; len = ((cmd & 3) << 8 | *src++) + 1; }
    else { type = cmd >> 5; len = (cmd & 0x1f) + 1; }
    if (type == 0) { memcpy(dst + o, src, len); src += len; }
    else if (type == 1) { memset(dst + o, *src++, len); }
    else return -1;
    o += len;
  }
}
static uint8 g_one[kNumberOfAssets], g_spr[8192], g_bg[8192];
static uint8 kRaw[0x600], kSheet[0x600], kLz[0x310];
static uint32 g_spr_size, g_bg_size, kLzSize;
static void Reset(void) {
  for (int i = 0; i < kNumberOfAssets; i++) { g_asset_ptrs[i] = &g_one[i]; g_asset_sizes[i] = 1; }
  g_asset_ptrs[64] = g_spr; g_asset_sizes[64] = g_spr_size;
  g_asset_ptrs[65] = g_bg; g_asset_sizes[65] = g_bg_size;
  Mods_ApplyAssetOverlays(NULL);  // what LoadAssets does after loading the file
}
static int Untouched(void) {
  for (int i = 0; i < kNumberOfAssets; i++) {
    const uint8 *p = i == 64 ? g_spr : i == 65 ? g_bg : &g_one[i];
    uint32 n = i == 64 ? g_spr_size : i == 65 ? g_bg_size : 1;
    if (g_asset_ptrs[i] != p || g_asset_sizes[i] != n) return 0;
  }
  return 1;
}
static MemBlk El(int asset, int i) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, i);
}
static int Count(int asset) {
  const uint8 *p = g_asset_ptrs[asset] + g_asset_sizes[asset] - 2;
  return (p[0] | p[1] << 8) + 1;
}
static int Same(MemBlk b, const uint8 *p, uint32 n) { return b.size == n && memcmp(b.ptr, p, n) == 0; }
// A kind 2 entry: tile size, count, numbers, padding, tiles (tile k filled with fill + k).
static uint32 Tiles(uint8 *out, int tile, const int *t, int n, uint8 fill) {
  out[0] = (uint8)tile; out[1] = 0; out[2] = (uint8)n; out[3] = 0;
  for (int k = 0; k < n; k++) { out[4 + 2 * k] = (uint8)t[k]; out[5 + 2 * k] = (uint8)(t[k] >> 8); }
  uint32 head = (4 + 2 * n + 3) & ~3u;
  memset(out + 4 + 2 * n, 0, head - 4 - 2 * n);
  for (int k = 0; k < n; k++) memset(out + head + k * tile, fill + k, tile);
  return head + n * tile;
}
static void Expect(uint8 *sheet, int tile, const int *t, int n, uint8 fill) {
  for (int k = 0; k < n; k++) memset(sheet + t[k] * tile, fill + k, tile);
}
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); return 1; } } while (0)
int main(int argc, char **argv) {
  (void)argc;
  for (int i = 0; i < 0x600; i++) { kRaw[i] = (uint8)(i * 7); kSheet[i] = i < 0x300 ? 0x11 : (uint8)i; }
  // kSheet in the game's LZ: 0x300 times 0x11, then 0x300 bytes copied.
  kLz[0] = 0xe4 | 0x2ff >> 8; kLz[1] = 0xff; kLz[2] = 0x11;
  kLz[3] = 0xe0 | 0x2ff >> 8; kLz[4] = 0xff; memcpy(kLz + 5, kSheet + 0x300, 0x300); kLz[0x305] = 0xff;
  kLzSize = 0x306;
  uint8 dec[0x800];
  if (Unlz(dec, kLz) != 0x600 || memcmp(dec, kSheet, 0x600)) { printf("FAIL: test LZ\n"); return 1; }
  const uint8 *spr[3] = { kRaw, kLz, (const uint8 *)"abc" };
  const uint32 spr_sz[3] = { 0x600, kLzSize, 3 };
  g_spr_size = Pack(g_spr, spr, spr_sz, 3);
  const uint8 *bg[2] = { kLz, kLz };
  const uint32 bg_sz[2] = { kLzSize, kLzSize };
  g_bg_size = Pack(g_bg, bg, bg_sz, 2);

  // Version 1: whole assets.
  uint8 a5[5], b7[7];
  memset(a5, 0xaa, 5); memset(b7, 0xbb, 7);
  E v1[2] = { { 57, 0, 0, 5, a5 }, { 94, 0, 0, 7, b7 } };
  Reset();
  size_t n = Build(1, v1, 2);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "version 1 refused");
  CHECK(g_asset_sizes[57] == 5 && g_asset_ptrs[57][4] == 0xaa && g_asset_sizes[94] == 7 &&
        g_asset_ptrs[94][6] == 0xbb && ((uintptr_t)g_asset_ptrs[94] & 3) == 0, "version 1: assets not replaced");
  CHECK(g_asset_ptrs[58] == &g_one[58], "version 1: other asset changed");
  memset(g_file, 0, sizeof(g_file));
  CHECK(g_asset_ptrs[57][0] == 0xaa, "the data is copied, the file can go");
  struct { size_t at; uint8 value; const char *what; } bad[] = {
    { 0, 'X', "magic" }, { 8, 3, "version" }, { 16, 0, "assets signature" },
    { 64, 200, "asset index" }, { 68, 0xff, "size past the end" },
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    Reset(); n = Build(1, v1, 2);
    g_file[bad[i].at] = bad[i].at == 16 ? (uint8)(g_file[16] ^ 1) : bad[i].value;
    if (bad[i].at == 68) g_file[69] = 0xff;
    CHECK(!Mods_ApplyAssetOverlay(g_file, n) && Untouched(), bad[i].what);
  }
  Reset(); n = Build(1, v1, 2);
  CHECK(!Mods_ApplyAssetOverlay(g_file, n - 1) && Untouched(), "truncated file");

  // Version 2, kind 0 and kind 1: a whole asset, one element of a packed one.
  E w[2] = { { 57, 0, 0, 5, a5 }, { 64, 2, 1, 4, (const uint8 *)"WXYZ" } };
  Reset(); n = Build(2, w, 2);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "kinds 0 and 1 refused");
  CHECK(g_asset_sizes[57] == 5 && g_asset_ptrs[57][0] == 0xaa, "kind 0: asset not replaced");
  CHECK(Count(64) == 3 && Same(El(64, 0), kRaw, 0x600) && Same(El(64, 1), kLz, kLzSize) &&
        Same(El(64, 2), (const uint8 *)"WXYZ", 4), "kind 1: element not replaced, or others changed");
  CHECK(Mods_SheetFromMod(64, 2) && !Mods_SheetFromMod(64, 0) && !Mods_SheetFromMod(65, 2),
        "kind 1: whole sheet marked, nothing else");

  // Kind 2: tiles of a sheet stored as is (kSprGfx 0) and of an LZ one (kBgGfx 1).
  uint8 p1[256], p2[256], sheet[0x600];
  const int t1[2] = { 1, 63 }, t2[3] = { 0, 40, 5 };
  E k2[2] = { { 64, 0, 2, Tiles(p1, 24, t1, 2, 0xa1), p1 }, { 65, 1, 2, Tiles(p2, 24, t2, 3, 0xc1), p2 } };
  Reset(); n = Build(2, k2, 2);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "kind 2 refused");
  memcpy(sheet, kRaw, 0x600); Expect(sheet, 24, t1, 2, 0xa1);
  CHECK(Same(El(64, 0), sheet, 0x600), "kind 2: sheet stored as is, tiles not patched");
  MemBlk e = El(65, 1);
  memcpy(sheet, kSheet, 0x600); Expect(sheet, 24, t2, 3, 0xc1);
  CHECK(e.size != 0x600 && Unlz(dec, e.ptr) == 0x600 && !memcmp(dec, sheet, 0x600),
        "kind 2: LZ sheet not patched");
  CHECK(Same(El(65, 0), kLz, kLzSize) && Same(El(64, 1), kLz, kLzSize), "kind 2: other sheets changed");
  CHECK(!Mods_SheetFromMod(64, 0) && !Mods_SheetFromMod(65, 1), "kind 2 leaves the cartridge's sheets alone");

  // In one file, each entry works on what the previous ones made; tiles of
  // 16 bytes up to the last one of the sheet.
  const int t3[2] = { 95, 2 };
  uint8 p3[256];
  E ch[2] = { { 65, 0, 1, kLzSize, kLz }, { 65, 0, 2, Tiles(p3, 16, t3, 2, 0x51), p3 } };
  Reset(); n = Build(2, ch, 2);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "chained entries refused");
  memcpy(sheet, kSheet, 0x600); Expect(sheet, 16, t3, 2, 0x51);
  e = El(65, 0);
  CHECK(Unlz(dec, e.ptr) == 0x600 && !memcmp(dec, sheet, 0x600) && Mods_SheetFromMod(65, 0),
        "chained entries: element then tiles");
  // A second file goes over the first.
  const int t4[1] = { 2 };
  E over[1] = { { 65, 0, 2, Tiles(p3, 16, t4, 1, 0x71), p3 } };
  n = Build(2, over, 1);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "second file refused");
  Expect(sheet, 16, t4, 1, 0x71);
  e = El(65, 0);
  CHECK(Unlz(dec, e.ptr) == 0x600 && !memcmp(dec, sheet, 0x600), "second file: tiles over the first file's");

  // Kind 3: a table of the C code, whole; NULL until a mod gives it.
  uint8 spots[32];
  for (int i = 0; i < 32; i++) spots[i] = (uint8)(0x40 + i);
  Reset();
  CHECK(Mods_Table(kModTable_BirdTravel) == NULL && Mods_Table(-1) == NULL && Mods_Table(kModTable_Count) == NULL,
        "kind 3: no table before a mod");
  E k3[2] = { { 57, 0, 0, 5, a5 }, { kModTable_BirdTravel, 0, 3, 32, spots } };
  n = Build(2, k3, 2);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "kind 3 refused");
  memset(g_file, 0, sizeof(g_file));
  CHECK(Mods_Table(kModTable_BirdTravel) && !memcmp(Mods_Table(kModTable_BirdTravel), spots, 32) &&
        g_asset_sizes[57] == 5, "kind 3: table not replaced, or copied wrong");

  // Complete skin colors are one 124-byte author payload, reset on unload.
  uint8 colors[124]; for (int i=0;i<124;i++) colors[i]=(uint8)(i*3);
  E skin = { kModTable_LinkPalette, 0, 3, sizeof(colors), colors };
  Reset(); n=Build(2,&skin,1);
  CHECK(Mods_ApplyAssetOverlay(g_file,n) && Mods_TableSize(kModTable_LinkPalette)==124 &&
        !memcmp(Mods_Table(kModTable_LinkPalette),colors,124), "skin palettes and gloves");
  Reset(); CHECK(!Mods_Table(kModTable_LinkPalette), "skin colors survive unload");

  // Kind 3, the cartridge's bytes: any number of 4-byte entries, in any
  // order and either bank form; without them the C code keeps its values.
  static const uint8 kTab4[4] = { 1, 2, 3, 4 };
  Reset();
  CHECK(Mods_RomByte(0x8db173, 0x42) == 0x42 && Mods_RomWord(0x82ef2a, 0x20f) == 0x20f &&
        Mods_RomTable(0x8db172, kTab4, 4) == kTab4 && Mods_TableSize(kModTable_RomBytes) == 0,
        "cartridge bytes: the C code's values before a mod");
  uint32 rb[4] = { 0x0db174 | 0x99u << 24, 0x8db173 | 0x77u << 24, 0x82ef2b | 0x12u << 24, 0x02ef2a | 0x34u << 24 };
  E k3b[1] = { { kModTable_RomBytes, 0, 3, sizeof(rb), (const uint8 *)rb } };
  n = Build(2, k3b, 1);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "cartridge bytes refused");
  memset(g_file, 0, sizeof(g_file));
  CHECK(Mods_TableSize(kModTable_RomBytes) == 16 && Mods_RomByte(0x0db173, 0) == 0x77 &&
        Mods_RomByte(0x8db174, 0) == 0x99 && Mods_RomByte(0x8db175, 0x5a) == 0x5a,
        "cartridge bytes: one byte, either bank form, or the C code's value");
  CHECK(Mods_RomWord(0x82ef2a, 0x20f) == 0x1234 && Mods_RomWord(0x82ef2b, 0xaaaa) == 0xaa12,
        "cartridge bytes: a word, each byte on its own");
  const uint8 *made = Mods_RomTable(0x8db172, kTab4, 4);
  CHECK(made != kTab4 && made[0] == 1 && made[1] == 0x77 && made[2] == 0x99 && made[3] == 4 && kTab4[1] == 2 &&
        Mods_RomTable(0x8db172, kTab4, 4) == made, "cartridge bytes: a table made once, the C code's own kept");
  CHECK(Mods_RomTable(0x8db180, kTab4, 4) == kTab4 && Mods_RomTable(0x8db16e, kTab4, 4) == kTab4,
        "cartridge bytes: a table they do not touch stays the C code's");
  uint32 rb2[1] = { 0x8db173 | 0x55u << 24 };
  E k3c[1] = { { kModTable_RomBytes, 0, 3, sizeof(rb2), (const uint8 *)rb2 } };
  n = Build(2, k3c, 1);
  CHECK(Mods_ApplyAssetOverlay(g_file, n), "second cartridge bytes refused");
  made = Mods_RomTable(0x8db172, kTab4, 4);
  CHECK(Mods_RomByte(0x8db174, 0x3c) == 0x99 && made[1] == 0x55 && made[2] == 0x99,
        "cartridge bytes: a later file's join the earlier ones, the later one winning at an address");
  Reset();
  CHECK(Mods_RomByte(0x8db173, 0x42) == 0x42 && Mods_RomTable(0x8db172, kTab4, 4) == kTab4 &&
        Mods_TableSize(kModTable_RomBytes) == 0, "cartridge bytes: gone with a new list");

  // Files that do not fit change nothing, even after entries that did.
  const int t_far[1] = { 64 }, t_one[1] = { 3 };
  uint8 pf[64], po[64];
  uint32 far_n = Tiles(pf, 24, t_far, 1, 1), one_n = Tiles(po, 24, t_one, 1, 1);
  struct { E e[2]; const char *what; } odd[] = {
    { { { 57, 0, 0, 5, a5 }, { 57, 0, 2, one_n, po } }, "kind 2 on an asset that is not a sheet" },
    { { { 64, 1, 1, 4, a5 }, { 64, 3, 1, 4, a5 } }, "element past the end" },
    { { { 57, 0, 0, 5, a5 }, { 64, 0, 4, 4, a5 } }, "unknown kind" },
    { { { 57, 0, 0, 5, a5 }, { kModTable_BirdTravel, 0, 3, 31, spots } }, "table of the wrong size" },
    { { { 57, 0, 0, 5, a5 }, { kModTable_LinkPalette, 0, 3, 123, colors } }, "truncated skin colors" },
    { { { 57, 0, 0, 5, a5 }, { kModTable_RomBytes, 0, 3, 6, spots } }, "cartridge bytes, not 4 each" },
    { { { kModTable_BirdTravel, 0, 3, 32, spots }, { kModTable_Count, 0, 3, 32, spots } }, "unknown table" },
    { { { 57, 0, 0, 5, a5 }, { 64, 0, 2, far_n, pf } }, "tile past the end of the sheet" },
    { { { 57, 0, 0, 5, a5 }, { 64, 0, 2, one_n - 1, po } }, "tiles and size do not agree" },
    { { { 57, 0, 0, 5, a5 }, { 64, 2, 2, one_n, po } }, "tiles for an element that is no sheet" },
  };
  for (size_t i = 0; i < sizeof(odd) / sizeof(odd[0]); i++) {
    Reset(); n = Build(2, odd[i].e, 2);
    CHECK(!Mods_ApplyAssetOverlay(g_file, n) && Untouched() && !Mods_SheetFromMod(64, 1) &&
          !Mods_Table(kModTable_BirdTravel) && !Mods_Table(kModTable_RomBytes), odd[i].what);
  }

  // A list: a missing file skipped, then two files, the later one winning.
  Reset();
  FILE *f = fopen(argv[1], "wb"); n = Build(2, w, 2); fwrite(g_file, 1, n, f); fclose(f);
  uint8 c3[3] = { 0xcc, 0xcc, 0xcc };
  E last[1] = { { 57, 0, 0, 3, c3 } };
  f = fopen(argv[2], "wb"); n = Build(1, last, 1); fwrite(g_file, 1, n, f); fclose(f);
  char list[1024];
  snprintf(list, sizeof(list), " %s/missing.z3mod , %s,%s ", argv[3], argv[1], argv[2]);
  CHECK(Mods_ApplyAssetOverlays(list) == 2, "list: files applied");
  CHECK(g_asset_sizes[57] == 3 && g_asset_ptrs[57][0] == 0xcc && Same(El(64, 2), (const uint8 *)"WXYZ", 4),
        "list: later file wins");
  CHECK(Mods_SheetFromMod(64, 2), "list: sheet marked");
  n = Build(2, k3, 2);
  CHECK(Mods_ApplyAssetOverlay(g_file, n) && Mods_Table(kModTable_BirdTravel), "list: table given");
  Reset();
  CHECK(!Mods_SheetFromMod(64, 2) && Untouched() && !Mods_Table(kModTable_BirdTravel),
        "a new list starts from a clean slate");
  printf("PASS: whole assets, elements, tiles, tables and cartridge bytes replaced; odd files change nothing; "
         "lists apply in order\n");
  return 0;
}
'''

PLATFORM_TEST = r'''
#include <dirent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#include "types.h"
#include "assets.h"
#include "mods.h"
#include "file_store.h"
static const char kProfilesDirectory[] = "@TMP@/profiles";
static char g_active_profile[512];
static char g_active_save_directory[512] = "@TMP@/saves";
static char g_quest_save_prefix[128];
static char g_randomizer_seed_key[65];
static void LogSetup(const char *format, ...) { (void)format; }
void Platform3DS_LogRuntime(const char *format, ...) {
  va_list ap; va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap); fputc('\n', stderr);
}
static bool EnsureDirectory(const char *path) {
  if (mkdir(path, 0777) == 0) return true;
  struct stat info;
  return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}
@SLICES@
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); return 1; } } while (0)
static int Find(ModEntry *m, int n, const char *folder) {
  for (int i = 0; i < n; i++) if (!strcmp(m[i].folder, folder)) return i;
  return -1;
}
int main(void) {
  CHECK(!strcmp(RomModLanguage("@TMP@/us.sfc"), "us"), "USA ROM");
  CHECK(!strcmp(RomModLanguage("@TMP@/ca.sfc"), "fr-c"), "Canadian ROM");
  CHECK(!strcmp(RomModLanguage("@TMP@/ca_header.smc"), "fr-c"), "Canadian ROM with a copier header");
  CHECK(!strcmp(RomModLanguage("@TMP@/none.sfc"), "us"), "missing ROM");
  ModEntry *m = g_mod_entries;
  int n = ScanMods("us", m, kMaxMods);
  CHECK(n == 6, "six mods for the USA ROM, one of them a folder down");
  CHECK(!strcmp(m[0].name, "H QUEST") && !strcmp(m[1].name, "B TEXT") && !strcmp(m[2].name, "C BOTH") &&
        !strcmp(m[3].name, "E NESTED") && !strcmp(m[4].name, "A SPRITE") && !strcmp(m[5].name, "G SKIN"),
        "QUEST, GAME, then other categories, SKIN last");
  CHECK(!strcmp(m[1].category, "GAME") && !strcmp(m[3].category, "OTHER") && !strcmp(m[4].category, "skin") &&
        !strcmp(m[3].folder, "group/e-nested"), "categories read, OTHER without one");
  int starts[kMaxMods + 1];
  CHECK(ModCategories(m, n, starts) == 4 && starts[0] == 0 && starts[1] == 1 && starts[2] == 3 && starts[3] == 4,
        "four categories, where each starts");
  CHECK(!strcmp(m[2].info[0], "FIRST LINE") && !strcmp(m[2].info[1], "SECOND LINE"), "info lines");
  int fr = ScanMods("fr-c", m, kMaxMods);
  CHECK(fr == 1 && !strcmp(m[0].folder, "c-both") && !strcmp(m[0].file, "fr-c.z3mod"), "one mod for the Canadian ROM");
  CHECK(!strcmp(m[0].info[0], "LIGNE CANADIENNE") && !m[0].info[1][0],
        "a description for one language replaces the default one whole");
  n = ScanMods("us", m, kMaxMods);
  int a = Find(m, n, "a-sprite"), b = Find(m, n, "b-text"), c = Find(m, n, "c-both"), g = Find(m, n, "g-skin");
  int h = Find(m, n, "h-quest");
  CHECK(!ModFolderValid("../x") && !ModFolderValid("a/b/c") && !ModFolderValid("a/..") &&
        !ModFolderValid("/a") && !ModFolderValid(".a") && ModFolderValid("group/e-nested"), "folder names");
  ToggleMod(m, n, b); ToggleMod(m, n, a);
  CHECK(m[a].enabled && m[b].enabled, "one mod on in each of two categories");
  ToggleMod(m, n, c);
  CHECK(m[c].enabled && !m[b].enabled && m[a].enabled, "turning one on turns off the other of its category only");
  ToggleMod(m, n, g);
  CHECK(m[g].enabled && !m[a].enabled && m[c].enabled, "category names match whatever their case");
  ToggleMod(m, n, c);
  CHECK(!m[c].enabled && m[g].enabled, "turned off again");
  ToggleMod(m, n, b); ToggleMod(m, n, h);
  CHECK(m[h].enabled && !m[b].enabled && m[g].enabled, "a QUEST turns the GAME mod off, the SKIN stays");
  ToggleMod(m, n, b);
  CHECK(m[b].enabled && !m[h].enabled && m[g].enabled, "a GAME mod turns the QUEST off");
  ToggleMod(m, n, b);
  ToggleMod(m, n, a); ToggleMod(m, n, b);
  const char *profile = "@TMP@/profiles/Some-ROM-12345678";
  CHECK(SaveModSelection(profile, m, n), "choice saved (profile folder made)");
  n = ScanMods("us", m, kMaxMods);
  LoadModSelection(profile, m, n);
  CHECK(m[Find(m, n, "a-sprite")].enabled && m[Find(m, n, "b-text")].enabled &&
        !m[Find(m, n, "c-both")].enabled && !m[Find(m, n, "g-skin")].enabled &&
        !m[Find(m, n, "h-quest")].enabled, "choice read back");
  snprintf(g_active_profile, sizeof(g_active_profile), "%s", profile);
  FILE *sel = fopen("@TMP@/profiles/Some-ROM-12345678/mods.txt", "w");
  fputs("a-sprite\nb-text\n", sel); fclose(sel);
  char list[1024];
  CHECK(BuildActiveModList("us", list, sizeof(list)), "list built");
  CHECK(!strcmp(list, "@TMP@/mods/b-text/us.z3mod,@TMP@/mods/a-sprite/us.z3mod"),
        "list for the engine: GAME before SKIN, whatever the order in mods.txt");
  CHECK(g_quest_save_prefix[0] == 0, "no QUEST: the game's saves");
  strcpy(g_randomizer_seed_key, "active-seed-test");
  CHECK(BuildActiveModList("us", list, sizeof(list)) &&
        !strcmp(list, "@TMP@/mods/a-sprite/us.z3mod"),
        "randomizer keeps character skins and excludes gameplay mods");
  g_randomizer_seed_key[0] = 0;
  CHECK(BuildActiveModList("us", list, sizeof(list)) &&
        !strcmp(list, "@TMP@/mods/b-text/us.z3mod,@TMP@/mods/a-sprite/us.z3mod"),
        "ordinary mode restores the original mod selection without editing mods.txt");

  sel = fopen("@TMP@/profiles/Some-ROM-12345678/mods.txt", "w");
  fputs("a-sprite\nh-quest\n", sel); fclose(sel);
  struct stat info;
  CHECK(BuildActiveModList("us", list, sizeof(list)) &&
        !strcmp(list, "@TMP@/mods/h-quest/us.z3mod,@TMP@/mods/a-sprite/us.z3mod") &&
        !strcmp(g_quest_save_prefix, "quest-h-quest/") &&
        stat("@TMP@/saves/quest-h-quest", &info) == 0 && S_ISDIR(info.st_mode),
        "a QUEST first, with its own save folder");
  sel = fopen("@TMP@/profiles/Some-ROM-12345678/mods.txt", "w");
  fputs("g-skin\nb-text\nh-quest\na-sprite\n", sel); fclose(sel);
  CHECK(BuildActiveModList("us", list, sizeof(list)) &&
        !strcmp(list, "@TMP@/mods/h-quest/us.z3mod,@TMP@/mods/a-sprite/us.z3mod") &&
        !strcmp(g_quest_save_prefix, "quest-h-quest/"),
        "mods.txt edited by hand: no GAME mod with a QUEST, one SKIN");
  sel = fopen("@TMP@/profiles/Some-ROM-12345678/mods.txt", "w");
  fputs("a-sprite\nb-text\n../escape\ngroup/e-nested\n", sel); fclose(sel);
  CHECK(BuildActiveModList("us", list, sizeof(list)) &&
        !strcmp(list, "@TMP@/mods/b-text/us.z3mod,@TMP@/mods/group/e-nested/us.z3mod,@TMP@/mods/a-sprite/us.z3mod"),
        "a folder down, other categories in between, nothing outside the mods folder");
  CHECK(BuildActiveModList("fr-c", list, sizeof(list)) && list[0] == 0, "no Canadian file: empty list");
  snprintf(g_active_profile, sizeof(g_active_profile), "@TMP@/profiles/none");
  CHECK(!BuildActiveModList("us", list, sizeof(list)), "no mods.txt: the setting is left alone");
  // Mods built into the RomFS.
  kBundledModsDirectory = "@TMP@/romfs/mods";
  n = ScanMods("us", m, kMaxMods);
  int i = Find(m, n, "i-built"), k = Find(m, n, "k-quest");
  a = Find(m, n, "a-sprite");
  CHECK(n == 8 && i >= 0 && k >= 0 && Find(m, n, "e-nested") < 0 && Find(m, n, "group/e-nested") >= 0,
        "the SD card's six mods and two built in: a built-in mod gives way to the SD card's of the same "
        "folder name, in a group or not");
  CHECK(!strcmp(m[a].name, "A SPRITE") && !strcmp(m[a].base, "@TMP@/mods") &&
        !strcmp(m[Find(m, n, "b-text")].name, "B TEXT") && !strcmp(m[Find(m, n, "c-both")].name, "C BOTH") &&
        m[i].base == kBundledModsDirectory && m[k].base == kBundledModsDirectory,
        "the SD card's mod is the one listed, the others come from the RomFS");
  CHECK(!strcmp(m[0].name, "H QUEST") && !strcmp(m[1].name, "K QUEST") && !strcmp(m[7].name, "I BUILT"),
        "built-in mods sorted with the others");
  fr = ScanMods("fr-c", m, kMaxMods);
  CHECK(fr == 3 && !strcmp(m[0].name, "B BUILT IN") && !strcmp(m[0].file, "fr-c.z3mod") &&
        !strcmp(m[1].name, "C BOTH") && !strcmp(m[2].name, "I BUILT"),
        "Canadian ROM: a built-in mod stays where the SD card's has no file for it");
  snprintf(g_active_profile, sizeof(g_active_profile), "%s", profile);
  sel = fopen("@TMP@/profiles/Some-ROM-12345678/mods.txt", "w");
  fputs("i-built\nb-text\n", sel); fclose(sel);
  CHECK(BuildActiveModList("us", list, sizeof(list)) &&
        !strcmp(list, "@TMP@/mods/b-text/us.z3mod,@TMP@/romfs/mods/i-built/us.z3mod"),
        "the list takes a built-in mod from the RomFS, and a mod the SD card has from the SD card");
  CHECK(BuildActiveModList("fr-c", list, sizeof(list)) &&
        !strcmp(list, "@TMP@/romfs/mods/b-text/fr-c.z3mod,@TMP@/romfs/mods/i-built/fr-c.z3mod"),
        "Canadian ROM: the built-in mod the menu lists");
  sel = fopen("@TMP@/profiles/Some-ROM-12345678/mods.txt", "w");
  fputs("k-quest\n", sel); fclose(sel);
  CHECK(BuildActiveModList("us", list, sizeof(list)) && !strcmp(list, "@TMP@/romfs/mods/k-quest/us.z3mod") &&
        !strcmp(g_quest_save_prefix, "quest-k-quest/") &&
        stat("@TMP@/saves/quest-k-quest", &info) == 0 && S_ISDIR(info.st_mode),
        "a built-in QUEST keeps its saves apart too");
  sel = fopen("@TMP@/profiles/Some-ROM-12345678/mods.txt", "w");
  fputs("e-nested\n", sel); fclose(sel);
  CHECK(BuildActiveModList("us", list, sizeof(list)) && list[0] == 0,
        "a built-in mod the SD card's replaces is not loaded either");
  // A native loose skin appears without a manifest, including in a group.
  unsigned char skin[64+16+124]={0};memcpy(skin,"Z3MOD\r\n\x1a",8);
  uint32_t version=2,entries=1;memcpy(skin+8,&version,4);memcpy(skin+12,&entries,4);
  static const unsigned char sig[48]={kAssets_Sig};memcpy(skin+16,sig,48);
  uint32_t entry[4]={kModTable_LinkPalette,0,3,124};memcpy(skin+64,entry,16);
  FILE *loose=fopen("@TMP@/mods/Loose_Hero.z3mod","wb");fwrite(skin,1,sizeof(skin),loose);fclose(loose);
  n=ScanMods("us",m,kMaxMods);int loose_index=Find(m,n,"Loose_Hero.z3mod");
  CHECK(loose_index>=0&&m[loose_index].loose&&!strcmp(m[loose_index].category,"SKIN"),"loose skin automatically discovered");
  for(int i=0;i<n;i++)m[i].enabled=false;ToggleMod(m,n,loose_index);
  CHECK(SaveModSelection(g_active_profile,m,n),"loose selection saved");
  CHECK(BuildActiveModList("us",list,sizeof(list))&&strstr(list,"/mods/Loose_Hero.z3mod")&&!strstr(list,"z3mod/"),"loose payload resolved directly");
  loose=fopen("@TMP@/Root_Hero.z3mod","wb");fwrite(skin,1,sizeof(skin),loose);fclose(loose);
  n=ScanMods("us",m,kMaxMods);CHECK(Find(m,n,"Root_Hero.z3mod")>=0,"loose skin beside ROM automatically discovered");
  entry[0]=kModTable_RomBytes;memcpy(skin+64,entry,16);loose=fopen("@TMP@/mods/Unsafe_Quest.z3mod","wb");fwrite(skin,1,sizeof(skin),loose);fclose(loose);
  n=ScanMods("us",m,kMaxMods);CHECK(Find(m,n,"Unsafe_Quest.z3mod")<0,"quest requires manifest and save namespace");
  loose=fopen("@TMP@/mods/Truncated.z3mod","wb");fwrite(skin,1,12,loose);fclose(loose);
  n=ScanMods("us",m,kMaxMods);CHECK(Find(m,n,"Truncated.z3mod")<0,"truncated native file rejected");
  // A ZSPR is a skin as downloaded: its own name and author, no manifest.
  static unsigned char zspr[29+40+0x7000+124];memset(zspr,0,sizeof(zspr));memcpy(zspr,"ZSPR\x01",5);
  uint32_t zp=29+40,zc=zp+0x7000;uint16_t zps=0x7000,zcs=124,zt=1;
  memcpy(zspr+9,&zp,4);memcpy(zspr+13,&zps,2);memcpy(zspr+15,&zc,4);memcpy(zspr+19,&zcs,2);memcpy(zspr+21,&zt,2);
  const char *zn="Sprite",*za="Artist";for(int i=0;i<6;i++){zspr[29+i*2]=zn[i];zspr[29+14+i*2]=za[i];}
  loose=fopen("@TMP@/mods/hero-1.zspr","wb");fwrite(zspr,1,sizeof(zspr),loose);fclose(loose);
  loose=fopen("@TMP@/mods/group/nested.ZSPR","wb");fwrite(zspr,1,sizeof(zspr),loose);fclose(loose);
  loose=fopen("@TMP@/root.zspr","wb");fwrite(zspr,1,sizeof(zspr),loose);fclose(loose);
  loose=fopen("@TMP@/mods/short.zspr","wb");fwrite(zspr,1,sizeof(zspr)-1,loose);fclose(loose);
  zt=2;memcpy(zspr+21,&zt,2);loose=fopen("@TMP@/mods/not-link.zspr","wb");fwrite(zspr,1,sizeof(zspr),loose);fclose(loose);
  n=ScanMods("us",m,kMaxMods);int zi=Find(m,n,"hero-1.zspr");
  CHECK(zi>=0&&m[zi].loose&&!strcmp(m[zi].category,"SKIN")&&!strcmp(m[zi].name,"Sprite")&&!strcmp(m[zi].info[0],"Artist"),"ZSPR listed with its own name and author");
  CHECK(Find(m,n,"group/nested.ZSPR")>=0&&Find(m,n,"root.zspr")>=0,"ZSPR found in a group folder and beside the ROM");
  CHECK(Find(m,n,"short.zspr")<0&&Find(m,n,"not-link.zspr")<0,"cut or non-Link ZSPR left out");
  for(int i=0;i<n;i++)m[i].enabled=false;ToggleMod(m,n,zi);
  CHECK(SaveModSelection(g_active_profile,m,n)&&BuildActiveModList("us",list,sizeof(list))&&strstr(list,"/mods/hero-1.zspr")&&!strstr(list,"zspr/"),"ZSPR choice reaches the engine's list");
  // A whole catalog fits in the menu and in mods.txt.
  for(int i=0;i<343;i++){char z[128];snprintf(z,sizeof(z),"@TMP@/mods/many-%03d.zspr",i);zt=1;memcpy(zspr+21,&zt,2);loose=fopen(z,"wb");fwrite(zspr,1,sizeof(zspr),loose);fclose(loose);}
  n=ScanMods("us",m,kMaxMods);CHECK(n>343&&Find(m,n,"many-342.zspr")>=0,"343 ZSPR files all listed");
  printf("PASS: ROM language, mods found and sorted by category, one on per category, QUEST saves apart, choice kept per profile, mods built into the RomFS\n");
  return 0;
}
'''


def rom(path, title, header=False):
    data = bytearray(0x10000)
    data[0x7fc0:0x7fc0 + len(title)] = title
    path.write_bytes((bytes(0x200) if header else b'') + bytes(data))


def slice_function(text, start):
    i = text.index(start)
    return text[i:text.index('\n}\n', i) + 3]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cflags = ['-O1', '-Wall', '-Werror', '-Wno-unused-function', '-I', str(ENGINE),
              '-iquote', str(ENGINE / 'src')]

    util = (ENGINE / 'src/util.c').read_text()
    find = slice_function(util, 'MemBlk FindIndexInMemblk(MemBlk data, size_t i) {')
    src = out / 'mods_loader_test.c'
    src.write_text(LOADER.replace('@FIND@', find))
    binary = out / 'mods_loader_test'
    subprocess.run(['cc', *cflags, str(src), str(ENGINE / 'src/mods.c'), str(ENGINE / 'src/language_cart.c'), '-o', str(binary)],
                   check=True)
    result = subprocess.run([str(binary), str(out / 'one.z3mod'), str(out / 'two.z3mod'), str(out)],
                            capture_output=True, text=True)
    print(result.stdout.strip())
    if result.returncode != 0:
        return 1

    tmp = out / 'sd'
    for fixture in ['Root_Hero.z3mod','mods/Loose_Hero.z3mod','mods/Unsafe_Quest.z3mod','mods/Truncated.z3mod',
                    'root.zspr','mods/group/nested.ZSPR','mods/hero-1.zspr','mods/short.zspr','mods/not-link.zspr',
                    *[f'mods/many-{i:03d}.zspr' for i in range(343)]]:
        (tmp/fixture).unlink(missing_ok=True)
    for d in ('mods/a-sprite', 'mods/b-text', 'mods/c-both', 'mods/d-bad', 'mods/.hidden', 'mods/group/e-nested',
              'mods/group/deeper/f-too-deep', 'mods/g-skin', 'mods/h-quest', 'profiles', 'saves'):
        (tmp / d).mkdir(parents=True, exist_ok=True)
    (tmp / 'mods/a-sprite/mod.ini').write_text('Name = A SPRITE\nCategory = skin\nus = us.z3mod\n')
    (tmp / 'mods/b-text/mod.ini').write_text('# comment\r\nName = B TEXT\r\nCategory = GAME\r\nus = us.z3mod\r\n')
    (tmp / 'mods/c-both/mod.ini').write_text(
        'Name = C BOTH\nCategory = GAME\nInfo.fr-c = LIGNE CANADIENNE\nInfo = FIRST LINE\nInfo2 = SECOND LINE\n'
        'us = us.z3mod\nfr-c = fr-c.z3mod\n')
    (tmp / 'mods/d-bad/mod.ini').write_text('Name = D BAD\nus = ../../x.z3mod\n')
    (tmp / 'mods/.hidden/mod.ini').write_text('Name = HIDDEN\nus = us.z3mod\n')
    (tmp / 'mods/README.md').write_text('not a mod')
    (tmp / 'mods/group/e-nested/mod.ini').write_text('Name = E NESTED\nus = us.z3mod\n')
    (tmp / 'mods/group/deeper/f-too-deep/mod.ini').write_text('Name = F TOO DEEP\nus = us.z3mod\n')
    (tmp / 'mods/g-skin/mod.ini').write_text('Name = G SKIN\nCategory = SKIN\nus = us.z3mod\n')
    (tmp / 'mods/h-quest/mod.ini').write_text('Name = H QUEST\nCategory = QUEST\nus = us.z3mod\n')
    # Mods built into the RomFS: the SD card has a-sprite, b-text (USA only),
    # c-both and group/e-nested; i-built and k-quest are only built in.
    for name, ini in (('a-sprite', 'Name = A BUILT IN\nCategory = skin\nus = us.z3mod\n'),
                      ('b-text', 'Name = B BUILT IN\nCategory = GAME\nus = us.z3mod\nfr-c = fr-c.z3mod\n'),
                      ('c-both', 'Name = C BUILT IN\nCategory = GAME\nus = us.z3mod\nfr-c = fr-c.z3mod\n'),
                      ('e-nested', 'Name = E BUILT IN\nus = us.z3mod\n'),
                      ('i-built', 'Name = I BUILT\nCategory = SKIN\nus = us.z3mod\nfr-c = fr-c.z3mod\n'),
                      ('k-quest', 'Name = K QUEST\nCategory = QUEST\nus = us.z3mod\n')):
        (tmp / 'romfs/mods' / name).mkdir(parents=True, exist_ok=True)
        (tmp / 'romfs/mods' / name / 'mod.ini').write_text(ini)
    # Production discovery requires the referenced payload to exist.
    for manifest in tmp.rglob('mod.ini'):
        for line in manifest.read_text().splitlines():
            if '=' not in line: continue
            key,value=(v.strip() for v in line.split('=',1))
            if key in ('us','fr-c') and '/' not in value:
                (manifest.parent/value).write_bytes(b'fixture')
    rom(tmp / 'us.sfc', b'THE LEGEND OF ZELDA  ')
    rom(tmp / 'ca.sfc', b'LA LEGENDE DE ZELDA  ')
    rom(tmp / 'ca_header.smc', b'LA LEGENDE DE ZELDA  ', header=True)

    text = PLATFORM.read_text()
    a = text.index('// Mods: sdmc:/3ds/Zelda 3DS/mods/')
    b = text.index('typedef struct ModsMenuContext {')
    mods = (ENGINE / 'src/mods.c').read_text()
    slices = (slice_function(mods, 'static uint32 ModU16(const uint8 *p) {') +
              slice_function(mods, 'static uint32 ModU32(const uint8 *p) {') +
              slice_function(mods, 'bool Mods_ZsprLayout(const uint8 *head') +
              slice_function(text, 'static char *Trim(char *text) {') + text[a:b])
    slices = slices.replace('"sdmc:/3ds/Zelda 3DS/mods"', '"@TMP@/mods"').replace('"sdmc:/3ds/Zelda 3DS"','"@TMP@"')
    # The RomFS's mods first stay away (the checks before them are those of
    # an SD card alone), then the test points it at its own folder.
    bundled = 'static const char kBundledModsDirectory[] = "romfs:/mods";'
    assert slices.count(bundled) == 1
    slices = slices.replace(bundled, 'static const char *kBundledModsDirectory = "@TMP@/no-romfs";')
    test = PLATFORM_TEST.replace('@SLICES@', slices).replace('@TMP@', str(tmp))
    src = out / 'mods_platform_test.c'
    src.write_text(test)
    binary = out / 'mods_platform_test'
    subprocess.run(['cc', *cflags, str(src), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    print(result.stdout.strip())
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
