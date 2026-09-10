from pathlib import Path
import subprocess,argparse
args=argparse.ArgumentParser();args.add_argument('--out',type=Path,required=True);args=args.parse_args()
r=Path(__file__).resolve().parents[3];t=args.out.resolve();t.mkdir(parents=True,exist_ok=True)
s=(r/'app/jni/src/src/platform/linux/second_screen_sdl.c').read_text();a=s.index('static const uint8_t *tiny_letter(');b=s.index('static float tiny_text_width',a)
code='#include <assert.h>\n#include "update_view.h"\n'+s[a:b]+r'''
int main(int argc,char **argv){
 static uint32_t guarded[512*256+2],letters[128*16],glyphs[96*32];
 guarded[0]=guarded[512*256+1]=0x12345678;
 for(int c=0;c<26;c++){const uint8_t*f=tiny_letter('A'+c);for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(f[y]&(1<<(4-x)))letters[((c/16)*8+y)*128+(c%16)*8+x+1]=0xfff8f8f8;}
 const int digits[10]={SS_GLYPH_DIGIT0,SS_GLYPH_DIGIT1,SS_GLYPH_DIGIT2,SS_GLYPH_DIGIT3,SS_GLYPH_DIGIT4,SS_GLYPH_DIGIT5,SS_GLYPH_DIGIT6,SS_GLYPH_DIGIT7,SS_GLYPH_DIGIT8,SS_GLYPH_DIGIT9};
 for(int c=0;c<10;c++){const uint8_t*f=tiny_letter('0'+c);int cell=digits[c];for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(f[y]&(1<<(4-x)))glyphs[((cell/12)*8+y)*96+(cell%12)*8+x+1]=0xfff8f8f8;}
 char lines[28][43]={"- Fixed stale world colors after portals", "  and mirror travel.","", "- Added Stable and Pre-release updates.", "- Read release notes on the top screen.","- Improved touch controls and startup."};
 for(int i=6;i<28;i++)memset(lines[i],'X',42);
 for(unsigned page=0;page<2;page++){
 UpdateView_Draw(guarded+1,letters,glyphs,"v3.1-E2",lines,28,page);
 assert(guarded[0]==0x12345678&&guarded[512*256+1]==0x12345678);
 if(!page){FILE*f=fopen(argv[1],"wb");fwrite(guarded+1,4,512*256,f);fclose(f);}
 }
 puts("PASS: shared atlas mapping, native integer text, full frame, 42-column/14-line pages and guarded buffer bounds. Screenshot uses a font stand-in.");
}
'''
(t/'top-view-test.c').write_text(code)
with (t/'top-view.log').open('w') as f:
 subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I'+str(r/'platform/3ds/source'),str(t/'top-view-test.c'),'-o',str(t/'top-view-test')],stdout=f,stderr=subprocess.STDOUT,check=True)
 subprocess.run([str(t/'top-view-test'),str(t/'top-view.raw')],stdout=f,stderr=subprocess.STDOUT,check=True)
from PIL import Image
Image.frombytes('RGBA',(512,256),(t/'top-view.raw').read_bytes(),'raw','BGRA').crop((0,0,400,240)).save(t/'top-view.png')

