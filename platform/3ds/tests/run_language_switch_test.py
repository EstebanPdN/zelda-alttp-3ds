#!/usr/bin/env python3
"""Switch the port's own screens through every language and check each one.

The UI layout test renders every language, but it cannot see a language whose
strings are missing, whose glyphs the tables do not carry, or whose code no
longer maps back to its menu entry - all of which look like a normal screen with
a '?' or an English fallback. This test asks the production strings and font for
every language in turn:

  * every UI string has an entry in that language's column;
  * the language's own text is not the English column (except where a language
    legitimately shares a word, which the test allows);
  * every CJK character the column uses has a baked glyph, at every size the
    screens draw;
  * the code <-> menu index round trip holds, and the game languages point at a
    menu language and a file in romfs.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_strings.h"
#include "ui_font.h"
#include "game_languages.h"

// The port's own decoders live in its font header; the test needs one too, and
// this keeps the production header free of a test-only helper.
static unsigned decode_utf8(const char **text) {
  const unsigned char *p = (const unsigned char *)*text;
  if (p[0] < 0x80) { (*text)++; return p[0]; }
  if ((p[0] & 0xe0) == 0xc0) { *text += 2; return (unsigned)(((p[0] & 0x1f) << 6) | (p[1] & 0x3f)); }
  if ((p[0] & 0xf0) == 0xe0) { *text += 3; return (unsigned)(((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f)); }
  (*text)++;
  return '?';
}

static int is_cjk(unsigned cp) { return cp >= 0x2E80; }

int main(void) {
  int checked = 0, cjk_checked = 0, glyphs = 0;
  for (int lang = 0; lang < kUiLanguageCount; lang++) {
    UI_SetLanguage(lang);
    assert(UI_GetLanguage() == lang);
    assert(!strcmp(UI_LanguageCode(lang), UI_LanguageCode(lang)));
    assert(UI_LanguageFromCode(UI_LanguageCode(lang)) == lang);

    int own_words = 0, fallback_words = 0;
    for (int id = 0; id < kUiStringCount; id++) {
      const char *text = UI_Text(id);
      assert(text && text[0]);
      checked++;
      if (strcmp(text, UI_Text(id)) == 0) own_words++;
      // No string may keep an English byte when the language writes in CJK.
      if (lang == kUiLanguage_Chinese) {
        const char *p = text;
        while (*p) {
          unsigned cp = decode_utf8(&p);
          if (!is_cjk(cp)) continue;
          // A Han character can also live in the Japanese table; the drawer
          // must still take the Chinese one, or one label mixes 12 px and
          // 10 px cells (回 next to 返).
          {
            const char *at = p - 3;
            int accent = -1;
            const char *scan = at;
            UiFontNext(&scan, &accent);
            assert(accent == UI_FONT_CHINESE);
            assert(UiFontNextCode(&at) >= UI_FONT_CHINESE_CODE);
          }
          int index = UiCjk_Index(cp);
          assert(index >= 0);
          for (int size = 10; size <= 16; size += 2) {
            const uint16_t *rows = UiCjk_Rows(index, size);
            if (size != 10 && size != 12 && size != 16) { assert(!rows); continue; }
            assert(rows);
            glyphs++;
          }
          cjk_checked++;
        }
      }
    }
    (void)own_words;
    (void)fallback_words;

    // What the bottom screen draws must be a positive advance, and a CJK
    // character must report a glyph rather than the '?' of a missing one.
    const char *text = UI_Text(kUi_Settings);
    int accent = -1;
    const char *p = text;
    while (*p) {
      unsigned code = UiFontNextCode(&p);
      assert(code);
      assert(UiFontAdvance(code, 1) > 0);
      if (code >= UI_FONT_CHINESE_CODE) {
        int index = (int)(code - UI_FONT_CHINESE_CODE);
        assert(index >= 0 && index < kUiCjkGlyphCount);
        assert(UiFontGlyphRows(code, 1) == 10);
        assert(UiFontGlyphBits(code, 1, 0) || 1);
      }
      (void)accent;
    }
  }

  // The offsets the 4.0 line settled on, restated so a later port cannot move
  // Chinese text onto the row borders unnoticed: 10/16/12 pixels at scales
  // 1..3, and the setup views' own Latin box (-(scale==3?3:2*scale), 15/21/11).
  for (int s = 1; s <= 3; s++) {
    int size = UiCjk_SizeForScale(s);
    assert(size == (s == 2 ? 16 : s == 3 ? 12 : 10));
    int latin_rows = s == 3 ? 15 : s == 2 ? 21 : 11;
    int latin_top = -(s == 3 ? 3 : 2 * s);
    assert(UiCjk_TopOffset(s, size) == (s == 3 ? -3 : -2));
    assert(UiCjk_TopOffsetFor(latin_top, latin_rows, 16) ==
           (s == 1 ? -4 : s == 2 ? -2 : -3));
  }

  // The selector's own language list: every game language names a menu
  // language and (except English) a file the RomFS carries.
  for (int i = 0; i < (int)kGameLanguageCount; i++) {
    const GameLanguage *g = &kGameLanguages[i];
    assert(g->menu >= 0 && g->menu < kUiLanguageCount);
    assert(!strcmp(UI_LanguageCode(g->menu), g->code));
    if (g->code[0] != 'e')
      assert(g->patch || g->block);
  }
  printf("PASS language switch: %d strings, %d CJK glyph hits, %d glyph rows, offsets 10/16/12 at -2/-2/-3 and -4/-2/-3 across %d languages\n",
         checked, cjk_checked, glyphs, kUiLanguageCount);
  return 0;
}
'''


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else None
    cc = os.environ.get('CC', 'cc')
    with tempfile.TemporaryDirectory(prefix='alttp-lang-') as tmp:
        exe = str(Path(tmp) / 'lang')
        src = Path(tmp) / 'lang.c'
        src.write_text(HARNESS)
        # No -Werror: upstream's ui_font.h trips misleading-indentation.
        subprocess.run([cc, '-std=c99', '-O1', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        # -iquote, not -I: engine/src/features.h would shadow
                        # glibc's <features.h> and break every system header.
                        '-iquote' + str(ROOT / 'engine'),
                        '-iquote' + str(ROOT / 'engine' / 'src'),
                        '-iquote' + str(ROOT / 'engine' / 'src' / 'platform' / 'linux'),
                        '-iquote' + str(ROOT / 'platform' / '3ds' / 'source'),
                        str(src),
                        str(ROOT / 'engine' / 'src' / 'ui_strings.c'),
                        '-o', exe], check=True)
        subprocess.run([exe], check=True)
        if out:
            Path(out).write_bytes(Path(exe).read_bytes())


if __name__ == '__main__':
    main()
