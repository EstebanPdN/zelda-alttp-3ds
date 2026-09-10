// State-transition model of Citro2D 1.7.0 DrawImage/Update/Flush. Uses the
// production format mappings and submit helper. This is not a PICA emulator.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "present_image.h"
#include "hardware_profile.h"
static C3D_TexEnv tev[3];
static TestTexture *pending;
static uint8_t displayed[3];
static bool image_mode, reject_image;
void C3D_FrameSplit(unsigned flags) { assert(flags==0 && !pending); }
void C3D_RenderTargetClear(C3D_RenderTarget *t,unsigned bits,uint32_t color,uint32_t depth) {
  assert(bits==C3D_CLEAR_COLOR && depth==0); t->clear=color;
}
C3D_TexEnv *C3D_GetTexEnv(unsigned i) { assert(i<3); return &tev[i]; }
void C3D_TexEnvInit(C3D_TexEnv *e) { *e=(C3D_TexEnv){{GPU_PREVIOUS,0,0},{0,0,0},GPU_REPLACE,0}; }
void C3D_TexEnvSrc(C3D_TexEnv *e,int channel,int a,int b,int c) { if(channel&C3D_RGB){e->source[0]=a;e->source[1]=b;e->source[2]=c;} }
void C3D_TexEnvOpRgb(C3D_TexEnv *e,int a,int b,int c) {e->operand[0]=a;e->operand[1]=b;e->operand[2]=c;}
void C3D_TexEnvFunc(C3D_TexEnv *e,int channel,int f) {if(channel&C3D_RGB)e->function=f;}
void C3D_TexEnvColor(C3D_TexEnv *e,uint32_t c) {e->color=c;}
static void ResetImageMode(void) {
  // Citro2D ImageSolid with default tint (blend=0) returns texture color and
  // resets stages 1-3. See upstream v1.7.0 base.c C2Di_Update lines 599-617.
  for(unsigned i=0;i<3;i++) C3D_TexEnvInit(&tev[i]);
  tev[0].source[0]=GPU_TEXTURE0;
}
void C2D_Flush(void) {
  if(!pending)return;
  uint8_t previous[3]={0};
  for(unsigned stage=0;stage<3;stage++) {
    C3D_TexEnv *e=&tev[stage]; uint8_t result[3];
    for(unsigned c=0;c<3;c++) {
      unsigned args[3];
      for(unsigned a=0;a<3;a++) {
        unsigned channel=e->operand[a]==GPU_TEVOP_RGB_SRC_G?1:e->operand[a]==GPU_TEVOP_RGB_SRC_B?2:e->operand[a]==GPU_TEVOP_RGB_SRC_ALPHA?3:c;
        args[a]=e->source[a]==GPU_TEXTURE0?pending->rgba[channel]:e->source[a]==GPU_CONSTANT?(e->color>>(channel*8))&255:previous[c];
      }
      unsigned value=e->function==GPU_REPLACE?args[0]:args[0]*args[1]/255+(e->function==GPU_MULTIPLY_ADD?args[2]:0);
      result[c]=value>255?255:value;
    }
    memcpy(previous,result,3);
  }
  memcpy(displayed,previous,3);pending=NULL;
}
bool C2D_DrawImage(C2D_Image image,const C2D_DrawParams *params,const void *tint) {
  (void)params;(void)tint;
  if(reject_image)return false;
  C2D_Flush();
  if(!image_mode){ResetImageMode();image_mode=true;}
  pending=image.tex;return true;
}
int main(void) {
  C3D_RenderTarget target={0xffffffff};
  Platform3DS_ClearBlackTarget(&target);
  assert(target.clear==0); // Both halves of a 16-bit fill must be black.
  C2D_DrawParams params={0};
  TestTexture t={{0,220,91,37}}; C2D_Image image={&t};
  uint8_t expected[3]={220,91,37};
  ConfigureArgbTextureEnv(); C2D_DrawImage(image,&params,NULL); C2D_Flush();
  assert(memcmp(displayed,expected,3)); // Old ordering must reproduce wrong channels.
  // Reproduce the reported New 3DS cadence: FPS switches to solid each
  // frame; a bottom image switches back to image only every other frame.
  unsigned correct=0, wrong=0;
  image_mode=false;
  for(unsigned frame=0;frame<8;frame++) {
    ConfigureArgbTextureEnv(); C2D_DrawImage(image,&params,NULL); C2D_Flush();
    if(memcmp(displayed,expected,3)) wrong++; else correct++;
    image_mode=false; // Solid FPS overlay leaves this as Citro2D's mode.
    if(!(frame&1)) {
      ConfigureArgbTextureEnv(); C2D_DrawImage(image,&params,NULL); C2D_Flush();
    }
  }
  assert(correct==4 && wrong==4);
  for(unsigned model=0;model<2;model++) {
    const Platform3DSHardwareProfile *p=Platform3DS_ProfileForModel(model);
    assert(p->old_ppu==!model && p->live_palette_upload);
    assert(p->rgb565_ui_textures==!model && p->integer_ui_rounding);
    for(unsigned frame=0;frame<4096;frame++) {
      expected[0]=frame*97;expected[1]=frame*11;expected[2]=frame*31;
      t=(TestTexture){{0,expected[0],expected[1],expected[2]}};
      if(frame&1) image_mode=false; // FPS/DUMP SAVED solid-to-image transition.
      assert(Platform3DS_DrawMappedImage(image,&params,ConfigureArgbTextureEnv));
      assert(!memcmp(displayed,expected,3));assert(!pending);
      if(!model) t=(TestTexture){{expected[0],expected[1],expected[2],255}};
      image_mode=false; // FPS or DUMP SAVED.
      if(!model || !(frame&1)) {
        assert(Platform3DS_DrawMappedImage(image,&params,model?ConfigureArgbTextureEnv:ConfigureRgb565TextureEnv));
        assert(!memcmp(displayed,expected,3));assert(!pending);
      }
    }
  }
  reject_image=true;
  assert(!Platform3DS_DrawMappedImage(image,&params,ConfigureArgbTextureEnv));
  puts("PASS: legacy New cadence alternates 4 wrong/4 correct; 8192 top/bottom transitions preserve RGB; both hardware policies isolated; failed draw handled");
}
