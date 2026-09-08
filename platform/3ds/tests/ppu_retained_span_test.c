// Differential coverage of the production ARM pair merge, including odd
// source/destination addresses, transparent pixels and tail guards. No assets.
#include <stdint.h>
#include <string.h>
#include "snes/ppu_retained.h"
static uint32_t rng = 0x186bc527u;
static uint32_t Random(void) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
int main(void) {
  _Alignas(4) uint16_t src[408], actual[408], expected[408];
  for (unsigned trial = 0; trial < 10000; trial++) {
    unsigned n = Random() % 401, layer = Random() % 3;
    unsigned so = 2 + (Random() & 1), dst = 2 + (Random() & 1);
    uint16_t mask = layer == 2 ? 0xfffc : 0xfff0;
    for (unsigned i = 0; i < 408; i++) {
      actual[i] = expected[i] = Random();
      src[i] = (Random() & 7) ? Random() : 0;
    }
    for (unsigned i = 0; i < n; i++)
      if ((src[so+i] & mask) > expected[dst+i]) expected[dst+i] = src[so+i];
    PpuMergeSpan(actual+dst, src+so, n, layer);
    for (unsigned i = 0; i < 408; i++) if (actual[i] != expected[i]) return 1;
  }
  return 0;
}
