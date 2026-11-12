#pragma once
#include <stdint.h>
#include "generated_ui_sprites.h"
#include "generated_ui_japanese.h"
#include "ui_font_cjk.h"
#include "../../ui_strings.h"  // UI_GetLanguage: which wide table wins

// Deterministic Latin and Cyrillic UI glyphs. Decode once per character; accented letters
// use the same editable 5x7 base and an explicit accent, never byte fragments.
enum UiFontAccent { UI_ACUTE, UI_GRAVE, UI_CEDILLA, UI_CIRCUMFLEX, UI_TILDE, UI_DIAERESIS };
enum { UI_FONT_JAPANESE = -2, UI_FONT_JAPANESE_CODE = 0x100 };
// Chinese reuses the same "blank base, wide code" shape over its own tables.
enum { UI_FONT_CHINESE = -3, UI_FONT_CHINESE_CODE = 0x400 };
static inline int UiFontJapaneseIndex(unsigned cp) {
  int lo = 0, hi = kUiJapaneseGlyphCount - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (kUiJapaneseCodepoints[mid] == cp) return mid;
    if (kUiJapaneseCodepoints[mid] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}
// Japanese reports a blank with *accent == UI_FONT_JAPANESE; UiFontNextCode gives its glyph.
static inline char UiFontNext(const char **text, int *accent) {
  const unsigned char *p = (const unsigned char *)*text;
  *accent = -1;
  if (!*p) return 0;
  (*text)++;
  if (*p < 128) return *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
  if ((*p == 0xd0 || *p == 0xd1) && p[1] >= 0x80 && p[1] <= 0xbf) {
    unsigned cp = ((*p & 31) << 6) | (p[1] & 63);
    (*text)++;
    if (cp == 0x401 || cp == 0x451) return (char)160;
    if (cp >= 0x430 && cp <= 0x44f) cp -= 32;
    if (cp >= 0x410 && cp <= 0x42f) return (char)(128 + cp - 0x410);
    return '?';
  }
  if (*p == 0xc3 && p[1] >= 0x80 && p[1] <= 0xbf) {
    (*text)++;
    unsigned cp = p[1] + 0x40;
    if (cp >= 0xe0 && cp <= 0xfe && cp != 0xf7) cp -= 32;
    switch (cp) {
    case 0xc0: *accent=UI_GRAVE; return 'A';
    case 0xc1: *accent=UI_ACUTE; return 'A';
    case 0xc2: *accent=UI_CIRCUMFLEX; return 'A';
    case 0xc3: *accent=UI_TILDE; return 'A';
    case 0xc4: *accent=UI_DIAERESIS; return 'A';
    case 0xc7: *accent=UI_CEDILLA; return 'C';
    case 0xc8: *accent=UI_GRAVE; return 'E';
    case 0xc9: *accent=UI_ACUTE; return 'E';
    case 0xca: *accent=UI_CIRCUMFLEX; return 'E';
    case 0xcb: *accent=UI_DIAERESIS; return 'E';
    case 0xcc: *accent=UI_GRAVE; return 'I';
    case 0xcd: *accent=UI_ACUTE; return 'I';
    case 0xce: *accent=UI_CIRCUMFLEX; return 'I';
    case 0xcf: *accent=UI_DIAERESIS; return 'I';
    case 0xd1: *accent=UI_TILDE; return 'N';
    case 0xd2: *accent=UI_GRAVE; return 'O';
    case 0xd3: *accent=UI_ACUTE; return 'O';
    case 0xd4: *accent=UI_CIRCUMFLEX; return 'O';
    case 0xd5: *accent=UI_TILDE; return 'O';
    case 0xd6: *accent=UI_DIAERESIS; return 'O';
    case 0xd9: *accent=UI_GRAVE; return 'U';
    case 0xda: *accent=UI_ACUTE; return 'U';
    case 0xdb: *accent=UI_CIRCUMFLEX; return 'U';
    case 0xdc: *accent=UI_DIAERESIS; return 'U';
    default: return '?';
    }
  }
  if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
    unsigned cp = ((*p & 15u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u);
    int zh = UiCjk_Index(cp), ja = UiFontJapaneseIndex(cp);
    // A Han character can exist in both tables (回, 直, 骨...), and the two
    // tables differ in size: 12 px against 10 at scale 3. The language on
    // screen picks its own table, so one line never mixes both.
    if (zh >= 0 && (ja < 0 || UI_GetLanguage() == kUiLanguage_Chinese)) {
      *text += 2;
      *accent = UI_FONT_CHINESE;
      return ' ';
    }
    if (ja >= 0) {
      *text += 2;
      *accent = UI_FONT_JAPANESE;
      return ' ';
    }
  }
  unsigned remaining = *p >= 0xf0 && *p <= 0xf4 ? 3 : *p >= 0xe0 && *p <= 0xef ? 2 : *p >= 0xc2 && *p <= 0xdf ? 1 : 0;
  while (remaining-- && **text && ((unsigned char)**text & 0xc0) == 0x80) (*text)++;
  return '?';
}

static inline unsigned UiFontNextCode(const char **text) {
  const unsigned char *p=(const unsigned char *)*text;
  int accent; char base=UiFontNext(text,&accent);
  if (accent==UI_FONT_JAPANESE)
    return UI_FONT_JAPANESE_CODE+UiFontJapaneseIndex(((p[0]&15u)<<12)|((p[1]&63u)<<6)|(p[2]&63u));
  if (accent==UI_FONT_CHINESE)
    return UI_FONT_CHINESE_CODE+UiCjk_Index(((p[0]&15u)<<12)|((p[1]&63u)<<6)|(p[2]&63u));
  if (accent>=0) for (unsigned i=0;i<sizeof(kNativeUiAccents)/sizeof(kNativeUiAccents[0]);i++)
    if(kNativeUiAccents[i].base==base && kNativeUiAccents[i].accent==accent) return kNativeUiAccents[i].code;
  return (unsigned char)base;
}

// One description of a glyph for every drawer. Japanese has the same size at scales 1 and 3,
// and a larger face at scale 2, instead of a pixel-doubled one.
static inline int UiFontGlyphRows(unsigned code, int scale) {
  if (code>=UI_FONT_CHINESE_CODE) return UiCjk_SizeForScale(scale);
  if (code>=UI_FONT_JAPANESE_CODE) return scale==2?12:10;
  return scale==3?15:scale==2?21:11;
}
static inline int UiFontGlyphColumns(unsigned code, int scale) {
  if (code>=UI_FONT_CHINESE_CODE) return UiCjk_SizeForScale(scale);
  if (code>=UI_FONT_JAPANESE_CODE) return scale==2?12:10;
  return scale==3?7:5*scale;
}
static inline int UiFontGlyphTop(unsigned code, int scale) {
  if (code>=UI_FONT_CHINESE_CODE) return UiCjk_TopOffset(scale, UiCjk_SizeForScale(scale));
  if (code>=UI_FONT_JAPANESE_CODE) return scale==2?0:scale==3?-1:-2;
  return scale==3?-3:-2*scale;
}
static inline unsigned UiFontGlyphBits(unsigned code, int scale, int row) {
  if (code>=UI_FONT_CHINESE_CODE) {
    const uint16_t *rows=UiCjk_Rows((int)(code-UI_FONT_CHINESE_CODE), UiCjk_SizeForScale(scale));
    return rows ? rows[row] : 0;
  }
  if (code>=UI_FONT_JAPANESE_CODE) {
    unsigned i=code-UI_FONT_JAPANESE_CODE;
    return scale==2?kUiJapaneseGlyphs12[i][row]:kUiJapaneseGlyphs10[i][row];
  }
  return scale==3?kNativeUiMediumGlyphs[code][row]:scale==2?kNativeUiLargeGlyphs[code][row]:kNativeUiGlyphs[code][row];
}
static inline int UiFontAdvance(unsigned code, int scale) {
  if (code>=UI_FONT_CHINESE_CODE) return UiCjk_SizeForScale(scale);
  if (code>=UI_FONT_JAPANESE_CODE) return scale==2?13:11;
  return scale==3?(code==' '?5:8):(code==' '?4:6)*scale;
}

static inline int UiFontWidth(const char *text, int scale) {
  int width=0;
  unsigned code;
  while ((code=UiFontNextCode(&text))) width += UiFontAdvance(code,scale);
  return width;
}

// A line must not begin with closing punctuation or a small kana.
static inline int UiFontNoLineStart(const char *text) {
  static const unsigned kMarks[]={0x3001,0x3002,0x30fc,0x3063,0x30c3,0x3083,0x3085,0x3087,0x30e3,0x30e5,0x30e7,
    0x3041,0x3043,0x3045,0x3047,0x3049,0x30a1,0x30a3,0x30a5,0x30a7,0x30a9,0xff01,0xff1f,0xff09,0x300d,0x30fb};
  const unsigned char *p=(const unsigned char *)text;
  if ((*p&0xf0)!=0xe0) return 0;
  unsigned cp=((p[0]&15u)<<12)|((p[1]&63u)<<6)|(p[2]&63u);
  for (unsigned i=0;i<sizeof(kMarks)/sizeof(kMarks[0]);i++) if (kMarks[i]==cp) return 1;
  return 0;
}
// Wrapping: an ASCII or full-width space is a break; Japanese breaks anywhere otherwise,
// but a clause break made with a full-width space is kept.
static inline int UiFontIsWideSpace(const char *p) {
  return (unsigned char)p[0]==0xe3 && (unsigned char)p[1]==0x80 && (unsigned char)p[2]==0x80;
}
static inline const char *UiFontBreakAt(const char *p, const char *last) {
  if (*p==' ' || UiFontIsWideSpace(p)) return p;
  return (unsigned char)*p>=0xe0 && last && *last==' ' ? NULL : last;
}
// A line must not begin with closing punctuation; Chinese keeps its marks with
// the character before them, like Japanese.
static inline int UiFontCjkNoLineStart(const char *p) {
  static const unsigned kMarks[]={0x3001,0x3002,0xff01,0xff1f,0xff09,0x300d,0x300f,0x3011,0xff0c,0xff1a,0xff1b,0x2026};
  const unsigned char *b=(const unsigned char *)p;
  if ((*b&0xf0)!=0xe0) return 0;
  unsigned cp=((b[0]&15u)<<12)|((b[1]&63u)<<6)|(b[2]&63u);
  for (unsigned i=0;i<sizeof(kMarks)/sizeof(kMarks[0]);i++) if (kMarks[i]==cp) return 1;
  return 0;
}
static inline const char *UiFontSkipBreak(const char *p) {
  for (;;) {
    if (*p==' ') p++;
    else if (UiFontIsWideSpace(p)) p+=3;
    else return p;
  }
}
// Where a wrapped line stops: at the last space of Latin text, anywhere between Japanese.
static inline const char *UiFontLineEnd(const char *start, const char *end, const char *last_space) {
  if (*end && last_space) return last_space;
  if (*end && end>start && (UiFontNoLineStart(end) || UiFontCjkNoLineStart(end)) &&
      (unsigned char)end[-1]>=0x80) {
    const char *back=end-1;
    while (back>start && ((unsigned char)*back&0xc0)==0x80) back--;
    if (back>start) return back;
  }
  return end;
}

// Center the actual ink, including accents/descenders, rather than the font's
// padded texture rectangle. draw_tiny_text positions that rectangle at y-2*scale.
static inline int UiFontCenteredY(const char *text, int scale, int top, int height) {
  int first=10000,last=-10000;
  unsigned code;
  while ((code=UiFontNextCode(&text))) {
    int rows=UiFontGlyphRows(code,scale);
    for(int y=0;y<rows;y++) {
      if(!UiFontGlyphBits(code,scale,y)) continue;
      int a=UiFontGlyphTop(code,scale)+y;
      int b=a;
      if(a<first)first=a;if(b>last)last=b;
    }
  }
  return first>last ? top : top+(height-(last-first+1))/2-first;
}
