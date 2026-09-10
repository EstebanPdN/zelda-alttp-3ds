// Exercise actual bottom-menu drawing with both immutable 3DS profiles.
#include <SDL.h>
#include <assert.h>
#include "hardware_profile.h"
static bool test_new;
static const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void) {
  return Platform3DS_ProfileForModel(test_new);
}
#define ZELDA3_TEST_3DS_UI 1
#include "src/platform/linux/second_screen_sdl.c"
static int equipped = 1;
int SS_GetEquippedSlot(void) { return equipped; }
void SS_ReadSram(uint8_t *out, int n) { memset(out, 0, n); }

static uint32_t ReadPixel(SDL_Surface *s, int x, int y) {
  uint8_t *p=(uint8_t *)s->pixels+y*s->pitch+x*s->format->BytesPerPixel;
  return s->format->BytesPerPixel==2 ? *(uint16_t *)p : *(uint32_t *)p;
}
static void CheckInnerFill(SDL_Surface *s, float x, float y, float w, float h, uint32_t color) {
  int top=(int)floorf(y+.5f), bottom=(int)floorf(y+h+.5f);
  int cx=(int)floorf(x+w/2+.5f);
  uint32_t expected=SDL_MapRGB(s->format,color>>16,color>>8,color);
  for(int yy=top; yy<bottom; yy++) assert(ReadPixel(s,cx,yy)==expected);
}
int main(int argc, char **argv) {
  assert(SDL_Init(0)==0);
  unsigned cases=0;
  for(int model=0;model<2;model++) {
    test_new=model;
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,320,240,model?32:16,
      model?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);
    assert(s); ss_r=SDL_CreateSoftwareRenderer(s); assert(ss_r);
    W=320;H=240;u=fmaxf(0.5f,unit_for_size(W,H));
    for(int fx=0;fx<8;fx++) for(int fy=0;fy<8;fy++) for(int radius=0;radius<12;radius++) {
      float x=10+fx/8.f,y=10+fy/8.f;
      set_color(COL_BOX);SDL_RenderClear(ss_r);
      fill_round(x,y,23.25f,21.75f,radius/2.f,COL_GOLD);
      CheckInnerFill(s,x,y,23.25f,21.75f,COL_GOLD);
      cases++;
    }
    // Every inventory position, including fractional grid coordinates. Empty
    // synthetic inventory leaves the entire selected border interior visible.
    for(equipped=1;equipped<=20;equipped++) {
      set_color(COL_BG_MENU);SDL_RenderClear(ss_r);
      draw_items((RectFS){4.25f,4.125f,230.75f,202.5f});
      int slot=equipped-1;
      CheckInnerFill(s,grid_x+(slot%5)*grid_cell+8*u,
        grid_y+(slot/5)*grid_cell+8*u,grid_cell-16*u,grid_cell-16*u,COL(46,40,16));
      cases++;
    }
    // Nested frames used by map/gear/settings and tab controls.
    for(int phase=0;phase<8;phase++) {
      RectFS row={8+phase/8.f,40+phase/8.f,180.375f,24.625f};
      draw_settings_row(&row,true);
      CheckInnerFill(s,row.x+3*u,row.y+3*u,row.w-6*u,row.h-6*u,COL(58,48,12));
      cases++;
    }
    equipped=12;set_color(COL_BG_MENU);SDL_RenderClear(ss_r);
    draw_items((RectFS){4,4,230,202});
    slot_bg(248.25f,12.5f,44.5f);
    RectFS row={242.25f,80.125f,72.5f,30.75f};draw_settings_row(&row,true);
    menu_box((RectFS){242.25f,130.125f,72.5f,72.75f},COL_BOX_BORDER2);
    if(argc>model+1) assert(SDL_SaveBMP(s,argv[model+1])==0);
    SDL_DestroyRenderer(ss_r);ss_r=NULL;SDL_FreeSurface(s);
  }
  printf("PASS %u actual menu/profile cases: Old RGB565 and New ARGB8888, all 20 item selections, settings/map-style nested borders\n",cases);
  SDL_Quit();return 0;
}
