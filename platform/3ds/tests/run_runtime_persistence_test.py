#!/usr/bin/env python3
"""Run real INI writer/profile copy/loader across fresh processes, no console."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'platform/3ds/source/platform_3ds.c').read_text()
u=(r/'app/jni/src/src/platform/linux/second_screen_sdl.c').read_text()
def function(text, signature):
 a=text.index(signature); b=text.index('{',a); level=1; end=b+1
 while level:
  level += (text[end]=='{')-(text[end]=='}');end+=1
 return text[a:end]+'\n'
code=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
// SD archive rename can reject an existing destination. Do not inherit the
// host filesystem's overwrite behavior (which hid E16's regression).
static int sd_rename(const char *from, const char *to) {
 struct stat st;
 if(stat(to,&st)==0){errno=EEXIST;return -1;}
 return rename(from,to);
}
#define rename sd_rename
#define __3DS__ 1
#define SDL_strcasecmp strcasecmp
#define SDL_strncasecmp strncasecmp
#define Platform3DS_LogRuntime(...) ((void)0)
#define LogSetup(...) ((void)0)
enum {kPlatform3DSDisplayOriginal,kPlatform3DSDisplayUltraWideMod,kPlatform3DSDisplayStretch};
enum {kPlatform3DSWideEdgeStandard,kPlatform3DSWideEdgeFixedCamera};
enum {kPlatform3DSCStickDisabled,kPlatform3DSCStickTurbo};
static bool g_is_new_3ds,g_display_mode_auto,g_wide_edge_mode_auto;
static bool g_display_mode_legacy_stretch,g_runtime_wide_edge_seen,g_show_fps;
static int g_display_mode,g_wide_edge_mode,g_wide_zoom_index,g_cstick_mode,g_turbo_multiplier;
static const char *kBundledConfig="bundled.ini";
static bool ProfileSetupFailure(const char *status,const char *path){return false;}
static const char *kProfilesDirectory="profiles", *g_active_save_directory="saves/test-rom";
static bool ParseBool(const char *v,bool *b){*b=atoi(v)!=0;return true;}
'''
for signature in ['static bool CopyFileReplacing(const char *source, const char *destination) {','void Platform3DS_PersistRuntimeSettings(void)', 'static void Platform3DS_ApplyAutoDisplayDefaults(void)', 'static char *Trim(char *text)', 'static void LoadRuntimeSetting(const char *key, const char *value)', 'void Platform3DS_LoadRuntimeSettings(void)']:
 code+=function(s,signature)
code+=function(s,'static bool IsRegularFile(const char *path) {')
code+=function(s,'static bool CopyFileIfMissing(')
code+=function(s,'static bool MigrateWideDefaults(')
code+=function(u,'static void update_ini(')
code+=r'''
int main(int argc,char **argv) {
 assert(argc==3);g_is_new_3ds=atoi(argv[1]);
 assert(MigrateWideDefaults("profiles/test-rom/zelda3.ini"));
 // Exact boot restore operation used by ResolveActiveProfile.
 assert(CopyFileReplacing("profiles/test-rom/zelda3.ini","zelda3.ini"));
 Platform3DS_LoadRuntimeSettings();
 if(!strcmp(argv[2],"change")) {
  assert(g_display_mode==1);assert(g_wide_edge_mode==1);
  update_ini("[General]","DisplayMode","Original");
  update_ini("[General]","WideEdgeMode","Standard");
  update_ini("[General]","WideZoom","1.5");
  update_ini("[General]","CStickTurboMultiplier","2");
 } else {
  assert(g_display_mode==0);assert(g_wide_edge_mode==0);
  assert(g_wide_zoom_index==2);assert(g_turbo_multiplier==2);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-settings-') as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 for model in range(4):
  cwd=p/str(model);profile=cwd/'profiles/test-rom';profile.mkdir(parents=True)
  (profile/'zelda3.ini').write_text('[General]\nDisplayMode = '+('Auto' if model<2 else 'Original')+'\nWideEdgeMode = '+('Auto' if model<2 else 'Standard')+'\nWideZoom = 1.2\n[Sound]\nEnableAudio = 1\n')
  for action in ['change','reopen','reopen']:
   subprocess.run([str(p/'test'),str(model%2),action],cwd=cwd,check=True)
  assert (profile/'zelda3.ini.wide-defaults-v1').read_text()=='1\n'
  assert 'EnableAudio = 1' in (profile/'zelda3.ini').read_text()
 print('PASS one-time migration for fresh and existing Old/New profiles; subsequent Original/Standard/zoom/turbo menu writes survive two fresh process restarts; marker and unrelated keys retained')
