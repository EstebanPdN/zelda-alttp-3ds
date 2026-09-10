// Production zoom button: symmetric strokes on both native pixel formats.
#include <SDL.h>
#include <assert.h>
#include "hardware_profile.h"
static bool test_new;
static const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void) {
  return Platform3DS_ProfileForModel(test_new);
}
#define ZELDA3_TEST_3DS_UI 1
#include "src/platform/linux/second_screen_sdl.c"
static bool white(SDL_Surface *s,int x,int y) {
  uint8_t *p=(uint8_t*)s->pixels+y*s->pitch+x*s->format->BytesPerPixel;
  uint32_t pixel=s->format->BytesPerPixel==2?*(uint16_t*)p:*(uint32_t*)p;
  return pixel==SDL_MapRGB(s->format,255,255,255);
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
      int bx=lroundf(r.x+7),by=lroundf(r.y+7),count=0;
      for(int y=0;y<28;y++)for(int x=0;x<28;x++) {
        bool ink=white(s,bx+x,by+y);count+=ink;
        if(ink!=white(s,bx+27-x,by+y)){fprintf(stderr,"model=%d plus=%d fx=%d fy=%d u=%f x=%d y=%d\n",model,plus,fx,fy,u,x,y);SDL_SaveBMP(s,"/tmp/e21-zoom-failure.bmp");abort();}
        assert(ink==white(s,bx+x,by+27-y));
        if(plus)assert(ink==white(s,bx+y,by+27-x));
        // A centered 14x2 horizontal stroke, and an equal vertical for plus.
        bool expected=(x>=7&&x<21&&y>=13&&y<15)||(plus&&x>=13&&x<15&&y>=7&&y<21);
        assert(ink==expected);
      }
      assert(count==(plus?52:28));cases++;
    }
    set_color(COL_BG_MENU);SDL_RenderClear(ss_r);whole_map=false;draw_map_zoom_button((RectFS){5,5,213,191});whole_map=true;draw_map_zoom_button((RectFS){45,5,213,191});
    if(argc>model+1)assert(SDL_SaveBMP(s,argv[model+1])==0);
    SDL_DestroyRenderer(ss_r);SDL_FreeSurface(s);
  }
  printf("PASS %u map zoom cases: exact centering, equal strokes, reflections and plus rotation, Old/New\n",cases);SDL_Quit();
}
