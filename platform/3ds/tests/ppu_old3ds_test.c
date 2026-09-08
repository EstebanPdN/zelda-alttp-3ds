// Differential test/benchmark. Link the E6 PPU with its exported symbols
// renamed to ref_* (see run_ppu_old3ds_test.py). No ROM is needed.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "snes/ppu.h"

Ppu *ref_ppu_init(void);
void ref_ppu_free(Ppu *);
void ref_ppu_reset(Ppu *);
void ref_ppu_runLine(Ppu *, int);
void ref_ppu_write(Ppu *, uint8_t, uint8_t);
void ref_ppu_saveload(Ppu *, SaveLoadFunc *, void *);
void ref_PpuBeginDrawing(Ppu *, uint8_t *, size_t, uint32_t);

enum { PITCH = 512, HEIGHT = 240, GUARD = 32 };
static unsigned candidate_flags = kPpuRenderFlags_Old3DS;
static uint32_t rng = 0x714a2bu;
static uint32_t Random(void) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng;
}
static void Setup(Ppu *p, unsigned scene) {
  ppu_reset(p);
  p->forcedBlank = scene % 29 == 0;
  p->brightness = Random() & 15;
  p->mode = scene % 11 == 0 ? 7 : 1;
  p->extraLeftRight = scene & 1 ? 72 : 0;
  p->extraLeftCur = Random() % (p->extraLeftRight + 1);
  p->extraRightCur = Random() % (p->extraLeftRight + 1);
  p->extraBottomCur = scene % 17;
  p->screenEnabled[0] = Random() & 0x17;
  p->screenEnabled[1] = Random() & 0x17;
  p->screenWindowed[0] = Random() & 0x17;
  p->screenWindowed[1] = Random() & 0x17;
  p->windowsel = Random() & 0xffffff;
  p->window1left = Random(); p->window1right = Random();
  p->window2left = Random(); p->window2right = Random();
  p->clipMode = Random() & 3; p->preventMathMode = Random() & 3;
  p->addSubscreen = Random() & 1; p->subtractColor = Random() & 1;
  p->halfColor = Random() & 1; p->mathEnabled = Random() & 63;
  p->fixedColorR = Random() & 31;
  p->fixedColorG = Random() & 31;
  p->fixedColorB = Random() & 31;
  p->mosaicSize = 1 + Random() % 16;
  p->mosaicEnabled = p->mosaicSize > 1 ? Random() & 7 : 0;
  // The upstream mosaic path indexes mosaicModulo by screen x and only
  // accepts native-width mosaic windows. Wide effects clamp side space.
  if (p->mosaicEnabled) p->extraLeftCur = p->extraRightCur = 0;
  for (int i = 0; i < 256; i++) p->cgram[i] = Random() & 0x7fff;
  for (int i = 0; i < 0x8000; i++) p->vram[i] = Random();
  for (int i = 0; i < 0x110; i++) p->oam[i] = Random();
  p->objSize = 0; // Zelda uses 8/16-pixel sprites.
  for (int i = 0; i < 3; i++) {
    p->bgLayer[i].hScroll = Random() & 1023;
    p->bgLayer[i].vScroll = Random() & 1023;
    p->bgLayer[i].tilemapAdr = i * 0x1000;
    p->bgLayer[i].tileAdr = i * 0x2000;
    p->bgLayer[i].tilemapWider = Random() & 1;
    p->bgLayer[i].tilemapHigher = Random() & 1;
  }
  for (int i = 0; i < 8; i++) p->m7matrix[i] = i < 4 ? (int)(Random() % 513) - 256 : Random() & 0x1fff;
}
static void CopyState(Ppu *dst, const Ppu *src) {
  PpuTileCache *cache = dst->tileCache;
  struct PpuRetainedMaps *retained = dst->retained;
  bool attempted = dst->retainedAttempted;
  *dst = *src; dst->tileCache = cache;
  dst->retained = retained; dst->retainedAttempted = attempted;
}
static double Now(void) {
  struct timespec t; timespec_get(&t, TIME_UTC);
  return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint32_t Hash(const uint32_t *p, size_t count) {
  uint32_t h = 2166136261u;
  while (count--) h = (h ^ *p++) * 16777619u;
  return h;
}
static void PaletteUpdateTest(Ppu *p, uint32_t *output) {
  ppu_reset(p); p->forcedBlank = false; p->brightness = 15; p->mode = 1;
  p->extraLeftRight = 0;
  uint16_t colors[256] = {0}; colors[0] = 31;
  PpuUpdateCgram(p, colors);
  PpuBeginDrawing(p, (uint8_t *)output, PITCH * 4, 1 | 16);
  ppu_runLine(p, 1);
  if (output[0] != 0xff0000) { fputs("FAIL red palette upload\n", stderr); exit(1); }
  PpuUpdateCgram(p, colors);
  if (p->colorMapDirty) { fputs("FAIL redundant palette invalidation\n", stderr); exit(1); }
  colors[0] = 31 << 10;
  PpuUpdateCgram(p, colors);
  PpuBeginDrawing(p, (uint8_t *)output, PITCH * 4, 1 | 16);
  ppu_runLine(p, 1);
  if (output[0] != 0xff) { fputs("FAIL blue palette upload\n", stderr); exit(1); }
  puts("PASS direct palette upload invalidation, unchanged brightness");
}
static void ReadState(void *context, void *data, size_t size) {
  if (fread(data, 1, size, context) != size) { fputs("Short checkpoint\n", stderr); exit(2); }
}
static void LoadDump(Ppu *p, const char *directory) {
  uint8_t ram[131072]; char path[1024];
  snprintf(path, sizeof(path), "%s/ram.bin", directory);
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(2); }
  ReadState(f, ram, sizeof(ram)); fclose(f);
  snprintf(path, sizeof(path), "%s/load-state.bin", directory);
  f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
  uint8_t magic[8]; ReadState(f, magic, 8);
  if (memcmp(magic, "Z3DLDS01", 8)) { fputs("Invalid checkpoint\n", stderr); exit(2); }
  // Dump header + payload header + serialized APU/DSP/DMA prefix, per
  // InternalSaveLoad. Only read the PPU fields that the format actually saves.
  if (fseek(f, 24 + 20 + 27 + 65536 + 40 + 3024 + 15 + 192, SEEK_SET)) exit(2);
  ppu_reset(p); ppu_saveload(p, ReadState, f); fclose(f);
  memcpy(p->oam, ram + 0x800, sizeof(p->oam));
  const unsigned regs[][2] = {
    {0x00, 0x13}, {0x05, 0x94}, {0x06, 0x95},
    {0x23, 0x96}, {0x24, 0x97}, {0x25, 0x98},
    {0x30, 0x99}, {0x31, 0x9a}, {0x32, 0x9c}, {0x32, 0x9d}, {0x32, 0x9e},
    {0x2c, 0x1c}, {0x2d, 0x1d}, {0x2e, 0x1e}, {0x2f, 0x1f},
  };
  for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); i++)
    ppu_write(p, regs[i][0], ram[regs[i][1]]);
  const unsigned scroll[][2] = {
    {0x0d, 0x120}, {0x0e, 0x124}, {0x0f, 0x11e},
    {0x10, 0x122}, {0x11, 0xe4}, {0x12, 0xea}
  };
  for (unsigned i = 0; i < sizeof(scroll)/sizeof(scroll[0]); i++) {
    ppu_write(p, scroll[i][0], ram[scroll[i][1]]);
    ppu_write(p, scroll[i][0], ram[scroll[i][1] + 1]);
  }
  ppu_write(p, 0x0b, 0x22); ppu_write(p, 0x0c, 7);
  p->extraLeftRight = p->extraLeftCur = p->extraRightCur = 72;
  snprintf(path, sizeof(path), "%s/ppu.txt", directory);
  f = fopen(path, "rb");
  if (f) {
    char text[256]; unsigned configured, left, right, bottom;
    while (fgets(text, sizeof(text), f)) {
      if (sscanf(text, "side_space configured/left/right/bottom=%u/%u/%u/%u",
                 &configured, &left, &right, &bottom) == 4) {
        if (configured > kPpuExtraLeftRight || left > configured || right > configured || bottom > 16) exit(2);
        p->extraLeftRight = configured; p->extraLeftCur = left;
        p->extraRightCur = right; p->extraBottomCur = bottom;
      }
    }
    fclose(f);
  }
  // Reconstructed static register state, not full game/HDMA playback. The
  // randomized suite separately exercises changing windows and math registers.
}
int main(int argc, char **argv) {
  if (getenv("ZELDA_TEST_NEW_PROFILE")) candidate_flags = 0;
  unsigned scenes = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 2048;
  Ppu *a = ref_ppu_init(), *b = ppu_init();
  if (getenv("ZELDA_TEST_NO_RETAIN")) b->retainedAttempted = true;
  const size_t words = PITCH * HEIGHT + 2 * GUARD;
  uint32_t *ra = malloc(words * 4), *rb = malloc(words * 4);
  if (!a || !b || !ra || !rb) return 2;
  uint32_t *oa = ra + GUARD, *ob = rb + GUARD;
  if (candidate_flags) PaletteUpdateTest(b, ob);
  printf("PROFILE %s\n", candidate_flags ? "Old 3DS" : "New 3DS stable");
  uint64_t pixels = 0;
  for (unsigned s = 0; s < scenes; s++) {
    Setup(b, s); CopyState(a, b);
    memset(ra, 0xa5, words * 4); memset(rb, 0xa5, words * 4);
    for (int frame = 0; frame < 3; frame++) {
      const unsigned flags = kPpuRenderFlags_NewRenderer |
        (s & 2 ? kPpuRenderFlags_NoSpriteLimits : 0);
      ref_PpuBeginDrawing(a, (uint8_t *)oa, PITCH * 4, flags | candidate_flags);
      PpuBeginDrawing(b, (uint8_t *)ob, PITCH * 4, flags | candidate_flags);
      // Full frames also test destination borders, 224/240 and Mode 7.
      for (int line = 1; line <= HEIGHT; line++) {
        if (s % 11 == 0 && (line == 99 || line == 199)) {
          // A frame beginning in Mode 7 has no prepared Mode 1 cache.
          uint8_t mode = line == 99 ? 0x09 : 0x07;
          ref_ppu_write(a, 0x05, mode); ppu_write(b, 0x05, mode);
        }
        // Register changes exercise cache invalidation and color/window math.
        if (line % 31 == 0) {
          const uint8_t regs[] = {0x21, 0x22, 0x22, 0x32, 0x31, 0x2d};
          for (unsigned r = (s & 4) ? 3 : 0; r < sizeof(regs); r++) {
            uint8_t v = Random();
            ref_ppu_write(a, regs[r], v); ppu_write(b, regs[r], v);
          }
        }
        if ((s & 8) && line == 117) {
          const uint8_t regs[] = {0x02, 0x03, 0x04, 0x04, 0x01};
          for (unsigned r = 0; r < sizeof(regs); r++) {
            uint8_t v = regs[r] == 0x01 ? 2 : Random();
            if (regs[r] == 0x03) v &= 1;
            ref_ppu_write(a, regs[r], v); ppu_write(b, regs[r], v);
          }
        }
        if ((s & 16) && line == 101) {
          // Live VRAM DMA-style writes must retire the retained plane for
          // the rest of the frame, including on the copied worker PPU.
          const uint8_t regs[] = {0x15, 0x16, 0x17, 0x18, 0x19};
          for (unsigned r = 0; r < sizeof(regs); r++) {
            uint8_t v = Random();
            if (regs[r] == 0x15) v &= ~0x0c; // E10 supports linear VRAM access only.
            ref_ppu_write(a, regs[r], v); ppu_write(b, regs[r], v);
          }
        }
        if ((s & 32) && line == 131) {
          const uint8_t regs[] = {0x07, 0x08, 0x09, 0x0b, 0x0c};
          for (unsigned r = 0; r < sizeof(regs); r++) {
            uint8_t v = Random();
            ref_ppu_write(a, regs[r], v); ppu_write(b, regs[r], v);
          }
        }
        ref_ppu_runLine(a, line); ppu_runLine(b, line);
      }
      if (memcmp(ra, rb, words * 4)) {
        for (size_t i = 0; i < words; i++) if (ra[i] != rb[i]) {
          fprintf(stderr, "FAIL scene=%u frame=%d word=%zu E6=%08x E11=%08x\n",
                  s, frame, i, ra[i], rb[i]); break;
        }
        return 1;
      }
      for (unsigned i = 0; i < GUARD; i++)
        if (ra[i] != 0xa5a5a5a5u || ra[words-1-i] != 0xa5a5a5a5u) return 1;
      pixels += PITCH * HEIGHT;
      // Direct VRAM writes must not leave stale decoded rows across frames.
      for (int i = 0; i < 64; i++) {
        unsigned addr = Random() & 0x7fff;
        a->vram[addr] = b->vram[addr] = Random();
      }
    }
  }
  printf("PASS %u randomized scenes, %llu compared output words (E6 reference)\n",
         scenes, (unsigned long long)pixels);
  // Structured scenarios resembling the observed masks; synthetic assets.
  const char *names[] = {"fixed dark", "rain half-add", "backdrop-only math", "no math"};
  for (int scenario = 0; scenario < 4; scenario++) {
    rng = 0x451728u + scenario;
    Setup(b, 1);
    b->forcedBlank = false; b->brightness = 15; b->mode = 1;
    b->extraLeftRight = b->extraLeftCur = b->extraRightCur = 72;
    b->screenEnabled[0] = 0x16; b->screenEnabled[1] = scenario == 0 ? 0 : 1;
    b->screenWindowed[0] = b->screenWindowed[1] = 0;
    b->clipMode = b->preventMathMode = 0; b->mosaicEnabled = 0;
    b->addSubscreen = true; b->halfColor = scenario == 1;
    b->subtractColor = scenario == 0;
    b->mathEnabled = scenario == 0 ? 0x33 : scenario == 1 ? 0x32 : scenario == 2 ? 0x20 : 0;
    CopyState(a, b);
    double times[2] = {0, 0}; uint32_t hashes[2];
    for (int variant = 0; variant < 2; variant++) {
      double begin = Now();
      for (int frame = 0; frame < 1000; frame++) {
        if (variant) PpuBeginDrawing(b, (uint8_t *)ob, PITCH * 4, 9 | candidate_flags);
        else ref_PpuBeginDrawing(a, (uint8_t *)oa, PITCH * 4, 9 | candidate_flags);
        for (int line = 1; line <= 224; line++) {
          if (variant) ppu_runLine(b, line); else ref_ppu_runLine(a, line);
        }
      }
      times[variant] = (Now() - begin) * 1000 / 1000;
      hashes[variant] = Hash(variant ? ob : oa, PITCH * 224);
    }
    if (hashes[0] != hashes[1]) return 1;
    printf("HOST %-20s E6 %.3f ms E11 %.3f ms ratio %.3f hash %08x\n",
           names[scenario], times[0], times[1], times[1]/times[0], hashes[0]);
  }
  for (int dump = 2; dump < argc; dump++) {
    LoadDump(b, argv[dump]); CopyState(a, b);
    memset(ra, 0, words * 4); memset(rb, 0, words * 4);
    double times[2]; uint32_t hashes[2];
    for (int v = 0; v < 2; v++) {
      double start = Now();
      for (int frame = 0; frame < 1000; frame++) {
        if (v) PpuBeginDrawing(b, (uint8_t *)ob, PITCH * 4, 1 | candidate_flags);
        else ref_PpuBeginDrawing(a, (uint8_t *)oa, PITCH * 4, 1 | candidate_flags);
        for (int y = 1; y <= 224; y++) {
          if (v) ppu_runLine(b, y); else ref_ppu_runLine(a, y);
        }
      }
      times[v] = (Now() - start) * 1000 / 1000;
      hashes[v] = Hash(v ? ob : oa, PITCH * 224);
    }
    if (memcmp(ra, rb, words * 4)) { fputs("FAIL reconstructed dump parity\n", stderr); return 1; }
    printf("DUMP %s E6 %.3f ms E11 %.3f ms ratio %.3f hash %08x (static reconstruction)\n",
           argv[dump], times[0], times[1], times[1]/times[0], hashes[0]);
  }
  if ((!candidate_flags || getenv("ZELDA_TEST_NO_RETAIN")) && b->retained) {
    fputs("FAIL unexpected retained cache allocation\n", stderr); return 1;
  }
  ref_ppu_free(a); ppu_free(b); free(ra); free(rb);
  return 0;
}
