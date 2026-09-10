"""Check the actual APT wrapper's scope, argument forwarding and safe fallback."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'platform/3ds/source/apt_responsiveness.c').read_text().replace('#include <3ds.h>','')
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef int Result;typedef void *Thread;typedef void (*ThreadFunc)(void*);
static void *tls=(void*)1;static int actual,calls,mode;static void entry(void *p){}
static void *getThreadLocalStorage(void){return tls;}
'''
suffix=r'''
Thread __real_threadCreate(ThreadFunc f,void *arg,size_t size,int priority,int core,bool detached){
 assert(f==entry&&arg==(void*)42);actual=priority;calls++;
 if(mode==1&&priority==0x19)return NULL;return (void*)7;
}
Result __real_aptInit(void){
 __wrap_threadCreate(entry,(void*)42,0x1000,0x31,-2,true);assert(actual==(mode==1?0x31:0x19));
 tls=(void*)2;__wrap_threadCreate(entry,(void*)42,0x1000,0x31,-2,true);assert(actual==0x31);tls=(void*)1;
 __wrap_threadCreate(entry,(void*)42,0x2000,0x31,-2,true);assert(actual==0x31);
 __wrap_threadCreate(entry,(void*)42,0x1000,0x30,-2,true);assert(actual==0x30);
 __wrap_threadCreate(entry,(void*)42,0x1000,0x31,0,true);assert(actual==0x31);
 __wrap_threadCreate(entry,(void*)42,0x1000,0x31,-2,false);assert(actual==0x31);
 return -123;
}
int main(void){
 for(mode=0;mode<2;mode++){
  calls=0;assert(__wrap_aptInit()==-123);assert(!apt_init_tls);assert(calls==6+mode);
  assert(Platform3DS_GetAptEventPriority()==(mode?0x31:0x19));
  __wrap_threadCreate(entry,(void*)42,0x1000,0x31,-2,true);assert(actual==0x31);
 }
 puts("PASS APT priority isolation, TLS/stack/core/detached guards, original result, failure fallback and post-init passthrough");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(prefix+s+suffix)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
