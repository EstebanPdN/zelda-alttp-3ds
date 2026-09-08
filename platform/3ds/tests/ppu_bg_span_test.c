#include <stdio.h>
#include <string.h>
#include "snes/ppu_bg_span.h"
static uint32_t state = 314159u;
static uint32_t Random(void) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
int main(void) {
  for (unsigned n = 0; n < 1000000; n++) {
    uint32_t pixels = Random(); bool opaque = true;
    for (int i = 0; i < 8; i++) opaque &= ((pixels >> (i * 4)) & 15) != 0;
    if (opaque != PpuRowIsOpaque(pixels)) return 1;
    if (!opaque) continue;
    uint16_t z = (Random() & 0xff00) | (Random() & 0xf0);
    if (z == 0 || z > 0xfff0) continue;
    for (int offset = 0; offset < 2; offset++) for (int flip = 0; flip < 2; flip++) {
      _Alignas(4) uint16_t expected[12], actual[12];
      for (int i = 0; i < 12; i++) expected[i] = Random();
      // Exercise strict equality as well as both comparison outcomes.
      expected[2 + offset] = z;
      memcpy(actual, expected, sizeof(actual));
      for (int i = 0; i < 8; i++) if (z > expected[i + offset])
        expected[i + offset] = z + ((pixels >> ((flip ? i : 7-i) * 4)) & 15);
      PpuDrawOpaqueBgRow(actual + offset, pixels, z, flip);
      if (memcmp(expected, actual, sizeof(actual))) {
        fprintf(stderr, "FAIL row %u alignment %d flip %d\n", n, offset, flip); return 1;
      }
    }
  }
  puts("PASS 1,000,000 tile rows: opacity, strict priority, flip, alignment, guards");
  return 0;
}
