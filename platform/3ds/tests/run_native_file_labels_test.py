#!/usr/bin/env python3
"""Check one-word native headings, glyphs, idempotence and write boundaries."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
code=r'''
#include <assert.h>
#include <stdio.h>
#include "native_file_labels.h"
static uint16_t before[32768],vram[32768],first[32768];
int main(void) {
  const char *languages[]={"en","es","fr","de","it","pt","ru","fr-c","sv"};
  for(unsigned l=0;l<9;l++)for(unsigned module=1;module<=4;module++)for(unsigned cap=21;cap<=27;cap+=2) {
    bool localized=l==2||l==3||l==7;
    unsigned row=module==4?4:2,frame=0x1000+row*32,base=localized?0x7800:0x6000;
    for(unsigned i=0;i<32768;i++)vram[i]=0x1234;
    vram[frame+2]=0x3589;vram[frame+cap]=0x7589;
    memcpy(before,vram,sizeof(vram));NativeFileLabels_Apply(vram,module,languages[l],localized);
    for(unsigned i=0;i<32768;i++) {
      unsigned y=(i-base)/32,x=(i-base)%32;
      bool title=(y==row+1||y==row+2)&&x>=4&&x<cap-1;
      bool action=module==1&&y>=22&&y<=25&&x>=6&&x<30;
      if(!title&&!action)assert(vram[i]==before[i]);
    }
    memcpy(first,vram,sizeof(vram));NativeFileLabels_Apply(vram,module,languages[l],localized);
    assert(!memcmp(first,vram,sizeof(vram)));
    const char *word=NativeFileLabels_Word(languages[l],module-1);
    if(l!=6)for(unsigned i=0;i<strlen(word);i++)assert(word[i]!=' ');
    unsigned start=4+(cap-5-strlen(word))/2;
    for(unsigned i=0;i<strlen(word);i++) {
      unsigned glyph=NativeFileLabels_Glyph((unsigned char)word[i],localized,l==6);
      assert(vram[base+(row+1)*32+start+i]==(0x1800|glyph));
      unsigned lower=(unsigned char)word[i]==0x81?0x1e:glyph+16;
      assert(vram[base+(row+2)*32+start+i]==(0x1800|lower));
    }
  }
  // The Swedish cartridge's plain O: top 0x64 over the original O's lower half.
  assert(!strcmp(NativeFileLabels_Word("sv",1),"K\x81PIERA"));
  assert(NativeFileLabels_Glyph(0x81,false,false)==0x64);
  memcpy(before,vram,sizeof(vram));
  NativeFileLabels_Apply(vram,7,"es",false);assert(!memcmp(before,vram,sizeof(vram)));
  assert(!strcmp(NativeFileLabels_Word("es",0),"PARTIDA"));
  assert(!strcmp(NativeFileLabels_Word("es",3),"NOMBRE"));
  assert(NativeFileLabels_Glyph(0x80,true,false)==0xcd);
  puts("PASS 144 native file-label scenes: all languages, four menus, translated frame widths, exact glyph rows, bounded writes and repeated frames");
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-file-labels-') as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I'+str(root/'engine/src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
