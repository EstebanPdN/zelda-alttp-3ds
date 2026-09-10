#!/usr/bin/env python3
"""Exercise actual libctru archive rename with generic versus SD error results."""
from pathlib import Path
import subprocess,hashlib,argparse,tempfile
args=argparse.ArgumentParser()
args.add_argument('archive_dev',type=Path,help='libctru v2.7.0 source/archive_dev.c')
src=args.parse_args().archive_dev.read_text()
a=src.index('static int\narchive_rename(');b=src.index('\n/*! Create a directory',a)
fn=src[a:b]
code=r'''
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
typedef int32_t Result;
typedef struct {unsigned size;const void *data;} FS_Path;
typedef struct {bool is_extdata;int archive;} archive_fsdevice;
struct _reent {int _errno;void *deviceData;};
static archive_fsdevice device={0,1};
#define R_FAILED(x) ((x)<0)
#define R_SUCCEEDED(x) ((x)>=0)
#define R_DESCRIPTION(x) ((x)&1023)
#define RD_ALREADY_EXISTS (0x3ff-3)
static bool exists=true;static Result exists_code;static int deletions;
static FS_Path archive_utf16path(struct _reent *r,const char *name,archive_fsdevice **dev){*dev=&device;return (FS_Path){strlen(name)+1,name};}
static Result FSUSER_RenameFile(int a,FS_Path old,int b,FS_Path next){return exists?exists_code:0;}
static Result FSUSER_RenameDirectory(int a,FS_Path old,int b,FS_Path next){return (Result)0xC92044FA;}
static Result FSUSER_DeleteFile(int a,FS_Path path){exists=false;deletions++;return 0;}
static Result FSUSER_DeleteDirectory(int a,FS_Path path){return -1;}
static int archive_translate_error(Result rc){return rc;}
'''+fn+r'''
int main(void){struct _reent r={0,&device};
 exists_code=(Result)0xC82044BE;
 assert(R_DESCRIPTION(exists_code)==190 && RD_ALREADY_EXISTS==1020);
 assert(archive_rename(&r,"config.tmp","config.ini")==-1);
 assert(deletions==0 && exists);
 puts("PASS actual libctru 2.7 archive_rename: SD-specific existing-target result 0xC82044BE rejects replacement; generic overwrite branch is not taken");
 exists_code=(Result)0xC82047FC;
 assert(archive_rename(&r,"config.tmp","config.ini")==0);
 assert(deletions==1 && !exists);
 puts("PASS generic RD_ALREADY_EXISTS takes delete/retry; result-code difference explains why a host/emulator overwrite test can pass");
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-libctru-rename-') as t:
 p=Path(t)/'test.c';p.write_text(code);exe=Path(t)/'test'
 subprocess.run(['cc',p,'-o',exe],check=True)
 subprocess.run([exe],check=True)
print('Reference file SHA256',hashlib.sha256(src.encode()).hexdigest())
