#ifndef ZELDA3_BOTTOM_HEARTS_H_
#define ZELDA3_BOTTOM_HEARTS_H_

// Old-only retained heart layer. The worker draws every heart cell in all three
// states over that cell's own background and keeps the RGB565 results; main
// owns a separate display copy and never touches SDL or worker buffers on a
// damage/healing update, it only copies prepared cells. The hearts have soft
// alpha edges, so cells are rendered by SDL rather than blended here.
enum { kBottomHeartMaxSize = 12 };

typedef struct BottomHearts {
  bool valid;
  int count, size, health, capacity, half_magic;
  SDL_Rect rect[20];
  uint16_t cell[20][3][kBottomHeartMaxSize * kBottomHeartMaxSize]; // empty, half, full
} BottomHearts;

// draw(glyph, x, y) renders one heart (0 empty, 1 half, 2 full). scratch is an
// ARGB8888 BLENDMODE_NONE texture of at least size x size that puts each cell's
// background back between states; the cell is left as it was found.
static void BottomHearts_Capture(BottomHearts *h, SDL_Renderer *renderer,
    SDL_Texture *scratch, void (*draw)(int glyph, int x, int y),
    int count, int size, int columns, int step, int x, int y,
    int health, int capacity, int half_magic) {
  h->valid = false;
  if (count < 1 || count > 20 || size < 1 || size > kBottomHeartMaxSize ||
      columns < 1 || !scratch || !draw)
    return;
  h->count = count; h->size = size; h->health = health;
  h->capacity = capacity; h->half_magic = half_magic;
  SDL_Rect src = {0, 0, size, size};
  uint32_t under[kBottomHeartMaxSize * kBottomHeartMaxSize];
  for (int i = 0; i < count; i++) {
    SDL_Rect rect = {x + i % columns * step, y + i / columns * step, size, size};
    if (rect.x < 0 || rect.y < 0 || rect.x + size > 320 || rect.y + size > 240)
      return;
    h->rect[i] = rect;
    if (SDL_RenderReadPixels(renderer, &rect, SDL_PIXELFORMAT_ARGB8888, under, size * 4) != 0 ||
        SDL_UpdateTexture(scratch, &src, under, size * 4) != 0)
      return;
    for (int g = 0; g < 3; g++) {
      draw(g, rect.x, rect.y);
      if (SDL_RenderReadPixels(renderer, &rect, SDL_PIXELFORMAT_RGB565,
                              h->cell[i][g], size * 2) != 0)
        return;
      SDL_RenderCopy(renderer, scratch, &src, &rect);
    }
  }
  h->valid = true;
}

static int BottomHearts_Glyph(int health, int i) {
  return i < (health >> 3) ? 2 :
    i == (health >> 3) && (health & 7) >= 4 ? 1 : 0;
}

// Returns the number of replaced heart cells. No allocation, renderer calls,
// locks, map work, or changes outside those cells. At most 2,880 pixels.
static int BottomHearts_Apply(BottomHearts *h, uint8_t *pixels, int pitch,
                            int health) {
  if (!h->valid || !pixels) return 0;
  int changed = 0, size = h->size;
  for (int i = 0; i < h->count; i++) {
    int glyph = BottomHearts_Glyph(health, i);
    if (glyph == BottomHearts_Glyph(h->health, i)) continue;
    changed++;
    for (int y = 0; y < size; y++)
      memcpy((uint16_t *)(pixels + (h->rect[i].y + y) * pitch) + h->rect[i].x,
             &h->cell[i][glyph][y * size], size * sizeof(uint16_t));
  }
  h->health = health;
  return changed;
}
#endif
