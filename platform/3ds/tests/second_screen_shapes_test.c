#include <stdint.h>
#include <stdio.h>
#include "src/platform/linux/second_screen_shapes.h"

static void OldRound(SDL_Renderer *r, float x, float y, float w, float h, float rad) {
  if (rad > w / 2) rad = w / 2; if (rad > h / 2) rad = h / 2;
  SDL_FRect mid = {x, y + rad, w, h - 2 * rad}; SDL_RenderFillRectF(r, &mid);
  for (int i = 0; i < (int)rad; i++) {
    float dy = rad - i, dx = rad - sqrtf(rad * rad - dy * dy);
    SDL_FRect t = {x + dx, y + i, w - 2 * dx, 1};
    SDL_FRect b = {x + dx, y + h - 1 - i, w - 2 * dx, 1};
    SDL_RenderFillRectF(r, &t); SDL_RenderFillRectF(r, &b);
  }
}
int main(int argc, char **argv) {
  SDL_Init(0);
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 640, 240, 16, SDL_PIXELFORMAT_RGB565);
  SDL_Renderer *r = SDL_CreateSoftwareRenderer(s);
  if (!s || !r) return 2;
  unsigned cases = 0;
  for (int fx = 0; fx < 8; fx++) for (int fy = 0; fy < 8; fy++)
    for (int radius = 0; radius < 12; radius++) {
      float x = 10 + fx / 8.0f, y = 10 + fy / 8.0f;
      float w = 23.25f, h = 21.75f, rad = radius / 2.0f;
      SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
      SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
      SecondScreenFillRound(r, x, y, w, h, rad); SDL_RenderPresent(r);
      int l = (int)floorf(x + .5f), t = (int)floorf(y + .5f);
      int right = (int)floorf(x + w + .5f), bottom = (int)floorf(y + h + .5f);
      for (int yy = t; yy < bottom; yy++) {
        const uint16_t *row = (uint16_t *)((uint8_t *)s->pixels + yy * s->pitch);
        const uint16_t *mirror = (uint16_t *)((uint8_t *)s->pixels + (bottom-1-(yy-t)) * s->pitch);
        // Every row reaches the center: no gap between caps and the middle.
        if (!row[(l + right) / 2]) { fprintf(stderr, "FAIL missing row %d\n", yy); return 1; }
        for (int xx = l; xx < right; xx++)
          if (row[xx] != row[right-1-(xx-l)] || row[xx] != mirror[xx]) {
            fputs("FAIL asymmetric rounded rectangle\n", stderr); return 1;
          }
      }
      cases++;
    }
  // Render E6 on the left and E7 on the right with identical fractional grid
  // positions/radii. The inner fill must not expose a stray gold scanline.
  SDL_SetRenderDrawColor(r, 12, 12, 12, 255); SDL_RenderClear(r);
  for (int variant = 0; variant < 2; variant++) for (int i = 0; i < 20; i++) {
    float x = 35 + (i % 5) * 50.375f + variant * 320, y = 22 + (i / 5) * 48.625f;
    SDL_SetRenderDrawColor(r, 232, 194, 96, 255);
    if (variant) SecondScreenFillRound(r, x + 2, y + 2, 34.375f, 34.375f, 5);
    else OldRound(r, x + 2, y + 2, 34.375f, 34.375f, 5);
    SDL_SetRenderDrawColor(r, 46, 40, 16, 255);
    if (variant) SecondScreenFillRound(r, x + 4, y + 4, 30.375f, 30.375f, 3.5f);
    else OldRound(r, x + 4, y + 4, 30.375f, 30.375f, 3.5f);
  }
  SDL_RenderPresent(r);
  if (argc > 1 && SDL_SaveBMP(s, argv[1])) return 2;
  printf("PASS %u fractional position/radius combinations: continuous, symmetric fills\n", cases);
  SDL_DestroyRenderer(r); SDL_FreeSurface(s); SDL_Quit();
  return 0;
}
