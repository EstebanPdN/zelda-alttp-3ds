#ifndef ZELDA3_SECOND_SCREEN_TEXTURE_H_
#define ZELDA3_SECOND_SCREEN_TEXTURE_H_

#include <SDL.h>
#include <stdbool.h>
#include <stdlib.h>

// Preconvert opaque art once for the Old 3DS RGB565 software target. SDL's
// nearest-neighbor RGB565 blitter can then copy/scale without converting every
// map/background pixel on each redraw. Alpha sheets retain ARGB8888.
static SDL_Texture *SecondScreenCreateTexture(SDL_Renderer *renderer,
    int w, int h, const void *pixels, bool blend, bool prefer_rgb565) {
  SDL_Texture *texture = NULL;
  if (prefer_rgb565 && !blend) {
    uint16_t *converted = malloc((size_t)w * h * sizeof(*converted));
    if (converted) {
      if (SDL_ConvertPixels(w, h, SDL_PIXELFORMAT_ARGB8888, pixels, w * 4,
                            SDL_PIXELFORMAT_RGB565, converted, w * 2) == 0) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                    SDL_TEXTUREACCESS_STATIC, w, h);
        if (texture && SDL_UpdateTexture(texture, NULL, converted, w * 2) != 0) {
          SDL_DestroyTexture(texture);
          texture = NULL;
        }
      }
      free(converted);
    }
  }
  if (!texture) {
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) return NULL;
    if (SDL_UpdateTexture(texture, NULL, pixels, w * 4) != 0) {
      SDL_DestroyTexture(texture);
      return NULL;
    }
  }
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
  SDL_SetTextureBlendMode(texture, blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
  return texture;
}
#endif
