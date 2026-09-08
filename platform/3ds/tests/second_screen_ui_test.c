// Exercise the production SDL map/cinema drawing without game assets or a ROM.
#include <SDL.h>
#include <assert.h>
#include <stdint.h>
static uint32_t test_ticks;
static uint32_t TestTicks(void) { return test_ticks; }
#define SDL_GetTicks TestTicks
#include "src/platform/linux/second_screen_sdl.c"
#undef SDL_GetTicks
static bool test_portal;
bool SS_GetMirrorPortal(int *p) { p[0]=2048; p[1]=2048; return test_portal; }

int main(int argc, char **argv) {
  assert(SDL_Init(0)==0);
  SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,320,240,16,SDL_PIXELFORMAT_RGB565);
  assert(surface);
  ss_r=SDL_CreateSoftwareRenderer(surface); assert(ss_r);
  W=320; H=240; u=unit_for_size(W,H);
  uint32_t pixels[512*512];
  for(unsigned y=0;y<512;y++) for(unsigned x=0;x<512;x++)
    pixels[y*512+x]=0xff000000u | (((x/16+y/16)&1)?0x407038:0x305828);
  tex_map[0]=make_tex(512,512,pixels,false); tex_map[1]=tex_map[0];
  for(unsigned i=0;i<256;i++) pixels[i]=0xff20b020;
  tex_face=make_tex(16,16,pixels,true);
  tex_bg_parch=make_tex(kSSTexParch_W,kSSTexParch_H,kSSTexParch,false);
  tex_triforce=make_tex(kSSTexTriforce_W,kSSTexTriforce_H,kSSTexTriforce,true);
  assert(tex_map[0] && tex_face && tex_bg_parch && tex_triforce);
  SDL_SetTextureScaleMode(tex_triforce,SDL_ScaleModeLinear);
  uint16_t before[320*240],after[320*240];
  ss_is_new_3ds=false;test_ticks=100;draw_cinema();
  SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,before,640);
  test_ticks=2300;draw_cinema();
  SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,after,640);
  assert(!memcmp(before,after,sizeof(before)));
  if(argc>1) SDL_SaveBMP(surface,argv[1]);
  ss_is_new_3ds=true;test_ticks=0;draw_cinema();
  SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,before,640);
  test_ticks=1000;draw_cinema();
  SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,after,640);
  assert(memcmp(before,after,sizeof(before)));
  whole_map=true;RectFS r={4,4,228,194};
  test_portal=false;draw_overworld(r,1000,1000,0);
  SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,before,640);
  test_portal=true;draw_overworld(r,1000,1000,0);
  SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,after,640);
  unsigned changed=0;
  for(unsigned y=0;y<240;y++) for(unsigned x=0;x<320;x++) if(before[y*320+x]!=after[y*320+x]) {
    assert(x>=4 && x<232 && y>=4 && y<198);changed++;
  }
  assert(changed>0 && changed<250);
  if(argc>2) SDL_SaveBMP(surface,argv[2]);
  test_portal=false;draw_overworld(r,1000,1000,0);
  SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,after,640);
  assert(!memcmp(before,after,sizeof(before))); // Portal removal leaves no trail.
  puts("PASS production SDL: static Old/animated New Triforce; portal appears and erases within map bounds");
  return 0;
}
