#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "snes/ppu.h"

enum { kWidth = 400, kHeight = 224, kPitchPixels = 512 };

static void SetupPpu(Ppu *ppu, int extra_side_space) {
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
  ppu->extraLeftRight = (uint8_t)extra_side_space;
  PpuSetExtraSideSpace(ppu, extra_side_space, extra_side_space, 0);

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

static uint32_t RenderAndHash(Ppu *ppu, uint32_t *output) {
  PpuBeginDrawing(ppu, (uint8_t *)output, kPitchPixels * 4,
                  kPpuRenderFlags_NewRenderer |
                  kPpuRenderFlags_NoSpriteLimits);
  for (int y = 1; y <= kHeight; y++)
    ppu_runLine(ppu, y);

  uint32_t hash = 2166136261u;
  for (int y = 0; y < kHeight; y++) {
    for (int x = 0; x < kWidth; x++)
      hash = (hash ^ output[y * kPitchPixels + x]) * 16777619u;
  }
  return hash;
}

int main(void) {
  static const uint32_t kExpectedWideHash = 0x4be2b6deu;
  static const uint32_t kExpectedOriginalHash = 0x7546bad1u;
  Ppu *ppu = ppu_init();
  uint32_t *output = calloc(kPitchPixels * kHeight, sizeof(*output));
  if (!ppu || !output)
    return 2;

  SetupPpu(ppu, 72);
  uint32_t wide_first = RenderAndHash(ppu, output);
  uint32_t wide_second = RenderAndHash(ppu, output);
  SetupPpu(ppu, 0);
  uint32_t original_first = RenderAndHash(ppu, output);
  uint32_t original_second = RenderAndHash(ppu, output);
  SetupPpu(ppu, 0);
  (void)RenderAndHash(ppu, output);
  ppu->screenEnabled[1] = 0;
  uint32_t disabled_subscreen_after_active = RenderAndHash(ppu, output);
  SetupPpu(ppu, 0);
  ppu->screenEnabled[1] = 0;
  uint32_t disabled_subscreen_fresh = RenderAndHash(ppu, output);
  printf("PPU BGRX WIDE: first=%08x second=%08x expected=%08x\n",
         wide_first, wide_second, kExpectedWideHash);
  printf("PPU BGRX ORIGINAL: first=%08x second=%08x expected=%08x\n",
         original_first, original_second, kExpectedOriginalHash);
  printf("PPU disabled subscreen: after-active=%08x fresh=%08x\n",
         disabled_subscreen_after_active, disabled_subscreen_fresh);

  ppu_free(ppu);
  free(output);
  return wide_first != kExpectedWideHash ||
         wide_second != kExpectedWideHash ||
         original_first != kExpectedOriginalHash ||
         original_second != kExpectedOriginalHash ||
         disabled_subscreen_after_active != disabled_subscreen_fresh;
}
