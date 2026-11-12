#include "dialogue_cjk.h"

// The messaging buffer keeps its pixels in 16-bit words: one word holds eight
// bitplanes of one column, so a pixel column is written as a mask into the
// byte at that column's half. These are the masks a text row uses, the same
// ones the letter renderer in messaging.c uses.
static const uint8_t kColumnMasks[8] = {0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1};

bool DialogueCjk_DecodePage(uint8_t prefix, uint8_t index, uint32_t *out_char) {
  if (prefix < kDialogueCjkPageFirst || prefix > kDialogueCjkPageLast)
    return false;
  *out_char = (uint32_t)(kDialogueCjkFirstChar +
                         (prefix - kDialogueCjkPageFirst) * 256 + index);
  return true;
}

bool DialogueCjk_IsGlyph(int c) {
  return c >= kDialogueCjkFirstChar;
}

// Draws one 8x16 half tile at the current pen position and advances the pen.
// `top` and `bottom` are the half tile's upper and lower eight rows, one bit
// per pixel, most significant bit leftmost. The pen keeps two values: the
// leftmost free column of this line (in `arr`) and the next free slot.
static void RenderHalf(const DialogueCjkTarget *target, const uint8_t *top,
                       const uint8_t *bottom, int width) {
  if (width <= 0)
    return;
  int slot = (*target->pen)++;
  uint8_t pen_x = target->arr[slot];
  target->arr[slot + 1] = (uint8_t)(pen_x + width);

  uint16_t base = (uint16_t)(pen_x * 2);
  uint8_t *buf = target->buf;

  for (int pass = 0; pass < 2; pass++) {
    // The second pass draws the tile's lower half eight rows further down the
    // buffer, which is where the message box keeps it.
    int line = pass == 0 ? target->line : target->line + 0x150;
    const uint8_t *src = pass == 0 ? top : bottom;
    for (int row = 0; row < 16; row += 2) {
      uint16_t bits = (uint16_t)(src[0] | (src[1] << 8));
      src += 2;
      int y = base + line;
      int x = (y & 0xff0) + row;
      y = (y >> 1) & 7;
      int left = width;
      do {
        // Each iteration consumes one bit of the row and moves to the next
        // pixel row, which is the next bitplane of the same column.
        uint8_t mask = kColumnMasks[y];
        if (bits & 0x0080)
          buf[x + 0] ^= mask;
        else
          buf[x + 0] &= (uint8_t)~mask;
        if (bits & 0x8000)
          buf[x + 1] ^= mask;
        else
          buf[x + 1] &= (uint8_t)~mask;
        bits = (uint16_t)((bits & ~0x8080) << 1);
      } while (--left && ++y != 8);
      x += 16;
      // Whatever did not fit the row's bitplanes continues in the next word,
      // exactly as the letter renderer stores it.
      if (bits != 0) {
        buf[x + 0] = (uint8_t)(bits & 0xff);
        buf[x + 1] = (uint8_t)(bits >> 8);
      }
    }
  }
}

void DialogueCjk_Render(const DialogueCjkTarget *target, int glyph) {
  const uint8_t *base = target->glyphs + glyph * kDialogueCjkGlyphBytes;
  int total = target->widths[kDialogueCjkFirstChar + glyph];

  // A glyph is drawn as two half tiles side by side: the left one holds the
  // glyph's left eight columns, the right one the rest. Wide glyphs simply
  // advance the pen twice as far.
  RenderHalf(target, base + 0 * kDialogueCjkHalfBytes,
             base + 2 * kDialogueCjkHalfBytes, total > 8 ? 8 : total);
  if (total > 8)
    RenderHalf(target, base + 1 * kDialogueCjkHalfBytes,
               base + 3 * kDialogueCjkHalfBytes, total - 8);
}
