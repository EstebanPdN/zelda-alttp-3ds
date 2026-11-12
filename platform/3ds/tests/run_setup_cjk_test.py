#!/usr/bin/env python3
"""Shared Han characters must retain the selected setup-screen font family."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'platform/3ds/source/platform_3ds.c').read_text()


def function(signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_font.h"
typedef int gfxScreen_t;
static unsigned char pixels[64][64];
static uint8_t SetupGlyph(uint32_t c, int row) { return 0; }
static void SetupFillRect(int screen, int x, int y, int w, int h, uint16_t color) {
  assert(x >= 0 && y >= 0 && x + w <= 64 && y + h <= 64);
  for (int yy=y; yy<y+h; yy++) for (int xx=x; xx<x+w; xx++) pixels[yy][xx]=1;
}
'''
harness += function('static int SetupCharWidth(')
harness += function('static void SetupDrawChar(')
harness += r'''
int main(void) {
  unsigned shared = 0;
  for (unsigned i=0; i<kUiJapaneseGlyphCount; i++) {
    unsigned cp = kUiJapaneseCodepoints[i];
    if (!UiCjk_HasGlyph(cp)) continue;
    shared++;
    for (int scale=1; scale<=3; scale++) {
      UI_SetLanguage(kUiLanguage_Japanese);
      assert(SetupCharWidth(cp,scale)==11*scale);
      memset(pixels,0,sizeof(pixels));
      SetupDrawChar(0,cp,4,12,scale,1);
      for (int row=0; row<10; row++) for (int col=0; col<10; col++) {
        int ink=!!(kUiJapaneseGlyphs10[i][row] & (1<<(9-col)));
        for (int yy=0; yy<scale; yy++) for (int xx=0; xx<scale; xx++)
          assert(pixels[12+(row-2)*scale+yy][4+col*scale+xx]==ink);
      }
      UI_SetLanguage(kUiLanguage_Chinese);
      assert(SetupCharWidth(cp,scale)==16);
      memset(pixels,0,sizeof(pixels));
      SetupDrawChar(0,cp,4,12,scale,1);
      const uint16_t *rows=UiCjk_RowsFor(cp);
      int top=scale==1 ? -4 : scale==2 ? -2 : -3;
      for (int row=0; row<16; row++) for (int col=0; col<16; col++)
        assert(pixels[12+top+row][4+col]==!!(rows[row]&(1<<(15-col))));
    }
  }
  assert(shared>0);
  printf("PASS setup font family: %u shared characters, both languages, three scales\n", shared);
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-setup-cjk-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(harness)
    subprocess.run(['cc', '-O1', '-fsanitize=address,undefined',
                    '-I'+str(ROOT/'engine/src/platform/linux'),
                    str(work/'test.c'), str(ROOT/'engine/src/ui_strings.c'),
                    '-o', str(work/'test')], check=True)
    subprocess.run([str(work/'test')], check=True)
