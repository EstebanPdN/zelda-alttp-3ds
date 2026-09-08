#ifndef ZELDA3_SECOND_SCREEN_SHAPES_H_
#define ZELDA3_SECOND_SCREEN_SHAPES_H_

#include <SDL.h>
#include <math.h>

// Snap the outer edges once, then fill disjoint integer scanlines. Mixing
// floor(radius) cap rows with a fractional center rectangle left a one-pixel
// seam at radius 3.5 (the selected item's inner border at 3DS scale).
static void SecondScreenFillRound(SDL_Renderer *renderer,
    float x, float y, float w, float h, float radius) {
  int left = (int)floorf(x + 0.5f), top = (int)floorf(y + 0.5f);
  int width = (int)floorf(x + w + 0.5f) - left;
  int height = (int)floorf(y + h + 0.5f) - top;
  if (width <= 0 || height <= 0) return;
  int r = (int)floorf(radius + 0.5f);
  if (r < 0) r = 0;
  if (r > width / 2) r = width / 2;
  if (r > height / 2) r = height / 2;
  SDL_Rect spans[32];
  int count = 0;
  if (height > 2 * r)
    spans[count++] = (SDL_Rect){left, top + r, width, height - 2 * r};
  for (int row = 0; row < r; row++) {
    float dy = r - row - 0.5f;
    int inset = (int)ceilf(r - sqrtf((float)r * r - dy * dy) - 0.5f);
    if (count + 2 > 32) {
      SDL_RenderFillRects(renderer, spans, count);
      count = 0;
    }
    spans[count++] = (SDL_Rect){left + inset, top + row, width - 2 * inset, 1};
    spans[count++] = (SDL_Rect){left + inset, top + height - 1 - row, width - 2 * inset, 1};
  }
  if (count) SDL_RenderFillRects(renderer, spans, count);
}
#endif
