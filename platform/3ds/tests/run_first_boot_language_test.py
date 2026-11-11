#!/usr/bin/env python3
"""Exercise production system-language selection and first profile boot flow."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3];s=(r/'platform/3ds/source/platform_3ds.c').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a)+1;n=1
 while n:n+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]+'\n'
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "game_languages.h"
typedef unsigned char u8;
#define R_SUCCEEDED(x) ((x)>=0)
#define R_FAILED(x) ((x)<0)
#define countof(x) (sizeof(x)/sizeof(*(x)))
enum {CFG_LANGUAGE_JP,CFG_LANGUAGE_EN,CFG_LANGUAGE_FR,CFG_LANGUAGE_DE,CFG_LANGUAGE_IT,CFG_LANGUAGE_ES,CFG_LANGUAGE_CN,CFG_LANGUAGE_KO,CFG_LANGUAGE_NL,CFG_LANGUAGE_PT,CFG_LANGUAGE_RU};
static int system_language,result,chosen,selected_calls,prepared,fail_once;static bool forced;
static int cfguInit(void){return result;}static void cfguExit(void){}
static int CFGU_GetSystemLanguage(u8*p){*p=system_language;return result;}
static int ReadChosenLanguage(void){return chosen;}static bool WriteChosenLanguage(int v){chosen=v;return true;}
typedef struct{char profile[64];}RomEntry;static RomEntry g_rom_entries[1]={{{0}}};
static const char*kForceSelectorFile="force",*kSelectedRomFile="selected",*kAssetsFilename="assets";
static bool IsRegularFile(const char*p){return forced&&!strcmp(p,"force");}
static int ScanRoms(RomEntry*r,int n){strcpy(r[0].profile,"profiles/test");return 1;}
static bool missing,cancel_missing;static unsigned notices;
static int FindUsaRom(RomEntry*r,int n){return missing?-1:0;}
static bool ShowMissingRom(void){notices++;if(cancel_missing)return false;missing=false;return true;}
static bool g_startup_menu_pending,g_startup_menu_seen,g_startup_reopen_once,g_setup_progress_suppressed;
static int g_setup_progress_language,g_pending_randomizer_kind;
static struct{char key[80];}g_pending_randomizer_seed;
static const GameLanguage*g_profile_menu_language,*g_build_language;
static const char*g_profile_prepare_status="Storage failure";
static bool aptMainLoop(void){return true;}
static int SelectLanguage(int l,int status,const RomEntry*r){selected_calls++;return l;}
static bool AssetsFileLooksValid(const char*p){return false;}
static bool ProfileLanguageMatches(const char*p,const GameLanguage*l){return true;}
static unsigned silent_progress,visible_progress;
static void Platform3DS_ShowOperationProgress(bool d,int v){if(g_setup_progress_suppressed)silent_progress++;else visible_progress++;}
static bool MigrateLegacyStorage(const RomEntry*r){return true;}
static bool FileStore_Recover(const char*p){return false;}
static char*Trim(char*p){return p;}
static bool RandoSeedKey_Valid(const char*p){return false;}
static bool EnsureProfileReady(const RomEntry*r,bool rebuild){prepared++;if(fail_once){fail_once--;return false;}return true;}
'''+fn('static int LanguageFromSystem(')+fn('static bool ResolveActiveProfile(')+r'''
int main(void){
 const int expected[]={7,0,4,5,3,1,0,0,0,2,6,0,0,0,0,0};
 for(int i=0;i<16;i++){
  g_startup_menu_seen=false;system_language=i;result=0;chosen=-1;forced=false;selected_calls=prepared=0;
  silent_progress=visible_progress=0;char profile[128];assert(ResolveActiveProfile(profile,sizeof(profile)));
  assert(chosen==expected[i]&&selected_calls==0&&prepared==1&&g_startup_menu_pending);
  assert(g_build_language==&kGameLanguages[expected[i]]);
  assert(silent_progress==0&&visible_progress==0);
 }
 result=-1;chosen=-1;selected_calls=0;char profile[128];assert(ResolveActiveProfile(profile,sizeof(profile)));assert(chosen==0&&!selected_calls);
 result=0;system_language=CFG_LANGUAGE_ES;chosen=5;assert(ResolveActiveProfile(profile,sizeof(profile)));assert(chosen==5&&!selected_calls);
 forced=true;assert(ResolveActiveProfile(profile,sizeof(profile)));assert(selected_calls==1);forced=false;
 fail_once=1;selected_calls=0;assert(ResolveActiveProfile(profile,sizeof(profile)));assert(selected_calls==1);
 missing=true;selected_calls=0;assert(ResolveActiveProfile(profile,sizeof(profile)));assert(notices==1&&!selected_calls);
 missing=cancel_missing=true;assert(!ResolveActiveProfile(profile,sizeof(profile)));assert(notices==2&&!selected_calls);
 puts("PASS missing ROM notice/retry/cancel without language selector; silent cold preparation (including uncached assets); first boot: 16 system languages, CFG failure, persisted override, explicit language selector and error retry");
}
'''
with tempfile.TemporaryDirectory()as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I'+str(r/'platform/3ds/source'),'-I'+str(r/'engine/src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=t,check=True)
