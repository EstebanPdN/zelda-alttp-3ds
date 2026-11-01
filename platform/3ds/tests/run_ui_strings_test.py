#!/usr/bin/env python3
"""Check every translation against the actual generated native glyph atlas."""
from pathlib import Path
import argparse, subprocess
root=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
source=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "src/ui_strings.c"
#include "src/test_scenarios.c"
#include "src/platform/linux/generated_ui_sprites.h"
#include "src/platform/linux/ui_font.h"
static unsigned checks;
static void glyphs(const char *text) {
 assert(text && *text);
 const char *p=text;char c;int accent;
 while (*p) {
  const char *start=p; c=UiFontNext(&p,&accent);
  if(c=='?' && *start!='?'){fprintf(stderr,"Missing glyph in %s at %02x %02x\n",text,(unsigned char)start[0],(unsigned char)start[1]);abort();}unsigned code=(unsigned char)c;
  if(accent>=0){bool found=false;for(unsigned i=0;i<sizeof(kNativeUiAccents)/sizeof(kNativeUiAccents[0]);i++)
   if(kNativeUiAccents[i].base==c&&kNativeUiAccents[i].accent==accent){code=kNativeUiAccents[i].code;found=true;break;}
   assert(found);}
  if(c!=' '){bool ink=false;for(int y=0;y<11;y++)ink|=kNativeUiGlyphs[code][y]!=0;assert(ink);}
 }
 checks++;
}
int main(void){
 for(int lang=0;lang<kUiLanguageCount;lang++){
  UI_SetLanguage(lang);assert(UI_LanguageFromCode(UI_LanguageCode(lang))==lang);
  for(int id=0;id<kUiStringCount;id++){assert(kUiStrings[id][lang] && *kUiStrings[id][lang]);glyphs(UI_Text(id));}
  for(int s=0;s<kTestScenarioCount;s++){glyphs(TestScenario_Name(s,lang));glyphs(TestScenario_Detail(s,lang));}
  for(int d=0;d<14;d++){glyphs(UI_DungeonName(d));assert(UiFontWidth(UI_DungeonName(d),1)<=185);}
  assert(UI_DungeonName(-1)==UI_Text(kUi_Dungeon));assert(UI_DungeonName(14)==UI_Text(kUi_Dungeon));
  assert(UI_UpdateError("NO WI-FI CONNECTION")==UI_Text(kUi_ErrorWifi));
  assert(UI_UpdateError("CANNOT WRITE TO SD CARD")==UI_Text(kUi_ErrorSdWrite));
  assert(strcmp(UI_UpdateError("UNKNOWN DIAGNOSTIC"),"UNKNOWN DIAGNOSTIC")==0);
 }
 UI_SetLanguage(99);assert(UI_GetLanguage()==0);assert(UI_LanguageFromCode(NULL)==0);assert(UI_LanguageFromCode("unknown")==0);
 printf("PASS %u translated strings/names across 8 languages: no missing translations, renderable native glyphs, dungeon title budget, code cycling and localized diagnostics\n",checks);
}
'''
file=out/'strings.c';file.write_text('#include <stdbool.h>\n'+source)
exe=out/'strings'
subprocess.run(['cc','-std=c11','-O1','-fsanitize=address,undefined','-I'+str(root/'engine'),str(file),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
