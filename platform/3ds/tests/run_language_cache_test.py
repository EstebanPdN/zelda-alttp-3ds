#!/usr/bin/env python3
"""Check that installed language caches expire when the bundled patch changes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / 'platform/3ds/source/platform_3ds.c').read_text()


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define LANGUAGE_CART_FILE "zelda3_language.sfc"
typedef struct { const char *code; int menu; const char *name, *patch, *block; } GameLanguage;
static const char kProfileLanguageFile[] = "assets-language.txt";
'''
for name in ['static char *Trim(', 'static bool IsRegularFile(const char *path) {',
             'static bool LanguageUsesPatch(', 'static bool LanguageCacheStamp(',
             'static bool ProfileLanguageMatches(']:
    code += function(name)
code += r'''
static void Write(const char *path, const void *data, size_t size) {
  FILE *f = fopen(path, "wb"); assert(f);
  assert(fwrite(data, 1, size, f) == size); assert(fclose(f) == 0);
}
int main(void) {
  GameLanguage en = {"en", 0, "English", NULL, NULL};
  GameLanguage pt = {"pt", 0, "Portuguese", "pt.bps", NULL};
  GameLanguage zh = {"zh", 0, "Chinese", NULL, "zh.bin"};
  // The shipped form: the pack arrives inside the cartridge its patch builds.
  GameLanguage zhp = {"zh", 0, "Chinese", "zh.bps", "zh.bin"};
  uint8_t patch[16] = {'B','P','S','1',0,0,0,0,0,0,0,0,0x12,0x34,0x56,0x78};
  char stamp[64];
  assert(mkdir("profile", 0700) == 0);
  assert(LanguageCacheStamp(&en, stamp) && !strcmp(stamp, "en:2:00000000"));
  assert(!ProfileLanguageMatches("profile", &en));
  Write("profile/assets-language.txt", stamp, strlen(stamp));
  assert(ProfileLanguageMatches("profile", &en));
  assert(!LanguageCacheStamp(&pt, stamp));
  Write("pt.bps", patch, sizeof(patch));
  assert(LanguageCacheStamp(&pt, stamp) && !strcmp(stamp, "pt:2:78563412"));
  Write("profile/assets-language.txt", "pt", 2);
  Write("profile/zelda3_language.sfc", "fixture", 7);
  assert(!ProfileLanguageMatches("profile", &pt));
  Write("profile/assets-language.txt", stamp, strlen(stamp));
  assert(ProfileLanguageMatches("profile", &pt));
  patch[15]++;
  Write("pt.bps", patch, sizeof(patch));
  assert(!ProfileLanguageMatches("profile", &pt));
  patch[15]--;
  Write("pt.bps", patch, sizeof(patch));
  remove("profile/zelda3_language.sfc");
  assert(!ProfileLanguageMatches("profile", &pt));
  // A pack has no cartridge: its stamp is the file itself, and its schema moved
  // when the CJK dialogue flag moved, so old assets are read again.
  Write("zh.bin", "pack", 4);
  assert(LanguageCacheStamp(&zh, stamp) && !strncmp(stamp, "zh:3:", 5));
  assert(!ProfileLanguageMatches("profile", &zh));
  Write("profile/assets-language.txt", stamp, strlen(stamp));
  assert(ProfileLanguageMatches("profile", &zh));
  Write("zh.bin", "PACK", 4);
  assert(!ProfileLanguageMatches("profile", &zh));
  // A pack carried by a patch cartridge keeps the pack's schema: the assets
  // built from it changed shape even though the patch file did not.
  Write("zh.bps", patch, sizeof(patch));
  assert(LanguageCacheStamp(&zhp, stamp) && !strncmp(stamp, "zh:3:", 5));
  assert(!strncmp(stamp, "zh:3:", 5) && strcmp(stamp, "zh:2:") != 0);
  // Switching through the shipped list: every language gets its own stamp, the
  // profile matches only the one it was built for, and moving on rebuilds.
  {
    GameLanguage shipped[] = {
      {"en", 0, "English", NULL, NULL},
      {"es", 0, "Spanish", "es.bps", NULL},
      {"pt", 0, "Portuguese", "pt.bps", NULL},
      {"it", 0, "Italian", "it.bps", NULL},
      {"fr", 0, "French", "fr.bps", NULL},
      {"de", 0, "German", "de.bps", NULL},
      {"ru", 0, "Russian", "ru.bps", NULL},
      {"ja", 0, "Japanese", "ja.bps", NULL},
      {"zh", 0, "Chinese", "zh.bps", "zh.bin"},
    };
    uint8_t small[16] = {'B','P','S','1',0,0,0,0,0,0,0,0,0x12,0x34,0x56,0x78};
    for (int i = 0; i < 9; i++) {
      if (shipped[i].patch) Write(shipped[i].patch, small, sizeof(small));
      if (shipped[i].block) Write(shipped[i].block, "pack", 4);
      small[15] = (uint8_t)(0x78 + i);  // every patch has its own tail
      if (shipped[i].patch) Write(shipped[i].patch, small, sizeof(small));
      char mine[64];
      assert(LanguageCacheStamp(&shipped[i], mine));
      for (int j = 0; j < i; j++) {
        char other[64];
        assert(LanguageCacheStamp(&shipped[j], other));
        assert(strcmp(mine, other) != 0);
      }
      Write("profile/assets-language.txt", mine, strlen(mine));
      // A patch language only counts as built when its cartridge is there.
      if (shipped[i].patch) Write("profile/zelda3_language.sfc", "cart", 4);
      else remove("profile/zelda3_language.sfc");
      assert(ProfileLanguageMatches("profile", &shipped[i]));
      assert(!ProfileLanguageMatches("profile", &shipped[(i + 1) % 9]));
    }
  }
  Write("pt.bps", patch, 7);
  assert(!LanguageCacheStamp(&pt, stamp));
  patch[0] = '?';
  Write("pt.bps", patch, sizeof(patch));
  assert(!LanguageCacheStamp(&pt, stamp));
  puts("PASS language cache: nine-language switching, pack schema, legacy marker, missing cartridge and malformed patch");
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-language-cache-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(code)
    subprocess.run(['cc', '-O1', '-g', '-fsanitize=address,undefined',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], cwd=work, check=True)
