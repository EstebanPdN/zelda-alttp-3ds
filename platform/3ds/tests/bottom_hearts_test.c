// Compare the retained layer with actual draw_sidebar output, including its
// background, equipment ring, integer geometry and all three heart sizes.
#include <SDL.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "src/platform/linux/bottom_hearts.h"
static BottomHearts captured;
static bool capture_enabled;
static void capture_bottom_hearts(int count, int size, int columns, int step, int x, int y);
#include "hardware_profile.h"
static const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void) { return Platform3DS_ProfileForModel(false); }
#define ZELDA3_TEST_3DS_UI 1
#define ZELDA3_TEST_BOTTOM_HEARTS 1
#include "src/platform/linux/second_screen_sdl.c"
int SS_GetEquippedSlot(void) { return 0; }
void SS_ReadSram(uint8_t *out, int n) { memset(out, 0, n); }
static void capture_bottom_hearts(int count, int size, int columns, int step, int x, int y) {
  if (capture_enabled)
    BottomHearts_Capture(&captured, ss_r, count, size, columns, step, x, y,
                        sram8(0x6d), sram8(0x6c), sram8(0x7b) >= 1);
}
static void Draw(bool dungeon) {
  draw_tiled(dungeon ? tex_bg_stone : tex_bg_menu,
    dungeon ? kSSTexStone_W : kSSTexMenu_W,
    dungeon ? kSSTexStone_H : kSSTexMenu_H,
    (RectFS){0,0,320,240}, COL_BG_MENU);
  draw_sidebar(222,5,93,191,dungeon);
  SDL_RenderFlush(ss_r);
}
int main(void) {
  assert(SDL_Init(0)==0);
  SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,320,240,16,SDL_PIXELFORMAT_RGB565);
  assert(surface);ss_r=SDL_CreateSoftwareRenderer(surface);assert(ss_r);
  W=320;H=240;u=.5f;
  tex_bg_menu=make_tex(kSSTexMenu_W,kSSTexMenu_H,kSSTexMenu,false);
  tex_bg_stone=make_tex(kSSTexStone_W,kSSTexStone_H,kSSTexStone,false);
  // Synthetic binary-alpha atlas stresses every nearest-neighbor sample and
  // RGB565 color bit, rather than requiring a private ROM in this test.
  uint32_t sheet[SS_GLYPH_COLS*8*((kGlyphCount+kGlyphCols-1)/kGlyphCols)*8];
  for(unsigned i=0;i<sizeof(sheet)/sizeof(*sheet);i++)
    sheet[i]=(i%5==0) ? 0 : 0xff000000u|((i*7919u)&0xffffff);
  tex_glyphs=make_tex(SS_GLYPH_COLS*8,((kGlyphCount+kGlyphCols-1)/kGlyphCols)*8,sheet,true);
  const int cells[]={SS_GLYPH_HEART_EMPTY,SS_GLYPH_HEART_HALF,SS_GLYPH_HEART_FULL};
  BottomHearts_LoadGlyphs(sheet,SS_GLYPH_COLS*8,cells,SS_GLYPH_COLS);
  uint8_t actual[320*240*2];
  unsigned cases=0;
  for(int dungeon=0;dungeon<2;dungeon++) for(int half=0;half<2;half++) for(int cap=1;cap<=20;cap++) {
    memset(sram,0,sizeof(sram));sram[0x6c]=cap*8;sram[0x6d]=cap*8;sram[0x7b]=half;
    capture_enabled=true;Draw(dungeon);assert(captured.valid);capture_enabled=false;
    memcpy(actual,surface->pixels,sizeof(actual));
    // Damage down to zero, then healing up to capacity, including sub-half-heart
    // changes which must not touch any pixel or trigger an upload.
    for(int t=0;t<=cap*16;t++) {
      int health=t<=cap*8 ? cap*8-t : t-cap*8;
      int changed=BottomHearts_Apply(&captured,actual,640,health);
      assert(changed<=1);
      sram[0x6d]=health;Draw(dungeon);
      if(memcmp(actual,surface->pixels,sizeof(actual))) {
        for(int i=0;i<320*240;i++) if(((uint16_t*)actual)[i]!=((uint16_t*)surface->pixels)[i]) {
          fprintf(stderr,"mismatch cap=%d half=%d dungeon=%d health=%d x=%d y=%d actual=%04x expected=%04x\n",cap,half,dungeon,health,i%320,i/320,((uint16_t*)actual)[i],((uint16_t*)surface->pixels)[i]);break;
        }
        return 1;
      }
      cases++;
    }
    // A large change is bounded by the capacity (maximum 20 cells).
    assert(BottomHearts_Apply(&captured,actual,640,0)<=20);
    sram[0x6d]=0;Draw(dungeon);assert(!memcmp(actual,surface->pixels,sizeof(actual)));
  }
  captured.valid=false;assert(!BottomHearts_Apply(&captured,actual,640,100));
  SDL_DestroyTexture(tex_glyphs);SDL_DestroyTexture(tex_bg_menu);SDL_DestroyTexture(tex_bg_stone);
  SDL_DestroyRenderer(ss_r);SDL_FreeSurface(surface);SDL_Quit();
  printf("PASS %u full-screen pixel comparisons: damage/healing, 1-20 hearts, half magic, both backgrounds, 10/12/16px glyphs; at most one cell per half-heart step\n",cases);
}
