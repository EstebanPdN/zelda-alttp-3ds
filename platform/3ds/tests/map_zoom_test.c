// Production zoom button: round gold slot, symmetric gold strokes on both
// native pixel formats.
#include <SDL.h>
#include <assert.h>
#include "hardware_profile.h"
static bool test_new;
static const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void) {
  return Platform3DS_ProfileForModel(test_new);
}
#define ZELDA3_TEST_3DS_UI 1
#include "src/platform/linux/second_screen_sdl.c"
static uint32_t pixel(SDL_Surface *s,int x,int y) {
  uint8_t *p=(uint8_t*)s->pixels+y*s->pitch+x*s->format->BytesPerPixel;
  return s->format->BytesPerPixel==2?*(uint16_t*)p:*(uint32_t*)p;
}
static bool gold(SDL_Surface *s,int x,int y) {
  return pixel(s,x,y)==SDL_MapRGB(s->format,227,182,76);
}
int main(int argc,char **argv) {
  assert(SDL_Init(0)==0);unsigned cases=0;
  for(int model=0;model<2;model++) {
    test_new=model;W=320;H=240;u=fmaxf(0.5f,unit_for_size(W,H));
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,W,H,model?32:16,model?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);
    assert(s);ss_r=SDL_CreateSoftwareRenderer(s);assert(ss_r);
    for(int plus=0;plus<2;plus++)for(int fx=0;fx<8;fx++)for(int fy=0;fy<8;fy++) {
      whole_map=plus;set_color(COL_BG_MENU);SDL_RenderClear(ss_r);
      RectFS r={5+fx/8.f,5+fy/8.f,213,191};draw_map_zoom_button(r);SDL_RenderFlush(ss_r);
      // The 22 px centre box; its rims (gold too) lie outside the inscribed disc.
      int bx=lroundf(r.x+24),by=lroundf(r.y+13),count=0;
      for(int y=0;y<22;y++)for(int x=0;x<22;x++) {
        // The whole slot, rims included, is mirror- and (for plus) rotation-symmetric.
        uint32_t c=pixel(s,bx+x,by+y);
        if(c!=pixel(s,bx+21-x,by+y)){fprintf(stderr,"model=%d plus=%d fx=%d fy=%d u=%f x=%d y=%d\n",model,plus,fx,fy,u,x,y);SDL_SaveBMP(s,"/tmp/e21-zoom-failure.bmp");abort();}
        if((x-10.5f)*(x-10.5f)+(y-10.5f)*(y-10.5f)>=10*10) continue;
        bool ink=gold(s,bx+x,by+y);count+=ink;
        if(plus)assert(ink==gold(s,bx+y,by+21-x));
        // A centered 10x2 horizontal stroke, and an equal vertical for plus.
        bool expected=(x>=6&&x<16&&y>=10&&y<12)||(plus&&x>=10&&x<12&&y>=6&&y<16);
        assert(ink==expected);
      }
      assert(count==(plus?36:20));cases++;
    }
    set_color(COL_BG_MENU);SDL_RenderClear(ss_r);whole_map=false;draw_map_zoom_button((RectFS){5,5,213,191});whole_map=true;draw_map_zoom_button((RectFS){45,5,213,191});
    if(argc>model+1)assert(SDL_SaveBMP(s,argv[model+1])==0);
    SDL_DestroyRenderer(ss_r);SDL_FreeSurface(s);
  }
  printf("PASS %u map zoom cases: exact centering, equal strokes, mirrored slot and rims, plus rotation, Old/New\n",cases);SDL_Quit();
}
