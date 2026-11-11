#!/usr/bin/env python3
"""Compare author ZSPR files with converted Mods packages in the real engine.

Private assets, reference saves and downloaded art must remain outside source.
Checks gameplay poses, armor/glove colors and the bottom map marker, across
all supplied language assets. Runs each representation in a fresh process.
"""
from pathlib import Path
import argparse, os, shlex, shutil, subprocess, json, struct, zlib

ROOT = Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--languages',type=Path,required=True)
p.add_argument('--refs',type=Path,required=True)
p.add_argument('--downloads',type=Path,required=True)
p.add_argument('--mods',type=Path,help='converted packages; without it each ZSPR is loaded as a mod itself')
p.add_argument('--work-dir',type=Path,required=True)
a=p.parse_args()
work=a.work_dir.resolve()
if work==ROOT or ROOT in work.parents:p.error('Keep private inputs outside source')
work.mkdir(parents=True,exist_ok=True)
code=r'''
#include <assert.h>
#define main ZeldaUnusedSkinMain
#include "src/main.c"
#undef main
#include "src/load_gfx.h"
#include "src/hud.h"
extern bool SS_RenderLinkFace(uint32 *pixels, int chunk);
#include "src/mods.h"
#include "snes/ppu_ui.h"
static uint8 pixels[400*240*4];
static void Run(int n, int keys) {
  while(n--) {ZeldaRunFrame(keys);ZeldaDrawPpuFrame(pixels,400*4,g_ppu_render_flags);}
}
static void Shot(const char *dir,const char *name) {
  assert(main_module_index==7 && submodule_index==0);
  printf("%s Link=(%u,%u) room=%u\n",name,link_x_coord,link_y_coord,dungeon_room_index);
  g_zenv.ppu->wideUi=PpuUiChooseLayout(main_module_index,submodule_index);
  ZeldaDrawPpuFrame(pixels,400*4,g_ppu_render_flags);
  char path[512];snprintf(path,sizeof(path),"%s/%s.raw",dir,name);
  FILE *f=fopen(path,"wb");assert(f);assert(fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels));fclose(f);
}
int main(int argc,char **argv) {
  assert(argc==6 && !chdir(argv[1]));
  bool mod=atoi(argv[5]);
  ParseConfigFile(NULL);
  g_config.mods=mod?argv[4]:NULL;g_config.link_graphics=mod?NULL:argv[3];
  LoadAssets();LoadLinkGraphics();ZeldaInitialize();ZeldaSetLanguage(g_config.language);
  g_config.extended_aspect_ratio=72;g_config.extend_y=true;
  g_config.features0|=kFeatures0_ExtendScreen64|kFeatures0_WidescreenVisualFixes;
  g_wanted_zelda_features=g_config.features0;
  g_zenv.ppu->extraLeftRight=72;g_snes_width=400;g_snes_height=240;
  g_ppu_render_flags=kPpuRenderFlags_NewRenderer|kPpuRenderFlags_Height240;
  ZeldaSetWidescreenEdgeMode(1);
  SaveLoadSlot(kSaveLoad_Load,267);LoadDefaultGraphics();Hud_Rebuild();
  which_entrance=8;main_module_index=6;submodule_index=subsubmodule_index=0;
  Run(150,0);Run(36,1<<4);Run(1,0);
  assert(main_module_index==7 && submodule_index==0);
  size_t size;uint8 *z=ReadWholeFile(argv[3],&size);assert(z&&size>=29);
  uint32 po=DWORD(z[15]);assert(po+124<=size);
  uint16 expected[62];memcpy(expected,z+po,124);
  // Check all four author palettes, including bunny; preserve the base's fifth.
  for(unsigned armor=0;armor<4;armor++)
    assert(!memcmp(Palette_GetLinkArmorColors(armor),z+po+armor*30,30));
  assert(Palette_GetLinkArmorColors(4)==kPalette_ArmorAndGloves+60);
  for(int armor=0;armor<3;armor++)for(int glove=0;glove<3;glove++) {
    link_armor=armor;link_item_gloves=glove;Palette_Load_LinkArmorAndGloves();
    if(glove)assert(main_palette_buffer[0xfd]==expected[60+glove-1]);
    Run(1,0);char name[64];snprintf(name,sizeof(name),"armor-%d-gloves-%d",armor,glove);Shot(argv[2],name);
  }
  link_armor=link_item_gloves=0;Palette_Load_LinkArmorAndGloves();Run(2,0);
  for(int d=0;d<4;d++){Run(12,1<<(4+d));char name[32];snprintf(name,sizeof(name),"walk-%d",d);Shot(argv[2],name);Run(1,0);}
  Run(1,1);Run(6,0);Shot(argv[2],"sword");
  for(int armor=0;armor<3;armor++)for(int glove=0;glove<3;glove++) {
    Palette_LoadForFileSelect_Armor(0,armor,glove);
    if(glove)assert(main_palette_buffer[0x8d]==expected[60+glove-1]);
  }
  uint32 face[16*16];assert(SS_RenderLinkFace(face,0));
  char path[512];snprintf(path,sizeof(path),"%s/marker.raw",argv[2]);FILE *f=fopen(path,"wb");assert(f);fwrite(face,4,256,f);fclose(f);
  if(mod){
    // Starting a new unmodified session must release palette table and graphics.
    g_config.mods=NULL;LoadAssets();
    assert(!Mods_Table(kModTable_LinkPalette));
    assert(Palette_GetLinkArmorColors(0)==kPalette_ArmorAndGloves);
  }
  free(z);ZeldaShutdownSession();return 0;
}
'''
config=os.environ.get('SDL2_CONFIG','sdl2-config')
prefix=subprocess.check_output([config,'--prefix'],text=True).strip()
flags=shlex.split(subprocess.check_output([config,'--cflags','--libs'],text=True).replace(prefix,shlex.quote(prefix)))
sources=[str(x) for x in (ROOT/'engine/src').glob('*.c') if x.name not in ['main.c','opengl.c','glsl_shader.c']]
sources += [str(x) for x in (ROOT/'engine/src/rando').glob('*.c')]
sources += [str(ROOT/'engine/third_party/sha256/sha256.c')]
sources += [str(x) for x in (ROOT/'engine/snes').glob('*.c')]
sources += [str(ROOT/'engine/src/platform/linux/second_screen_sdl.c'),str(ROOT/'engine/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c')]
(work/'trials.c').write_text(code);exe=work/'skin-trials'
with (work/'build.log').open('w') as log:
 subprocess.run(['cc','-DSDL_MAIN_HANDLED','-DSYSTEM_VOLUME_MIXER_AVAILABLE=0','-O1','-g','-w','-fsanitize=address','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections','-Wl,-dead_strip','-I'+str(ROOT/'engine'),'-iquote',str(ROOT/'engine/src'),'-I'+str(ROOT/'engine/third_party/opus-1.3.1-stripped'),str(work/'trials.c'),*sources,*flags,'-Wl,-rpath,'+prefix+'/lib','-o',str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True)

def png(path,data,w,h):
 rgb=bytearray(w*h*4);rgb[0::4]=data[2::4];rgb[1::4]=data[1::4];rgb[2::4]=data[0::4]
 # PPU frames store RGB in uint32 with no alpha; map markers use real alpha.
 rgb[3::4]=data[3::4] if w==16 else bytes([255])*(w*h)
 rows=b''.join(b'\0'+rgb[y*w*4:(y+1)*w*4] for y in range(h))
 def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))

env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
records=json.loads((a.downloads/'skins.json').read_text());checks=0
for folder in sorted(a.languages.iterdir()):
 if not (folder/'zelda3_assets.dat').exists():continue
 lang=folder.name;data=work/'data'/lang
 data.mkdir(parents=True,exist_ok=True);(data/'saves/ref').mkdir(parents=True,exist_ok=True)
 for filename in ['zelda3_assets.dat','zelda3_language.sfc','zelda3_localized.sfc']:
  if (folder/filename).exists():shutil.copyfile(folder/filename,data/filename)
 for ref in a.refs.glob('*.sav'):shutil.copyfile(ref,data/'saves/ref'/ref.name)
 (data/'zelda3.ini').write_text('[General]\nAutosave = 0\nLanguage = '+('' if lang=='en' else lang)+'\n[Sound]\nEnableAudio = 0\n')
 for item in records:
  outputs=[]
  for mode in [0,1]:
   out=work/'frames'/lang/item['slug']/('mod' if mode else 'zspr');out.mkdir(parents=True,exist_ok=True)
   result=subprocess.run([str(exe),str(data),str(out),str((a.downloads/item['file']).resolve()),str((a.mods/item['slug']/'skin.z3mod').resolve() if a.mods else (a.downloads/item['file']).resolve()),str(mode)],env=env,capture_output=True)
   (out/'test.log').write_bytes(result.stdout+result.stderr)
   if result.returncode:raise RuntimeError(f'{lang}/{item["slug"]}/{mode} failed: {out/"test.log"}')
   outputs.append(out)
  files=sorted(outputs[0].glob('*.raw'));assert len(files)==15
  for f in files:
   left=f.read_bytes();right=(outputs[1]/f.name).read_bytes()
   assert left==right,(lang,item['slug'],f.name)
   for out,b in zip(outputs,[left,right]):png(out/f.with_suffix('.png').name,b,16 if f.stem=='marker' else 400,16 if f.stem=='marker' else 240)
   f.unlink();(outputs[1]/f.name).unlink();checks+=1
  print(lang,item['slug'],'PASS 15 direct-ZSPR/mod comparisons',flush=True)
print(f'PASS {checks} pixel-identical scene/marker pairs, six skins, all supplied languages; armor/glove/file palette and unload assertions passed',flush=True)
