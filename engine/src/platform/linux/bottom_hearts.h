#ifndef ZELDA3_BOTTOM_HEARTS_H_
#define ZELDA3_BOTTOM_HEARTS_H_

// Old-only retained heart layer. Worker captures the underlying pixels before
// drawing hearts; main owns a separate display copy and never touches SDL or
// worker buffers on a damage/healing update. HUD glyph alpha is binary (the
// SS_DrawTile palette emits either transparent or opaque pixels).
typedef struct BottomHearts {
  bool valid;
  int count, size, health, capacity, half_magic;
  SDL_Rect rect[20];
  uint16_t under[20][16 * 16];
} BottomHearts;

static uint32_t bottom_heart_glyphs[3][64]; // empty, half, full; immutable per ROM

static void BottomHearts_LoadGlyphs(const uint32_t *sheet, int stride,
                                   const int cells[3], int cols) {
  for (int g = 0; g < 3; g++)
    for (int y = 0; y < 8; y++)
      memcpy(&bottom_heart_glyphs[g][y * 8],
        sheet + (cells[g] / cols * 8 + y) * stride + cells[g] % cols * 8,
        8 * sizeof(uint32_t));
}

static void BottomHearts_Capture(BottomHearts *h, SDL_Renderer *renderer,
    int count, int size, int columns, int step, int x, int y,
    int health, int capacity, int half_magic) {
  h->valid = false;
  if (count < 1 || count > 20 || size < 1 || size > 16 || columns < 1)
    return;
  h->count = count; h->size = size; h->health = health;
  h->capacity = capacity; h->half_magic = half_magic;
  for (int i = 0; i < count; i++) {
    SDL_Rect rect = {x + i % columns * step, y + i / columns * step, size, size};
    if (rect.x < 0 || rect.y < 0 || rect.x + size > 320 || rect.y + size > 240)
      return;
    h->rect[i] = rect;
    if (SDL_RenderReadPixels(renderer, &rect, SDL_PIXELFORMAT_RGB565,
                            h->under[i], size * 2) != 0)
      return;
  }
  h->valid = true;
}

static int BottomHearts_Glyph(int health, int i) {
  return i < (health >> 3) ? 2 :
    i == (health >> 3) && (health & 7) >= 4 ? 1 : 0;
}

// Returns the number of replaced heart cells. No allocation, renderer calls,
// locks, map work, or changes outside those cells. At most 5,120 pixels.
static int BottomHearts_Apply(BottomHearts *h, uint8_t *pixels, int pitch,
                            int health) {
  if (!h->valid || !pixels) return 0;
  int changed = 0, size = h->size;
  for (int i = 0; i < h->count; i++) {
    int glyph = BottomHearts_Glyph(health, i);
    if (glyph == BottomHearts_Glyph(h->health, i)) continue;
    changed++;
    for (int y = 0; y < size; y++) {
      uint16_t *row = (uint16_t *)(pixels + (h->rect[i].y + y) * pitch) + h->rect[i].x;
      // Match SDL nearest-neighbor scaling's 16.16 center sample.
      int sy = ((0x8000 * 8 / size) + y * (0x10000 * 8 / size)) >> 16;
      for (int x = 0; x < size; x++) {
        int sx = ((0x8000 * 8 / size) + x * (0x10000 * 8 / size)) >> 16;
        uint32_t c = bottom_heart_glyphs[glyph][sy * 8 + sx];
        row[x] = c >> 24 ? ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f) : h->under[i][y * size + x];
      }
    }
  }
  h->health = health;
  return changed;
}
#endif
