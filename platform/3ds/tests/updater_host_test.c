// Host I/O harness: real transfer/hash/control flow, fake console install service.
#include <assert.h>
#include <stdarg.h>
#include "updater.c"
static bool close_request,wrong_title,short_write,no_space;
static unsigned starts,finishes,cancels,written;
static FILE *input_file;
bool aptShouldClose(void){return close_request;}bool aptIsActive(void){return true;}
bool aptIsHomeAllowed(void){return true;}bool aptIsSleepAllowed(void){return true;}
void aptSetHomeAllowed(bool b){}void aptSetSleepAllowed(bool b){}
void Platform3DS_LogRuntime(const char*f,...){(void)f;}
Result FSUSER_GetSdmcArchiveResource(FS_ArchiveResource*r){r->freeClusters=no_space?0:100000;r->clusterSize=4096;return 0;}
Result amInit(void){return 0;}void amExit(void){}
Result FSUSER_OpenFileDirectly(Handle*h,int a,FS_Path b,FS_Path c,int d,int e){input_file=fopen(UPDATE_PART,"rb");*h=1;return input_file?0:-1;}
Result AM_GetCiaFileInfo(int a,AM_TitleInfo*i,Handle h){i->titleID=wrong_title?123:0x0004000005a13e00ull;return 0;}
Result AM_GetCiaRequiredSpace(u64*r,int a,Handle h){*r=5000000;return 0;}
Result AM_StartCiaInstallOverwrite(Handle*h,int a){starts++;*h=2;return 0;}
Result FSFILE_Read(Handle h,u32*n,u64 off,void*b,u32 s){fseek(input_file,off,SEEK_SET);*n=fread(b,1,s,input_file);return 0;}
Result FSFILE_Write(Handle h,u32*n,u64 off,const void*b,u32 s,u32 f){*n=short_write?s-1:s;written+=*n;return 0;}
Result AM_FinishCiaInstall(Handle h){finishes++;return 0;}
Result AM_CancelCIAInstall(Handle h){cancels++;return 0;}
Result FSFILE_Close(Handle h){fclose(input_file);return 0;}
int main(int argc,char**argv){
 initialized=true; status.state=UPDATE_CHECKING;
 // GitHub request, redirect and CA verification use the real host libcurl.
 assert(curl_global_init(CURL_GLOBAL_DEFAULT)==0);
 Transfer t={0};assert(fetch("https://api.github.com/repos/" UPDATE_REPOSITORY "/releases/latest",&t));
 assert(Update_ParseRelease(t.data,t.size,false,false,&release)==1);free(t.data);
 assert(!strcmp(release.version,"v3.0"));
 status.state=UPDATE_DOWNLOADING;t=(Transfer){.file=fopen(UPDATE_PART,"wb"),.expected=release.size};assert(t.file);
 assert(fetch(release.url,&t));assert(!fclose(t.file));assert(t.size==release.size);assert(verify_file());
 // A wrong app or insufficient space must never start an install transaction.
 wrong_title=true;assert(!install_cia()&&starts==0);wrong_title=false;
 no_space=true;assert(!install_cia()&&starts==0);no_space=false;
 short_write=true;assert(!install_cia()&&starts==1&&cancels==1&&!finishes);short_write=false;
 written=0;assert(install_cia()&&finishes==1&&written==release.size);
 cancel=true;assert(!verify_file());assert(!install_cia());cancel=false;
 FILE*f=fopen(UPDATE_PART,"r+b");assert(f);fputc(42,f);fclose(f);assert(!verify_file());
 f=fopen(UPDATE_PART,"wb");assert(f);fwrite("3DSXtest",1,8,f);fclose(f);assert(!verify_file());
 Transfer limited={.expected=4,.file=tmpfile()};assert(!receive("12345",1,5,&limited));fclose(limited.file);
 Transfer memory={0};cancel=true;assert(!receive("123",1,3,&memory));cancel=false;
 strcpy(launch_file,"sdmc:/test.3dsx");f=fopen(launch_file,"wb");fwrite("OLD",1,3,f);fclose(f);
 assert(install_3dsx());f=fopen(launch_file,"rb");char b[8];assert(fread(b,1,8,f)==8&&!memcmp(b,"3DSXtest",8));fclose(f);
 curl_global_cleanup();puts("PASS: live GitHub HTTPS/redirect/download/SHA256; corruption/truncation/cancel/size; title/space/short-write/commit guards; 3DSX replacement. AM is mocked, not console-tested.");
}
