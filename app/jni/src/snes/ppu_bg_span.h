#ifndef ZELDA3_PPU_BG_SPAN_H_
#define ZELDA3_PPU_BG_SPAN_H_
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static inline bool PpuRowIsOpaque(uint32_t pixels) {
  // There is a zero nibble iff subtracting one borrows through a nibble's
  // high bit while that bit was clear. Only the all-nonzero result is used.
  return ((pixels - 0x11111111u) & ~pixels & 0x88888888u) == 0;
}
static inline uint32_t PpuSelectBgPair(uint32_t old, uint32_t source, uint32_t z) {
#if defined(__3DS__) && defined(__arm__) && !defined(__thumb__)
  uint32_t result;
  // USUB16 produces two independent >= conditions in GE; z-1 makes the
  // original z > destination test strict. SEL consumes GE in the same asm
  // block so no intervening compiler instruction can replace those flags.
  __asm__("usub16 %0, %2, %3\n\tsel %0, %1, %3"
          : "=&r"(result) : "r"(source), "r"(z - 0x00010001u), "r"(old) : "cc");
  return result;
#else
  uint32_t lo = (uint16_t)z > (uint16_t)old ? source & 0xffffu : old & 0xffffu;
  uint32_t hi = (z >> 16) > (old >> 16) ? source & 0xffff0000u : old & 0xffff0000u;
  return lo | hi;
#endif
}
static inline void PpuDrawOpaqueBgRow(uint16_t *dst, uint32_t pixels,
                                     uint16_t z, bool hflip) {
  if (!hflip) {
    pixels = __builtin_bswap32(pixels);
    pixels = (pixels >> 4 & 0x0f0f0f0fu) | (pixels << 4 & 0xf0f0f0f0u);
  }
  bool unaligned = ((uintptr_t)dst & 2) != 0;
  if (unaligned) {
    if (z > dst[0]) dst[0] = z + (pixels & 15);
    dst++; pixels >>= 4;
  }
  const uint32_t zz = z | (uint32_t)z << 16;
  const unsigned pairs = unaligned ? 3 : 4;
  // Every 32-bit access below is aligned, even for odd camera/scroll offsets.
  uint32_t *out = (uint32_t *)__builtin_assume_aligned(dst, 4);
  for (unsigned i = 0; i < pairs; i++, pixels >>= 8) {
    uint32_t source = zz + (pixels & 15) + ((pixels & 0xf0) << 12);
    out[i] = PpuSelectBgPair(out[i], source, zz);
  }
  if (unaligned && z > dst[6]) dst[6] = z + (pixels & 15);
}
#endif
