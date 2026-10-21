// Compare the retained layer with actual draw_status_bar output: wood ground,
// plaque, soft-edged 11 px hearts in one or two rows, magic and counters, on
// an RGB565 target and on the 3DS's RGBA8888 window format read back as RGB565.
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
    BottomHearts_Capture(&captured, ss_r, tex_heart_scratch, draw_heart, count, size,
                        columns, step, x, y, sram8(0x6d), sram8(0x6c), sram8(0x7b) >= 1);
}
static void Draw(bool dungeon, uint16_t *out) {
  draw_ground((SDL_Rect){0, 176, 320, 34});  // the ground under and around the bar
  draw_status_bar(dungeon);
  SDL_RenderFlush(ss_r);
  assert(SDL_RenderReadPixels(ss_r, NULL, SDL_PIXELFORMAT_RGB565, out, 640) == 0);
}
int main(void) {
  assert(SDL_Init(0)==0);
  static uint16_t actual[320*240], expected[320*240];
  unsigned cases=0;
  const Uint32 formats[2]={SDL_PIXELFORMAT_RGB565,SDL_PIXELFORMAT_RGBA8888};
  for(int f=0;f<2;f++) {
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,320,240,f?32:16,formats[f]);
    assert(surface);ss_r=SDL_CreateSoftwareRenderer(surface);assert(ss_r);
    W=320;H=240;u=.5f;
    // The production art: baked wood, chrome icons and label font.
    tex_bg_wood=make_tex(kSSTexWood_W,kSSTexWood_H,kSSTexWood,false);
    tex_ui_icons=make_tex(kSSUiIcons_W,kSSUiIcons_H,kSSUiIcons,true);
    ui_font[FONT_LARGE].tex=make_tex(kSSUiFontLarge_W,kSSUiFontLarge_H,kSSUiFontLarge,true);
    ui_font[FONT_SMALL].tex=make_tex(kSSUiFontSmall_W,kSSUiFontSmall_H,kSSUiFontSmall,true);
    tex_heart_scratch=SDL_CreateTexture(ss_r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,12,12);
    assert(tex_bg_wood&&tex_ui_icons&&tex_heart_scratch);
    SDL_SetTextureBlendMode(tex_heart_scratch,SDL_BLENDMODE_NONE);
    for(int dungeon=0;dungeon<2;dungeon++) for(int half=0;half<2;half++) for(int cap=1;cap<=20;cap++) {
      memset(sram,0,sizeof(sram));sram[0x6c]=cap*8;sram[0x6d]=cap*8;sram[0x7b]=half;
      sram[0x6e]=64;sram[0x6f]=dungeon?3:0xff;sram[0x62]=0x37;sram[0x43]=12;sram[0x77]=30;
      capture_enabled=true;Draw(dungeon,actual);assert(captured.valid);capture_enabled=false;
      // Capturing leaves the frame exactly as an uncaptured draw.
      Draw(dungeon,expected);assert(!memcmp(actual,expected,sizeof(actual)));
      // Damage down to zero, then healing up to capacity, including sub-half-heart
      // changes which must not touch any pixel or trigger an upload.
      for(int t=0;t<=cap*16;t++) {
        int health=t<=cap*8 ? cap*8-t : t-cap*8;
        int changed=BottomHearts_Apply(&captured,(uint8_t*)actual,640,health);
        assert(changed<=1);
        sram[0x6d]=health;Draw(dungeon,expected);
        if(memcmp(actual,expected,sizeof(actual))) {
          for(int i=0;i<320*240;i++) if(actual[i]!=expected[i]) {
            fprintf(stderr,"mismatch format=%d cap=%d half=%d dungeon=%d health=%d x=%d y=%d actual=%04x expected=%04x\n",f,cap,half,dungeon,health,i%320,i/320,actual[i],expected[i]);break;
          }
          return 1;
        }
        cases++;
      }
      // A large change is bounded by the capacity (maximum 20 cells).
      assert(BottomHearts_Apply(&captured,(uint8_t*)actual,640,0)<=20);
      sram[0x6d]=0;Draw(dungeon,expected);assert(!memcmp(actual,expected,sizeof(actual)));
    }
    captured.valid=false;assert(!BottomHearts_Apply(&captured,(uint8_t*)actual,640,100));
    destroy_textures();
    SDL_DestroyRenderer(ss_r);SDL_FreeSurface(surface);
  }
  SDL_Quit();
  printf("PASS %u full-screen pixel comparisons: damage/healing, 1-20 hearts in one or two rows, half magic, keys, RGB565 and RGBA8888 targets; at most one cell per half-heart step\n",cases);
}
