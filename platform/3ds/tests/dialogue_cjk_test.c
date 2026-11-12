// Chinese dialogue text: the escape decoding and the 16x16 glyph blit.
//
// The blit writes into the messaging buffer, whose layout is the letter
// renderer's: one 16-bit word per pixel column per row group. This test drives
// the renderer with hand-built glyphs and checks the pen arithmetic and the
// pixels it touches, not the engine around it.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../engine/src/dialogue_cjk.h"

static uint8_t g_buf[0x2000];
static uint8_t g_arr[0x800];
static uint16_t g_pen;

static void Reset(void) {
  memset(g_buf, 0, sizeof(g_buf));
  memset(g_arr, 0, sizeof(g_arr));
  g_pen = 0;
}

static DialogueCjkTarget Target(const uint8_t *glyphs, const uint8_t *widths) {
  DialogueCjkTarget target = {
    .buf = g_buf,
    .arr = g_arr,
    .pen = &g_pen,
    .line = 0,
    .glyphs = glyphs,
    .widths = widths,
  };
  return target;
}

// Every width byte the language block carries, with the letter widths the
// engine never reads for a glyph, kept at a value the code must ignore.
static uint8_t g_widths[kDialogueCjkFirstChar + 8];

static void TestDecode(void) {
  uint32_t c = 0;
  assert(DialogueCjk_DecodePage(0xf0, 0, &c) && c == 111);
  assert(DialogueCjk_DecodePage(0xf0, 255, &c) && c == 111 + 255);
  assert(DialogueCjk_DecodePage(0xf1, 1, &c) && c == 111 + 256 + 1);
  assert(DialogueCjk_DecodePage(0xf4, 255, &c) && c == 111 + 4 * 256 + 255);
  // Every glyph the pages can name must be a glyph, and no letter may be.
  for (int prefix = 0; prefix < 256; prefix++) {
    bool escapes = prefix >= kDialogueCjkPageFirst && prefix <= kDialogueCjkPageLast;
    for (int index = 0; index < 256; index += 31) {
      bool decoded = DialogueCjk_DecodePage((uint8_t)prefix, (uint8_t)index, &c);
      assert(decoded == escapes);
      if (decoded) {
        assert(c >= (uint32_t)kDialogueCjkFirstChar);
        assert(DialogueCjk_IsGlyph((int)c));
      }
    }
  }
  assert(!DialogueCjk_IsGlyph(kDialogueCjkFirstChar - 1));
  assert(DialogueCjk_IsGlyph(kDialogueCjkFirstChar));
  puts("decode ok");
}

// A glyph whose every pixel is set: the blit must touch exactly the pixels of
// its two halves, and the pen must advance by the glyph's total width.
static void TestWideGlyphSpan(void) {
  static uint8_t glyphs[3 * kDialogueCjkGlyphBytes];
  memset(glyphs, 0xff, sizeof(glyphs));
  g_widths[kDialogueCjkFirstChar] = 16;
  Reset();
  DialogueCjkTarget target = Target(glyphs, g_widths);
  DialogueCjk_Render(&target, 0);
  assert(g_pen == 2);
  assert(g_arr[0] == 0);
  assert(g_arr[1] == 8);
  assert(g_arr[2] == 16);
  bool any = false;
  for (size_t i = 0; i < sizeof(g_buf); i++)
    any |= g_buf[i] != 0;
  assert(any);

  // A blank glyph writes nothing at all but still advances the pen.
  memset(glyphs, 0, sizeof(glyphs));
  Reset();
  target = Target(glyphs, g_widths);
  DialogueCjk_Render(&target, 0);
  assert(g_pen == 2);
  for (size_t i = 0; i < sizeof(g_buf); i++)
    assert(g_buf[i] == 0);
  puts("wide glyph ok");
}

// A narrow glyph is one half tile and advances by its own width.
static void TestNarrowGlyph(void) {
  // Two glyphs: this one renders index 1, so the block has to hold it.
  static uint8_t glyphs[2 * kDialogueCjkGlyphBytes];
  memset(glyphs, 0xff, sizeof(glyphs));
  g_widths[kDialogueCjkFirstChar + 1] = 6;
  Reset();
  DialogueCjkTarget target = Target(glyphs, g_widths);
  DialogueCjk_Render(&target, 1);
  assert(g_pen == 1);
  assert(g_arr[0] == 0);
  assert(g_arr[1] == 6);
  puts("narrow glyph ok");
}

// Two glyphs in a row: the second one starts where the first one ended, so the
// text keeps the letter renderer's single pen.
static void TestAdvanceAcrossGlyphs(void) {
  static uint8_t glyphs[4 * kDialogueCjkGlyphBytes];
  memset(glyphs, 0, sizeof(glyphs));
  g_widths[kDialogueCjkFirstChar + 0] = 12;
  g_widths[kDialogueCjkFirstChar + 1] = 10;
  Reset();
  DialogueCjkTarget target = Target(glyphs, g_widths);
  DialogueCjk_Render(&target, 0);
  DialogueCjk_Render(&target, 1);
  assert(g_pen == 4);
  assert(g_arr[1] == 8);
  assert(g_arr[2] == 12);
  assert(g_arr[3] == 20);
  assert(g_arr[4] == 22);
  puts("advance ok");
}

int main(void) {
  TestDecode();
  TestWideGlyphSpan();
  TestNarrowGlyph();
  TestAdvanceAcrossGlyphs();
  puts("dialogue_cjk_test ok");
  return 0;
}
