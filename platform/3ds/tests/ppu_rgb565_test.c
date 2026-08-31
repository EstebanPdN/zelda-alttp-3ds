#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "snes/ppu.h"

enum { kWidth = 400, kHeight = 224, kPitchPixels = 512 };

static void SetupPpu(Ppu *ppu) {
  ppu_reset(ppu);
  ppu->forcedBlank = false;
  ppu->brightness = 13;
  ppu->mode = 1;
  ppu->screenEnabled[0] = 0x17;
  ppu->screenEnabled[1] = 0x07;
  ppu->addSubscreen = true;
  ppu->mathEnabled = 0x3f;
  ppu->halfColor = true;
  ppu->fixedColorR = 3;
  ppu->fixedColorG = 7;
  ppu->fixedColorB = 11;
  ppu->extraLeftRight = 72;
  PpuSetExtraSideSpace(ppu, 72, 72, 0);

  for (int i = 0; i < 256; i++)
    ppu->cgram[i] = (uint16_t)((i * 73u) ^ (i << 8));
  for (int i = 0; i < 0x8000; i++)
    ppu->vram[i] = (uint16_t)(i * 40503u + (i >> 3) * 97u);
  for (int i = 0; i < 0x100; i++)
    ppu->oam[i] = 0xf000;

  ppu->bgLayer[0].tilemapAdr = 0x0000;
  ppu->bgLayer[1].tilemapAdr = 0x0400;
  ppu->bgLayer[2].tilemapAdr = 0x0800;
  ppu->bgLayer[0].tileAdr = 0x1000;
  ppu->bgLayer[1].tileAdr = 0x3000;
  ppu->bgLayer[2].tileAdr = 0x5000;
  ppu->bgLayer[0].tilemapWider = true;
  ppu->bgLayer[1].tilemapWider = true;
  ppu->bgLayer[2].tilemapWider = true;
  ppu->bgLayer[0].hScroll = 51;
  ppu->bgLayer[1].hScroll = 137;
  ppu->bgLayer[2].hScroll = 219;
}

static uint16_t ToRgb565(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xff;
  uint32_t g = (rgb >> 8) & 0xff;
  uint32_t b = rgb & 0xff;
  return (uint16_t)((r & 0xf8) << 8 | (g & 0xfc) << 3 | b >> 3);
}

int main(void) {
  Ppu *ppu32 = ppu_init();
  Ppu *ppu16 = ppu_init();
  uint32_t *out32 = calloc(kPitchPixels * kHeight, sizeof(*out32));
  uint16_t *out16 = calloc(kPitchPixels * kHeight, sizeof(*out16));
  if (!ppu32 || !ppu16 || !out32 || !out16)
    return 2;

  SetupPpu(ppu32);
  SetupPpu(ppu16);
  PpuBeginDrawing(ppu32, (uint8_t *)out32, kPitchPixels * 4,
                  kPpuRenderFlags_NewRenderer |
                  kPpuRenderFlags_NoSpriteLimits);
  PpuBeginDrawing(ppu16, (uint8_t *)out16, kPitchPixels * 2,
                  kPpuRenderFlags_NewRenderer |
                  kPpuRenderFlags_NoSpriteLimits |
                  kPpuRenderFlags_OutputRgb565);

  for (int y = 1; y <= kHeight; y++) {
    ppu_runLine(ppu32, y);
    ppu_runLine(ppu16, y);
  }

  size_t mismatches = 0;
  for (int y = 0; y < kHeight; y++) {
    for (int x = 0; x < kWidth; x++) {
      uint16_t expected = ToRgb565(out32[y * kPitchPixels + x]);
      uint16_t actual = out16[y * kPitchPixels + x];
      if (expected != actual && ++mismatches < 8) {
        fprintf(stderr, "mismatch %d,%d: %04x != %04x\n",
                x, y, expected, actual);
      }
    }
  }

  printf("RGB565 equivalence: %s (%zu mismatches)\n",
         mismatches ? "FAIL" : "PASS", mismatches);
  ppu_free(ppu32);
  ppu_free(ppu16);
  free(out32);
  free(out16);
  return mismatches != 0;
}
