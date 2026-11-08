#!/usr/bin/env python3
"""Exercise native randomizer generation without a ROM, assets or save files.

This checks the placement/logic component, not the gameplay hooks or 3DS UI.
The empty WRAM fixture represents no active game; generation must not mutate it.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]

HARNESS = r'''
#include "src/rando/rando_placement.h"
#include "src/rando/rando_settings.h"
#include "src/rando/rando_rng.h"
#include "src/rando/seed_identity.h"
#include "src/rando/item_ids.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8 g_ram[0x20000];

static void require(bool ok, const char *message) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void generate(SettingsPreset preset, uint64 seed, uint8 digest[32]) {
  RandoSettings settings;
  require(Settings_ApplyPreset(preset, &settings) == 0, "preset");
  require(Settings_Validate(&settings), "valid settings");
  char err[256] = {0};
  require(Placement_PreflightSettings(&settings, err, sizeof(err)), err);
  RandoPlacementTable placements = {
    .entries = calloc(kRandoLocationCapacity, sizeof(RandoPlacement))
  };
  require(placements.entries != NULL, "placement allocation");
  // Zero is a fixed retry cap, independent of CPU speed or wall-clock time.
  require(Place_AssumedFill(&settings, seed, 0, &placements), "assumed fill");
  require(placements.count > 200 && placements.count <= kRandoLocationCapacity,
          "complete placement size");
  for (uint16 i = 1; i < placements.count; i++)
    require(placements.entries[i-1].location_id < placements.entries[i].location_id,
            "unique ordered location IDs");
  require(Goal_IsCompletable(&settings, seed, &placements), "reachable goal");
  require(Accessibility_SeedAcceptable(&settings, seed, &placements),
          "accessibility acceptance");
  RandoSpheres spheres;
  require(Logic_ComputeSpheres(&settings, &placements, &spheres),
          "every placement has a reachable sphere");
  // Vanilla dungeon items must stay in the cartridge's actual key chests,
  // not merely preserve a total count that the reachability model assumes.
  const uint16 keys[][2]={{35,ITEM_SmallKey_PalaceOfDarkness},{47,ITEM_SmallKey_PalaceOfDarkness},
    {82,ITEM_SmallKey_IcePalace},{97,ITEM_SmallKey_TurtleRock},{100,ITEM_SmallKey_TurtleRock},
    {103,ITEM_SmallKey_TurtleRock},{104,ITEM_SmallKey_TurtleRock}};
  Placement_Install(&placements);
  for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++) {
    const RandoLocationDef *loc=Rando_FindLocationDef(keys[i][0]);
    require(loc&&loc->vanilla_item_id==keys[i][1],"cartridge key location registry");
    if(Settings_EffectiveSmallKeysMode(&settings)==kDungeonItemMode_Vanilla)
      require(Placement_Lookup(keys[i][0],keys[i][1])==keys[i][1],"vanilla key placement");
  }
  require(Rando_FindLocationDef(99)->vanilla_item_id==ITEM_Map_TurtleRock,"Turtle Rock map chest");
  require(Rando_FindLocationDef(126)->vanilla_item_id==ITEM_Compass_GanonsTower,"Ganon's Tower compass chest");
  const unsigned not_keys[]={44,46,106,107};
  for(unsigned i=0;i<sizeof(not_keys)/sizeof(*not_keys);i++)
    require(Rando_FindLocationDef(not_keys[i])->vanilla_item_id==ITEM_Rupee100,"non-key chest must not consume a vanilla key");
  Placement_Install(NULL);
  PlacementTable_ComputeDigest(&placements, digest);
  for (size_t i = 0; i < sizeof(g_ram); i++)
    require(g_ram[i] == 0, "generation changed game WRAM");
  free(placements.entries);
}

int main(void) {
  Rando_Rng_SelfCheck();
  Settings_SelfCheck();
  RandoSettings identity_settings; Settings_SetDefaults(&identity_settings);
  char native[65], same[65], other[65]; uint8 imported[32]={0};
  require(RandoSeedKey_Create(&identity_settings,42,kRandoSeedOrigin_Native,NULL,native), "native save key");
  require(RandoSeedKey_Valid(native), "canonical save key");
  require(RandoSeedKey_Create(&identity_settings,42,kRandoSeedOrigin_Native,NULL,same) && !strcmp(native,same), "stable save key");
  require(RandoSeedKey_Create(&identity_settings,43,kRandoSeedOrigin_Native,NULL,other) && strcmp(native,other), "seeds have separate saves");
  require(RandoSeedKey_Create(&identity_settings,42,kRandoSeedOrigin_Alttpr,imported,other) && strcmp(native,other), "imported seeds have separate saves");
  strcpy(same,other); imported[0]=1;
  require(RandoSeedKey_Create(&identity_settings,42,kRandoSeedOrigin_Alttpr,imported,other) && strcmp(same,other), "exact placements affect imported save key");
  require(!RandoSeedKey_Create(&identity_settings,42,kRandoSeedOrigin_Alttpr,NULL,other), "missing imported placement identity rejected");
  require(!RandoSeedKey_Valid("../other-seed") && !RandoSeedKey_Valid(NULL), "unsafe seed key rejected");
  strcpy(same,native); same[0]='A'; require(!RandoSeedKey_Valid(same), "noncanonical key rejected");
  const uint64 seeds[] = {0, 1, 42, UINT64_MAX};
  uint8 expected[kPreset__Count][4][32];
  for (int preset = 0; preset < kPreset__Count; preset++)
    for (int i = 0; i < 4; i++)
      generate((SettingsPreset)preset, seeds[i], expected[preset][i]);
  // Reverse both axes to catch state leaking across seeds/world states.
  for (int preset = kPreset__Count - 1; preset >= 0; preset--) {
    for (int i = 3; i >= 0; i--) {
      uint8 actual[32];
      generate((SettingsPreset)preset, seeds[i], actual);
      require(memcmp(actual, expected[preset][i], 32) == 0,
              "seed changed after another generation");
      printf("preset=%d seed=%llu digest=", preset, (unsigned long long)seeds[i]);
      for (int j = 0; j < 32; j++) printf("%02x", actual[j]);
      puts("");
    }
  }
  require(memcmp(expected[0][0], expected[0][1], 32) != 0,
          "different seeds should change placement");
  puts("PASS: 40 generations, deterministic replay, goals, reachability, WRAM isolation");
  return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--sdl2-config', default=os.environ.get('SDL2_CONFIG', 'sdl2-config'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    harness = work / 'randomizer_core_test.c'
    harness.write_text(HARNESS)
    binary = work / 'randomizer_core_test'
    engine = ROOT / 'engine'
    sdl_prefix = subprocess.check_output([args.sdl2_config, '--prefix'], text=True).strip()
    sdl_cflags = subprocess.check_output([args.sdl2_config, '--cflags'], text=True)
    # SDL's script does not quote a prefix containing spaces.
    include_flag = '-I' + str(Path(sdl_prefix) / 'include/SDL2')
    sdl_flags = [include_flag, *shlex.split(sdl_cflags.replace(include_flag, ''))]
    flags = ['-std=gnu11', '-O1', '-g', '-ffunction-sections', '-fdata-sections']
    flags += ['-Wl,-dead_strip'] if sys.platform == 'darwin' else ['-Wl,--gc-sections']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    sources = sorted((engine / 'src/rando').glob('*.c'))
    command = [os.environ.get('CC', 'clang'), *flags, *sdl_flags,
               '-I', str(engine), '-I', str(engine / 'src'),
               *(str(p) for p in sources), str(engine / 'third_party/sha256/sha256.c'),
               str(harness), '-o', str(binary)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / 'build.log').write_text(result.stdout)
    if result.returncode:
        print(result.stdout)
        return result.returncode
    result = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (work / 'test.log').write_text(result.stdout)
    print(result.stdout)
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
