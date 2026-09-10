#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t u32; typedef uint64_t u64; typedef int32_t Result; typedef uint32_t Handle; typedef int LightLock; typedef void *Thread;
typedef struct {uint64_t titleID;} AM_TitleInfo;
typedef struct {u32 freeClusters,clusterSize;} FS_ArchiveResource;
typedef struct {int type; const char *path;} FS_Path;
#define R_FAILED(r) ((r)<0)
#define R_SUCCEEDED(r) ((r)>=0)
#define MEDIATYPE_SD 1
#define ARCHIVE_SDMC 9
#define PATH_EMPTY 1
#define PATH_ASCII 3
#define FS_OPEN_READ 1
static inline FS_Path fsMakePath(int type,const char *path){return (FS_Path){type,path};}
static inline void LightLock_Lock(LightLock *p){} static inline void LightLock_Unlock(LightLock *p){} static inline void LightLock_Init(LightLock *p){}
bool aptShouldClose(void);bool aptIsActive(void);bool aptIsHomeAllowed(void);bool aptIsSleepAllowed(void);
void aptSetHomeAllowed(bool b);void aptSetSleepAllowed(bool b);
Result FSUSER_GetSdmcArchiveResource(FS_ArchiveResource*);Result amInit(void);void amExit(void);
Result FSUSER_OpenFileDirectly(Handle*,int,FS_Path,FS_Path,int,int);
Result AM_GetCiaFileInfo(int,AM_TitleInfo*,Handle);Result AM_GetCiaRequiredSpace(u64*,int,Handle);
Result AM_StartCiaInstallOverwrite(Handle*,int);Result FSFILE_Read(Handle,u32*,u64,void*,u32);
Result FSFILE_Write(Handle,u32*,u64,const void*,u32,u32);Result AM_FinishCiaInstall(Handle);
Result AM_CancelCIAInstall(Handle);Result FSFILE_Close(Handle);
Result acInit(void);Result ACU_GetWifiStatus(u32*);void acExit(void);
void *memalign(size_t,size_t);Result socInit(void*,u32);void socExit(void);
Thread threadCreate(void(*)(void*),void*,unsigned,int,int,bool);Result threadJoin(Thread,u64);void threadFree(Thread);bool envIsHomebrew(void);
