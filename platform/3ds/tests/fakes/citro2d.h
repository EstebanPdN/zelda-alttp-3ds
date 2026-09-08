#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { uint8_t rgba[4]; } TestTexture;
typedef struct { TestTexture *tex; } C2D_Image;
typedef struct { int unused; } C2D_DrawParams;
typedef struct { uint32_t clear; } C3D_RenderTarget;
enum { C3D_CLEAR_COLOR=1 };
void C3D_FrameSplit(unsigned);
void C3D_RenderTargetClear(C3D_RenderTarget *,unsigned,uint32_t,uint32_t);
typedef struct { int source[3], operand[3], function; uint32_t color; } C3D_TexEnv;
enum { C3D_RGB=1, C3D_Alpha=2, C3D_Both=3 };
enum { GPU_TEXTURE0=1, GPU_CONSTANT, GPU_PREVIOUS };
enum { GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_ALPHA };
enum { GPU_REPLACE, GPU_MODULATE, GPU_MULTIPLY_ADD };
#define C2D_Color32(r,g,b,a) ((uint32_t)(r) | (uint32_t)(g)<<8 | (uint32_t)(b)<<16 | (uint32_t)(a)<<24)
C3D_TexEnv *C3D_GetTexEnv(unsigned);
void C3D_TexEnvInit(C3D_TexEnv *);
void C3D_TexEnvSrc(C3D_TexEnv *,int,int,int,int);
void C3D_TexEnvOpRgb(C3D_TexEnv *,int,int,int);
void C3D_TexEnvFunc(C3D_TexEnv *,int,int);
void C3D_TexEnvColor(C3D_TexEnv *,uint32_t);
void C2D_Flush(void);
bool C2D_DrawImage(C2D_Image,const C2D_DrawParams *,const void *);
