#!/usr/bin/env python3
"""Run the production exit handler with an active SDL dummy audio thread.
Desktop lifecycle test; it does not emulate NDSP or prove console startup.
"""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[3]
source=(root/'app/jni/src/src/main.c').read_text()
def function(name):
    start=source.index(name)
    begin=source.index('{',start)
    depth=1;end=begin+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
handler=function('static void Shutdown3DSRuntimeAtExit(void)')
assert 'atexit(Shutdown3DSRuntimeAtExit)' in source
program=r'''
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static volatile int callbacks;
static int ppu_stopped, bottom_stopped, presenter_stopped;
static void callback(void *p, Uint8 *out, int n) { (void)p; SDL_memset(out,0,n); callbacks++; }
static void ZeldaShutdownPpuWorker(void) { assert(!SDL_WasInit(SDL_INIT_AUDIO)); ppu_stopped=1; }
static void SecondScreenSDL_Shutdown(void) { assert(ppu_stopped); bottom_stopped=1; }
static void presenter(void) { assert(bottom_stopped);presenter_stopped=1; }
static struct { void (*Destroy)(void); } g_renderer_funcs;
HANDLER
static void verify_before_heap_teardown(void) {
  assert(!SDL_WasInit(SDL_INIT_AUDIO));
  assert(ppu_stopped && bottom_stopped && presenter_stopped);
  puts("PASS exit handler joined active SDL dummy audio before heap-teardown sentinel");
}
int main(int argc,char **argv) {
  assert(atexit(verify_before_heap_teardown)==0);
  assert(atexit(Shutdown3DSRuntimeAtExit)==0);
  g_renderer_funcs.Destroy=presenter;
  SDL_setenv("SDL_AUDIODRIVER","dummy",1);
  assert(SDL_Init(SDL_INIT_AUDIO)==0);
  SDL_AudioSpec want={0};want.freq=32000;want.format=AUDIO_S16;want.channels=2;want.samples=256;want.callback=callback;
  SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&want,NULL,0);assert(device);
  SDL_PauseAudioDevice(device,0);SDL_Delay(40);assert(callbacks>0);
  // One path exits with active audio, the other mimics prior normal shutdown.
  if(argc>1 && argv[1][0]=='n') Shutdown3DSRuntimeAtExit();
  exit(17);
}
'''.replace('HANDLER',handler)
config=os.environ.get('SDL2_CONFIG','/tmp/lttp-e7-sdl-host/bin/sdl2-config')
flags=subprocess.check_output([config,'--cflags','--static-libs'],text=True).split()
with tempfile.TemporaryDirectory(prefix='lttp-exit-test-') as td:
    p=Path(td);(p/'test.c').write_text(program)
    subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),*flags,'-o',str(p/'test')],check=True)
    for mode in ['failure','normal']:
        result=subprocess.run([str(p/'test'),mode],capture_output=True,text=True,timeout=10)
        assert result.returncode==17,(mode,result.returncode,result.stdout,result.stderr)
        assert 'PASS' in result.stdout,result.stderr
        print(mode+': '+result.stdout.strip())
