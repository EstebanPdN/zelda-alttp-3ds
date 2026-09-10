#include "update_manifest.h"
#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static json_t *make(const char *tag, bool pre, bool homebrew) {
  char url[512], name[100]; const char *v=tag[0]=='v'?tag+1:tag;
  snprintf(name,sizeof(name),"zelda3-3ds-v%s.%s",v,homebrew?"3dsx":"cia");
  snprintf(url,sizeof(url),"https://github.com/" UPDATE_REPOSITORY "/releases/download/%s/%s",tag,name);
  return json_pack("{s:s,s:b,s:b,s:[{s:s,s:s,s:s,s:i}]}","tag_name",tag,"draft",0,"prerelease",pre,"assets",
    "name",name,"browser_download_url",url,"digest","sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","size",5000000);
}
static int parse(json_t *r,bool pre,bool hb,UpdateRelease *out) {
 char *s=json_dumps(r,JSON_COMPACT);int result=Update_ParseRelease(s,strlen(s),pre,hb,out);free(s);return result;
}
int main(int argc,char **argv) {
 assert(Update_IsNewer("v3.1","3.1-E1"));assert(Update_IsNewer("v3.1-E10","3.1-E9"));
 assert(Update_IsNewer("v3.10","3.9"));assert(!Update_IsNewer("v3.1-E99","3.1"));
 assert(!Update_IsNewer("v3.0","3.1-E1"));assert(!Update_IsNewer("v3.1-E1","3.1-E1"));
 assert(!Update_ValidVersion("3.1/../evil"));assert(!Update_ValidVersion("3..1"));
 assert(!Update_ValidVersion("3.1-E99999999"));assert(!Update_ValidVersion("3.1.2.3"));
 char lines[384][43];
 unsigned count=Update_FormatNotes("## Changelog\n\n- **Fixed** [world colors](https://example.com).\n![QR](https://example.com/qr.png)",lines,384);
 assert(count==3 && !strcmp(lines[0],"Changelog") && !lines[1][0] && !strcmp(lines[2],"- Fixed world colors."));
 assert(Update_FormatNotes("",lines,384)==1 && !strcmp(lines[0],"No changelog provided."));
 assert(Update_FormatNotes("ignored",lines,0)==0);
 char long_notes[12289];memset(long_notes,'x',12288);long_notes[12288]=0;
 count=Update_FormatNotes(long_notes,lines,384);assert(count==293);
 for(unsigned i=0;i<count;i++)assert(strlen(lines[i])<=42);
 assert(Update_FormatNotes(long_notes,lines,2)==2);
 UpdateRelease out;json_t *r=make("v3.1",false,false);assert(parse(r,false,false,&out)==1);
 assert(!strcmp(out.version,"v3.1")&&out.size==5000000);assert(parse(r,false,true,&out)==-1);
 json_t *a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"digest",json_string("sha256:abc"));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"browser_download_url",json_string("https://evil.example/zelda.cia"));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);a=json_array_get(json_object_get(r,"assets"),0);
 json_object_set_new(a,"size",json_integer(UPDATE_MAX_FILE+1));assert(parse(r,false,false,&out)==-1);
 json_decref(r);r=make("v3.1",false,false);json_object_set_new(r,"draft",json_true());assert(parse(r,false,false,&out)==-1);json_decref(r);
 r=json_array();assert(parse(r,true,false,&out)==0);
 json_array_append_new(r,make("v3.2-E2",true,false));json_array_append_new(r,make("v3.2-E10",true,false));
 json_array_append_new(r,make("v3.3",false,false));assert(parse(r,true,false,&out)==1&&!strcmp(out.version,"v3.2-E10"));json_decref(r);
 r=make("v3.1",false,true);assert(parse(r,false,true,&out)==1);json_decref(r);
 assert(Update_ParseRelease("{}garbage",9,false,false,&out)==-1);
 assert(Update_ParseRelease("{\"draft\":false,\"draft\":true}",28,false,false,&out)==-1);
 for(int i=1;i<argc;i++) {
   FILE *f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
   char *data=malloc(n);assert(fread(data,1,n,f)==(size_t)n);fclose(f);
   bool pre=i==2;int result=Update_ParseRelease(data,n,pre,false,&out);free(data);
   assert(result>=0);printf("Live %s response: %d %s\n",pre?"pre-release":"stable",result,out.version);
 }
 puts("PASS: version order, channels, assets, malformed JSON, digest/size/URL/draft guards, live GitHub responses");
}
