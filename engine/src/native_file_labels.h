#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Use each cartridge's existing tall menu glyphs. Only the file-menu label
// cells change: frames, palette, keyboard, names, cursors and save data stay native.
static inline const char *NativeFileLabels_Word(const char *language, unsigned menu) {
  static const struct { const char *code; const char *word[4]; } labels[] = {
    {"en", {"FILE", "COPY", "ERASE", "NAME"}},
    {"es", {"PARTIDA", "COPIAR", "BORRAR", "NOMBRE"}},
    {"fr", {"PARTIE", "COPIER", "EFFACER", "NOM"}},
    {"de", {"SPIEL", "KOPIEREN", "L\x80SCHEN", "NAME"}},
    {"it", {"PARTITA", "COPIA", "ELIMINA", "NOME"}},
    {"pt", {"PARTIDA", "COPIAR", "APAGAR", "NOME"}},
    // Chief-Net's native glyph indices: ИГРА, КОПИЯ, УДАЛИТЬ, ИМЯ.
    {"ru", {"\x05\x01\x0c\xab", "\x07\x64\x0b\x05\x2a",
            "\x0f\x02\xab\x08\x05\x0e\x27", "\x05\x09\x2a"}},
  };
  if (menu >= 4) return "";
  if (language) for (unsigned i=0;i<sizeof(labels)/sizeof(labels[0]);i++)
    if (!strcmp(language,labels[i].code) || (i==2 && !strcmp(language,"fr-c")))
      return labels[i].word[menu];
  return labels[0].word[menu];
}

static inline unsigned NativeFileLabels_Glyph(unsigned char c, bool localized, bool russian) {
  if (russian) return c;
  if (c == 0x80) return 0xcd; // German Ö, as in the original LÖSCHEN label.
  if (c == 'I') return localized ? 0xe0 : 0xaf;
  if (c >= 'A' && c <= 'P') return c-'A';
  return c-'Q'+0x20;
}

static inline void NativeFileLabels_Write(uint16_t *vram, unsigned base,
                                         unsigned row, unsigned first, unsigned end,
                                         const char *word, bool localized, bool russian,
                                         bool center) {
  unsigned len=(unsigned)strlen(word);
  if (end > 32 || first >= end || len > end-first || row >= 31) return;
  for (unsigned y=0;y<2;y++) for (unsigned x=first;x<end;x++)
    vram[base+(row+y)*32+x]=0x18a9;
  unsigned start=first+(center ? (end-first-len)/2 : 0);
  for (unsigned i=0;i<len;i++) {
    unsigned glyph=NativeFileLabels_Glyph((unsigned char)word[i],localized,russian);
    vram[base+row*32+start+i]=(uint16_t)(0x1800|glyph);
    vram[base+(row+1)*32+start+i]=(uint16_t)(0x1800|(glyph+16));
  }
}

static inline void NativeFileLabels_Apply(uint16_t *vram, unsigned module,
                                         const char *language, bool localized) {
  if (module < 1 || module > 4) return;
  if (language && !strcmp(language, "ja")) return;  // its screens have their own words
  unsigned frame_row=module==4?4:2, frame=0x1000+frame_row*32;
  // A complete live plaque is the readiness guard, including translated widths.
  if ((vram[frame+2]&0x3ff)!=0x189) return;
  unsigned cap=4;
  for (;cap<32;cap++)
    if ((vram[frame+cap]&0x43ff)==0x4189) break;
  if (cap==32) return;
  unsigned base=localized?0x7800:0x6000;
  bool russian=language && !strcmp(language,"ru");
  NativeFileLabels_Write(vram,base,frame_row+1,4,cap-1,
    NativeFileLabels_Word(language,module-1),localized,russian,true);
  if (module==1) {
    NativeFileLabels_Write(vram,base,22,6,30,NativeFileLabels_Word(language,1),localized,russian,false);
    NativeFileLabels_Write(vram,base,24,6,30,NativeFileLabels_Word(language,2),localized,russian,false);
  }
}
