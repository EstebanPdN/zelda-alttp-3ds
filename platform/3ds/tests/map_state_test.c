#include <assert.h>
#include <stdio.h>
#include "src/zelda_rtl.h"
#include "src/variables.h"
#include "snes/ppu.h"
bool SS_GetMirrorPortal(int *out);
void SS_ResetRomCaches(void);
int main(void) {
  Ppu ppu={0};g_zenv.ppu=&ppu;ppu.extraLeftRight=72;
  enhanced_features0=kFeatures0_WidescreenVisualFixes;
  ZeldaSetWidescreenEdgeMode(1);
  main_module_index=9;submodule_index=0;
  assert(ZeldaGetWidescreenFixedCameraMargin()==72);
  for(unsigned context=0;context<2;context++) {
    main_module_index=14;saved_module_for_menu=context?7:9;
    const unsigned modes[]={3,7,10};
    for(unsigned m=0;m<3;m++) for(unsigned state=0;state<9;state++) {
      submodule_index=modes[m];overworld_map_state=state;
      assert(ZeldaGetWidescreenFixedCameraMargin()==0);
    }
  }
  int out[2];overworld_screen_index=0;
  assert(!SS_GetMirrorPortal(out));
  bird_travel_x_hi[15]=9;bird_travel_x_lo[15]=123;
  bird_travel_y_hi[15]=5;bird_travel_y_lo[15]=231;
  assert(SS_GetMirrorPortal(out) && out[0]==2427 && out[1]==1511);
  overworld_screen_index=0x40;assert(!SS_GetMirrorPortal(out));
  overworld_screen_index=0;bird_travel_x_hi[15]=bird_travel_x_lo[15]=0;
  bird_travel_y_hi[15]=bird_travel_y_lo[15]=0;
  assert(!SS_GetMirrorPortal(out));
  SS_ResetRomCaches();
  puts("PASS engine: map camera disabled through all map/warp states; live mirror portal coordinates/world/removal");
}
