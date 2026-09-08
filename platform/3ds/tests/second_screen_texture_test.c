#include <stdio.h>
#include <string.h>
#include "src/platform/linux/second_screen_texture.h"

int main(void) {
  if (SDL_Init(0)) return 2;
  SDL_Surface *surface[2]; SDL_Renderer *renderer[2];
  SDL_Texture *texture[2];
  static uint32_t pixels[512 * 512];
  static uint16_t output[2][320 * 240];
  for (unsigned i = 0; i < 512 * 512; i++)
    pixels[i] = 0xff000000u | (i * 3571u & 0xffffff);
  for (int variant = 0; variant < 2; variant++) {
    surface[variant] = SDL_CreateRGBSurfaceWithFormat(0, 320, 240, 16, SDL_PIXELFORMAT_RGB565);
    if (!surface[variant]) return 2;
    renderer[variant] = SDL_CreateSoftwareRenderer(surface[variant]);
    if (!renderer[variant]) return 2;
    texture[variant] = SecondScreenCreateTexture(renderer[variant], 512, 512, pixels, false, variant);
    if (!texture[variant]) return 2;
    Uint32 format; SDL_QueryTexture(texture[variant], &format, NULL, NULL, NULL);
    if (variant && format != SDL_PIXELFORMAT_RGB565) return 1;
  }
  const SDL_FRect cases[] = {
    {0, 0, 320, 240}, {-73.5f, -42.0f, 512, 512},
    {13.5f, 24.5f, 189.0f, 137.0f}, {0, 0, 1024, 1024}
  };
  for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
    for (int v = 0; v < 2; v++) {
      SDL_SetRenderDrawColor(renderer[v], 0, 0, 0, 255);
      SDL_RenderClear(renderer[v]);
      if (SDL_RenderCopyF(renderer[v], texture[v], NULL, &cases[c])) return 2;
      if (SDL_RenderReadPixels(renderer[v], NULL, SDL_PIXELFORMAT_RGB565, output[v], 640)) return 2;
    }
    if (memcmp(output[0], output[1], sizeof(output[0]))) {
      fprintf(stderr, "FAIL opaque texture case %u\n", c); return 1;
    }
  }
  // Transparent sheets must retain all 8-bit alpha values and ARGB storage.
  SDL_Texture *alpha = SecondScreenCreateTexture(renderer[1], 512, 512, pixels, true, true);
  Uint32 format; SDL_QueryTexture(alpha, &format, NULL, NULL, NULL);
  if (format != SDL_PIXELFORMAT_ARGB8888) return 1;
  SDL_DestroyTexture(alpha);
  for (int v = 0; v < 2; v++) {
    Uint64 start = SDL_GetPerformanceCounter();
    for (int i = 0; i < 1000; i++) {
      SDL_RenderCopyF(renderer[v], texture[v], NULL, &cases[0]);
      SDL_RenderPresent(renderer[v]);
    }
    printf("HOST opaque redraw %s %.3f us\n", v ? "E7" : "E6",
      (SDL_GetPerformanceCounter() - start) * 1e6 / SDL_GetPerformanceFrequency() / 1000);
    SDL_DestroyTexture(texture[v]); SDL_DestroyRenderer(renderer[v]); SDL_FreeSurface(surface[v]);
  }
  SDL_Quit(); puts("PASS RGB565 opaque scaling/clipping parity; alpha kept as ARGB8888");
  return 0;
}
