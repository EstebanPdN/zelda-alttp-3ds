#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef bool (*FileMenuRectFn)(void *context, int sx, int sy,
                              int x, int y, int width, int height);

// File menus have an unobstructed 16-pixel column on their left edge. Repeat
// that column across the side margins. The top/bottom gaps use its 16x16
// corner, with the same phase as the central menu. Every rectangle is 1:1.
// No game graphics are stored here: sample the current rendered texture so
// palette changes, translations and fades remain synchronized on both GPUs.
static inline bool FileMenu_DrawBackground(int source_width, int source_height,
                                           FileMenuRectFn draw, void *context) {
  if (source_width < 256 || source_height < 224) return false;
  int source_x = (source_width - 256) / 2;
  int menu_y = (240 - source_height) / 2;
  if (menu_y < 0) menu_y = 0;
  for (int side = 0; side < 2; side++) {
    int end = side ? 400 : 72;
    for (int x = side ? 328 : 0; x < end;) {
      int phase = (x - 72 + 400) % 16;
      int width = 16 - phase;
      if (width > end - x) width = end - x;
      if (!draw(context, source_x + phase, 0, x, menu_y, width, 224)) return false;
      x += width;
    }
  }
  for (int band = 0; band < 2; band++) {
    int end_y = band ? 240 : menu_y;
    for (int y = band ? menu_y + 224 : 0; y < end_y;) {
      int phase_y = (y - menu_y + 240) % 16;
      int height = 16 - phase_y;
      if (height > end_y - y) height = end_y - y;
      for (int x = 0; x < 400;) {
        int phase_x = (x - 72 + 400) % 16;
        int width = 16 - phase_x;
        if (width > 400 - x) width = 400 - x;
        if (!draw(context, source_x + phase_x, phase_y,
                  x, y, width, height)) return false;
        x += width;
      }
      y += height;
    }
  }
  return draw(context, source_x, 0, 72, menu_y, 256, 224);
}

// The native plaque starts at tile (2,2) on the file-menu BG. Its right
// cap is the horizontally flipped left-cap tile. Read the live tilemap:
// translated cartridges have wider plaques than the 20-tile USA title.
static inline int FileMenu_TitleWidth(const uint16_t *vram) {
  // The Japanese cartridge has no plaque or panel to widen.
  if (vram[0x1042] == 0x007f) return 0;
  if (vram[0x1042] == 0x3589) {
    for (int column=3; column<32; column++)
      if (vram[0x1040+column] == 0x7589)
        return (column-1)*8;
  }
  return 160;
}

// The file panel gains 144 pixels without changing any glyph, fairy or heart.
// The title remains a single native plaque, centered above the expanded panel.
// Copy/erase prompts stay intact; the three player rows place names on the
// left and health on the right. Naming keeps its native scrolling keyboard.
static inline bool FileMenu_DrawWide(int source_width, int source_height,
                                    unsigned module, int title_width, FileMenuRectFn draw, void *context) {
  if (source_width != 400 || source_height < 224 || module < 1 || module > 3 ||
      title_width < 16 || title_width > 240)
    return false;
  int ox = (source_width - 256) / 2;
  int oy = (240 - source_height) / 2;
  if (oy < 0) oy = 0;
  for (int y = 0; y < 240;) {
    int sy = (y - oy + 240) % 16;
    int h = y == oy ? 224 : 16 - sy;
    if (h > 240 - y) h = 240 - y;
    for (int x = 0; x < 400;) {
      int sx = (x - 72 + 400) % 16, w = 16 - sx;
      if (w > 400 - x) w = 400 - x;
      if (!draw(context, ox + sx, sy, x, y, w, h)) return false;
      x += w;
    }
    y += h;
  }
  if (!draw(context, ox + 16, 16, (400-title_width)/2, oy + 16, title_width, 32) ||
      !draw(context, ox + 16, 56, 16, oy + 56, 8, 160) ||
      !draw(context, ox + 232, 56, 376, oy + 56, 8, 160)) return false;
  for (int x = 24; x < 376; x += 8) {
    if (!draw(context, ox + 24, 56, x, oy + 56, 8, 8) ||
        !draw(context, ox + 24, 208, x, oy + 208, 8, 8)) return false;
  }
  // The copy destination panel occupies native x=224 at the top. Repeat only
  // the 16x48 black patch BELOW it (x=216,y=160): clear of names, maximum
  // hearts, fairies and every translated action label. This remains 1:1 and
  // under the existing 240-quad limit, including confirmation substates.
  for (int y = 64; y < 208; y += 48) {
    for (int x = 24; x < 376; x += 16)
      if (!draw(context, ox + 216, 160, x, oy + y, 16, 48)) return false;
  }
  if (module == 1) {
    return draw(context, ox + 24, 64, 24, oy + 64, 112, 104) &&
           draw(context, ox + 136, 64, 280, oy + 64, 96, 104) &&
           // Keep the fairy in its player-row column; align action text
           // (native x=48) with the slot numbers (x=72).
           draw(context, ox + 24, 168, 24, oy + 168, 24, 40) &&
           draw(context, ox + 48, 168, 72, oy + 168, 184, 40);
  }
  return draw(context, ox + 24, 64, 96, oy + 64, 208, 144);
}
