#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../../../app/jni/src/src/platform/linux/ss_sheets.h"
#include "../../../app/jni/src/src/platform/linux/ss_textures.h"

// Native-size ALttP letters and the same menu palette/frame as the touch UI.
static void UpdateView_Box(uint32_t *p, int x, int y, int w, int h, int radius, uint32_t color) {
  for (int yy=0; yy<h; yy++) for (int xx=0; xx<w; xx++) {
    int dx=xx<radius?radius-xx-1:xx>=w-radius?xx-(w-radius):0;
    int dy=yy<radius?radius-yy-1:yy>=h-radius?yy-(h-radius):0;
    if (dx*dx+dy*dy<=radius*radius && x+xx>=0 && x+xx<400 && y+yy>=0 && y+yy<240)
      p[(y+yy)*512+x+xx]=color;
  }
}
static int UpdateView_Width(const char *s, int scale) {
  int width=0; for (;*s;s++) width+=(*s==' '?5:8)*scale; return width;
}
static void UpdateView_Text(uint32_t *p, const uint32_t *letters, const uint32_t *glyphs,
                            int x, int y, const char *s, int scale) {
  const int digits[10]={SS_GLYPH_DIGIT0,SS_GLYPH_DIGIT1,SS_GLYPH_DIGIT2,SS_GLYPH_DIGIT3,SS_GLYPH_DIGIT4,
    SS_GLYPH_DIGIT5,SS_GLYPH_DIGIT6,SS_GLYPH_DIGIT7,SS_GLYPH_DIGIT8,SS_GLYPH_DIGIT9};
  for (;*s;s++) {
    char c=*s; if(c>='a'&&c<='z')c-='a'-'A';
    if(c==' '){x+=5*scale;continue;}
    const uint32_t *sheet=NULL;int cell=0,cols=0;
    if(c>='A'&&c<='Z'){sheet=letters;cell=kSS_LetterCell[c-'A'];cols=SS_LETTER_COLS;}
    if(c>='0'&&c<='9'){sheet=glyphs;cell=digits[c-'0'];cols=SS_GLYPH_COLS;}
    for(int yy=0;yy<8;yy++)for(int xx=0;xx<8;xx++) {
      uint32_t color=0;
      if(sheet)color=sheet[((cell/cols)*8+yy)*(cols*8)+(cell%cols)*8+xx];
      else {
        bool dot=(c=='.'||c==',')&&xx>=3&&xx<=4&&yy>=6;
        bool dash=c=='-'&&xx>=1&&xx<=6&&yy==3;
        bool slash=c=='/'&&xx==6-yy;
        bool colon=c==':'&&xx==3&&(yy==2||yy==5);
        bool quote=(c=='\''||c=='"')&&yy<3&&(xx==3||(c=='"'&&xx==5));
        bool bracket=(c=='('||c==')')&&xx==((yy==0||yy==7)?(c=='('?4:2):(c=='('?3:3));
        if(dot||dash||slash||colon||quote||bracket)color=0xfff8f8f8;
      }
      if(color>>24)UpdateView_Box(p,x+xx*scale,y+yy*scale,scale,scale,0,color);
    }
    x+=8*scale;
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
  char title[80];snprintf(title,sizeof(title),"CHANGELOG %s",version);
  int scale=UpdateView_Width(title,2)<=352?2:1;
  UpdateView_Text(p,letters,glyphs,200-UpdateView_Width(title,scale)/2,29-4*scale,title,scale);
  for(unsigned i=page*14;i<count&&i<(page+1)*14;i++)
    UpdateView_Text(p,letters,glyphs,20,53+(i-page*14)*11,lines[i],1);
  UpdateView_Box(p,15,213,370,13,2,0xff7a581e);
  UpdateView_Box(p,16,214,368,11,1,0xff1c1c1c);
  char footer[40];snprintf(footer,sizeof(footer),"PAGE %u / %u",page+1,(count+13)/14);
  UpdateView_Text(p,letters,glyphs,200-UpdateView_Width(footer,1)/2,215,footer,1);
}
