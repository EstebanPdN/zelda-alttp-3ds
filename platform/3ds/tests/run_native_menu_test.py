#!/usr/bin/env python3
"""Exercise native file-menu dispatch and map exits using production functions.

Graphics/upload helpers are stubs; RAM, input and dispatch are the real paths.
This checks control/recovery policy, not rendered screens or physical hardware.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
files = (root / 'engine/src/select_file.c').read_text()
maps = (root / 'engine/src/messaging.c').read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "src/types.h"
#include "src/native_menu.h"
#include "src/rando/rando.h"
#include "src/rando/rando_save.h"
bool RandoSave_SeedStorageActive(void) { return false; }
bool RandoSave_CreateFileSlot(int slot) { return false; }
void SelectFile_NotifySlotWritten(int slot) {}
RandoSidecarFileState Rando_SidecarFileState(void) { return kRandoSidecarFile_Absent; }
bool Rando_LoadSidecarSlot(int slot, RandoSidecarSlot *out) { return false; }
bool Rando_DetectChecksumDrift(const RandoSlotHeader *hdr, const uint8 *sram, uint32 size) { return false; }
void Rando_ActivateSidecarSlot(const RandoSidecarSlot *slot) {}
bool Rando_IsActive(void) { return false; }
void Rando_DeactivateSlot(void) {}
void Rando_ApplyLoadedSaveRuntimeSettings(void) {}
uint8 g_ram[131072];
#include "src/variables.h"
static bool LanguageCart_Japanese(void) { return false; }
static const uint8 *LanguageCart_RawBytes(uint32 addr, size_t size) { return NULL; }
#include "src/features.h"
#define ZELDA3_TEST_NATIVE_FILE_MENU 1
static uint8 save[8192];
static struct { uint8 *sram; } g_zenv = {save};
static unsigned actions;
static void SelectFile_Func5_DrawOams(int k) {}
static void SelectFile_Func6_DrawOams2(int k) {}
static void SelectFile_Func17(int k) {}
static void FileSelect_DrawFairy(uint8 x, uint8 y) {}
static void CopySaveToWRAM(void) { actions++; }
static void OverworldMap_SetupHdma(void) {}
static void WorldMap_HandleSprites(void) {}
'''
code += '\n'.join(line for line in files.splitlines()
                  if line.startswith('#define selectfile_R')) + '\n'
stubs = ['Module_SelectFile_0', 'FileSelect_ReInitSaveFlagsAndEraseTriforce',
         'Module_EraseFile_1', 'FileSelect_TriggerStripesAndAdvance',
         'FileSelect_TriggerNameStripesAndAdvance', 'FileSelect_EraseTriforce',
         'Module_CopyFile_2', 'CopyFile_ChooseSelection', 'CopyFile_ChooseTarget',
         'CopyFile_ConfirmSelection', 'KILLFile_SetUp', 'KILLFile_HandleSelection',
         'KILLFile_HandleConfirmation', 'NameFile_EraseSave', 'Module_NamePlayer_1',
         'Module_NamePlayer_2', 'NameFile_DoTheNaming']
for name in stubs:
    code += f'static void {name}(void) {{ actions++; }}\n'
for signature in ['void ReturnToFileSelect()', 'static bool FileSelect_HandleNativeInput(',
                  'void FileSelect_Main()', 'void Module01_FileSelect()',
                  'void Module02_CopyFile()', 'void Module03_KILLFile()',
                  'void Module04_NameFile()']:
    code += function(files, signature)
for name in ['kOverworldMap_Timer', 'kOverworldMap_Table2', 'kOverworldMap_Table3']:
    code += re.search(r'static const [^\n]+\b' + name + r'\[[^;]+;', maps)[0] + '\n'
for signature in ['bool DidPressButtonForMap()', 'void WorldMap_PlayerControl()',
                  'static inline bool WantExitDungeonMap()']:
    code += function(maps, signature)
code += r'''
int main(void) {
  const uint8 lows[] = {0x80,0,0,0x40,0,0};
  const uint8 highs[] = {0,0x10,0x80,0,0x40,0x04};
  for (unsigned i=0;i<6;i++) {
    memset(g_ram,0,sizeof(g_ram)); main_module_index=1; submodule_index=5;
    filtered_joypad_L=lows[i]; filtered_joypad_H=highs[i];
    Module01_FileSelect();
    assert(main_module_index==(i<2 ? 4 : 1));
    if (i==5) assert(selectfile_R16==1); // Direction navigation survives.
  }
  void (*dispatch[])(void)={Module02_CopyFile,Module03_KILLFile,Module04_NameFile};
  for(unsigned i=0;i<3;i++) {
    memset(g_ram,0,sizeof(g_ram)); memset(save,0xa5,sizeof(save));
    main_module_index=i+2; submodule_index=3; filtered_joypad_H=0x80;
    unsigned before=actions; dispatch[i]();
    assert(main_module_index==1 && submodule_index==1 && irq_flag==0xff);
    assert(actions==before); // B cannot copy, erase, or enter a character.
    for(unsigned j=0;j<sizeof(save);j++) assert(save[j]==0xa5);
  }
  for(unsigned item=0;item<2;item++) for(unsigned zoom=0;zoom<2;zoom++) {
    memset(g_ram,0,sizeof(g_ram)); hud_cur_item_x=item;
    overworld_map_flags=zoom; overworld_map_state=5; filtered_joypad_H=0x80;
    WorldMap_PlayerControl(); assert(overworld_map_state==6);
    assert(WantExitDungeonMap());
    filtered_joypad_H=0; filtered_joypad_L=0;
    assert(!WantExitDungeonMap());
    if(item) filtered_joypad_H=0x20; else filtered_joypad_L=0x40;
    assert(WantExitDungeonMap()); // Original X/Select path remains.
  }
  for(unsigned i=0;i<5;i++) assert(!NativeMenu_CanAutosave(i));
  assert(NativeMenu_CanAutosave(7)); assert(NativeMenu_CanAutosave(9));
  assert(NativeMenu_CanAutosave(14));
  assert(!NativeMenu_CanAutosave(20)); assert(!NativeMenu_CanAutosave(26));
  puts("PASS A/Start confirmation, B cancellation without SRAM writes, X/Y ignored in file selection, directions retained, B map exit at both zooms/assignments, title/naming excluded from autosave");
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-native-menu-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(code)
    subprocess.run(['cc', '-O1', '-fsanitize=address,undefined',
                    '-fno-sanitize=alignment', '-I' + str(root / 'engine'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
