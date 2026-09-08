#pragma once
#include <citro2d.h>

// These targets are color-only RGB565. Clear accepts a native pixel value,
// not C2D_Color32/byte-swapped RGBA: its alpha byte would color the margins.
static inline void Platform3DS_ClearBlackTarget(C3D_RenderTarget *target) {
  C2D_Flush();
  C3D_FrameSplit(0);
  C3D_RenderTargetClear(target, C3D_CLEAR_COLOR, 0, 0);
}

static void ConfigureArgbTextureEnv(void) {
  C3D_TexEnv *env = C3D_GetTexEnv(0);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
  C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G,
                  GPU_TEVOP_RGB_SRC_COLOR,
                  GPU_TEVOP_RGB_SRC_COLOR);
  C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
  C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
  C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
  C3D_TexEnvColor(env, C2D_Color32(255, 0, 0, 255));

  env = C3D_GetTexEnv(1);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
  C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B,
                  GPU_TEVOP_RGB_SRC_COLOR,
                  GPU_TEVOP_RGB_SRC_COLOR);
  C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
  C3D_TexEnvColor(env, C2D_Color32(0, 255, 0, 255));

  env = C3D_GetTexEnv(2);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
  C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA,
                  GPU_TEVOP_RGB_SRC_COLOR,
                  GPU_TEVOP_RGB_SRC_COLOR);
  C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
  C3D_TexEnvColor(env, C2D_Color32(0, 0, 255, 255));
}

static void ConfigureRgb565TextureEnv(void) {
  C3D_TexEnv *env = C3D_GetTexEnv(0);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
  C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
  for (int i = 1; i < 3; i++)
    C3D_TexEnvInit(C3D_GetTexEnv(i));
}

// DrawImage performs Citro2D's lazy mode update, overwriting TEV stages.
// Apply our format mapping AFTER that update, then submit before another
// image, solid overlay, target or mode can overwrite it. Flush preceding
// geometry first because the texture change inside DrawImage can flush it.
static inline bool Platform3DS_DrawMappedImage(C2D_Image image,
    const C2D_DrawParams *params, void (*configure)(void)) {
  C2D_Flush();
  if (!C2D_DrawImage(image, params, NULL)) return false;
  configure();
  C2D_Flush();
  return true;
}
