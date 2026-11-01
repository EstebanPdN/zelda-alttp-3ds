#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../../../engine/src/platform/linux/ss_sheets.h"
#include "../../../engine/src/platform/linux/ui_font.h"
#include "../../../engine/src/ui_strings.h"
#include "../../../engine/src/platform/linux/ss_textures.h"

// Native-size ALttP letters and the same menu palette/frame as the touch UI.
static void UpdateView_Box(uint32_t *p, int x, int y, int w, int h, int radius, uint32_t color) {
  for (int yy=0; yy<h; yy++) for (int xx=0; xx<w; xx++) {
    int dx=xx<radius?radius-xx-1:xx>=w-radius?xx-(w-radius):0;
    int dy=yy<radius?radius-yy-1:yy>=h-radius?yy-(h-radius):0;
    if (dx*dx+dy*dy<=radius*radius && x+xx>=0 && x+xx<400 && y+yy>=0 && y+yy<240)
      p[(y+yy)*512+x+xx]=color;
  }
}
static int UpdateView_Width(const char *s, int scale) { return UiFontWidth(s,scale); }
static void UpdateView_Text(uint32_t *p, const uint32_t *letters, const uint32_t *glyphs,
                            int x, int y, const char *s, int scale) {
  (void)letters; (void)glyphs;
  unsigned code;
  while((code=UiFontNextCode(&s))) {
    int rows=UiFontGlyphRows(code,scale), top=y+UiFontGlyphTop(code,scale), width=UiFontGlyphColumns(code,scale);
    for(int yy=0;yy<rows;yy++) for(int xx=0;xx<width;xx++) {
      unsigned bits=UiFontGlyphBits(code,scale,yy);
      if(bits&(1u<<(width-1-xx))) UpdateView_Box(p,x+xx,top+yy,1,1,0,0xfff8f8f8);
    }
    x+=UiFontAdvance(code,scale);
  }
}
static void UpdateView_Draw(uint32_t *p, const uint32_t *letters, const uint32_t *glyphs,
                            const char *version, char lines[][43], unsigned count, unsigned page) {
  for(int y=0;y<256;y++)for(int x=0;x<512;x++)
    p[y*512+x]=(x<400&&y<240)?kSSTexMenu[((y/2)%kSSTexMenu_H)*kSSTexMenu_W+(x/2)%kSSTexMenu_W]:0xff000000;
  UpdateView_Box(p,4,4,392,232,5,0xff0c0c0c);
  UpdateView_Box(p,5,5,390,230,4,0xff60c878);
  UpdateView_Box(p,7,7,386,226,3,0xffc8c8c8);
  UpdateView_Box(p,9,9,382,222,3,0xff0c0c0c);
  const int corners[4][2]={{7,7},{390,7},{7,226},{390,226}};
  for(int i=0;i<4;i++)UpdateView_Box(p,corners[i][0],corners[i][1],3,3,1,0xffffffff);
  UpdateView_Box(p,15,15,370,29,3,0xff7a581e);
  UpdateView_Box(p,16,16,368,27,2,0xff1c1c1c);
  char title[80];snprintf(title,sizeof(title),"%s %s",UI_Text(kUi_Changes),version);
  int scale=UpdateView_Width(title,2)<=352?2:1;
  UpdateView_Text(p,letters,glyphs,200-UpdateView_Width(title,scale)/2,29-4*scale,title,scale);
  for(unsigned i=page*14;i<count&&i<(page+1)*14;i++)
    UpdateView_Text(p,letters,glyphs,20,53+(i-page*14)*11,lines[i],1);
  UpdateView_Box(p,15,213,370,13,2,0xff7a581e);
  UpdateView_Box(p,16,214,368,11,1,0xff1c1c1c);
  char footer[40];snprintf(footer,sizeof(footer),"%s %u / %u",UI_Text(kUi_Page),page+1,(count+13)/14);
  UpdateView_Text(p,letters,glyphs,200-UpdateView_Width(footer,1)/2,215,footer,1);
}
