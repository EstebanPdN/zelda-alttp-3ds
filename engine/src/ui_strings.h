// Interface language for the port's own screens (bottom-screen menus, the 3DS
// setup and updater views). The game's dialogue is a separate thing: it comes
// from the ROM and is picked with `Language =` in zelda3.ini.
//
// UTF-8 Latin, Cyrillic, Japanese and Chinese text uses the Python-generated native pixel fonts.
#pragma once

enum UiLanguage {
  kUiLanguage_English = 0,
  kUiLanguage_French = 1,
  kUiLanguage_Spanish, kUiLanguage_Portuguese, kUiLanguage_Italian, kUiLanguage_German, kUiLanguage_Russian,
  kUiLanguage_Japanese,
  // Chinese is drawn with the CJK glyphs baked from the translation (see
  // platform/linux/ui_font_cjk.h); it is last so the other languages keep
  // their values.
  kUiLanguage_Chinese,
  kUiLanguageCount,
};

enum UiString {
  // bottom-screen tabs and panels
  kUi_Gear, kUi_Map, kUi_Items, kUi_Dungeon,
  kUi_Bottles, kUi_Pendants, kUi_Crystals,
  // settings
  kUi_Settings, kUi_Screen, kUi_TurboSpeed, kUi_Developer, kUi_Update,
  kUi_Restart, kUi_Language, kUi_Back, kUi_Off, kUi_On, kUi_Open, kUi_Cancel,
  // screen settings
  kUi_DisplayMode, kUi_EdgeMode, kUi_Zoom, kUi_TopHud,
  kUi_Wide, kUi_Original, kUi_Stretch, kUi_FixedCamera, kUi_Standard,
  // developer settings
  kUi_MemDump, kUi_LoadState, kUi_ShowFps, kUi_Overlay,
  kUi_Write, kUi_Done, kUi_Wait, kUi_Load,
  kUi_LoadDumpLine1, kUi_LoadDumpLine2, kUi_ProgressLost,
  // overlay rows
  kUi_Version, kUi_Model, kUi_FpsNow, kUi_FpsAvg, kUi_Core1,
  kUi_Room, kUi_Module, kUi_House, kUi_Overworld,
  // button remapping
  kUi_RemapButtons, kUi_More, kUi_Top, kUi_PressKey,
  // updater
  kUi_Channel, kUi_Stable, kUi_Prerelease, kUi_NoRelease,
  kUi_DownloadAndInstall, kUi_CheckForUpdate, kUi_DownloadUpdate,
  kUi_InstallUpdate, kUi_PleaseWait, kUi_UpdateInstalled,
  kUi_NotesOnTop, kUi_TapForNotes, kUi_Prev, kUi_Next, kUi_SystemUpdates,
  kUi_UpdateAvailable, kUi_PickReleaseLine1, kUi_PickReleaseLine2,
  kUi_Gameplay, kUi_TurboControl, kUi_Autosave, kUi_GameLanguage,
  kUi_XHold, kUi_Checking, kUi_UpToDate, kUi_UpdateReady, kUi_NoUpdate,
  kUi_Downloading, kUi_Verifying, kUi_Installing, kUi_UpdateError,
  kUi_StateLoaded, kUi_NoDump, kUi_NoState, kUi_InvalidState, kUi_WrongGame, kUi_IoError,
  kUi_ErrorSdWrite, kUi_ErrorWifi, kUi_ErrorTls, kUi_ErrorRelease,
  kUi_ErrorSpace, kUi_ErrorLaunchPath, kUi_ErrorDownload, kUi_ErrorInstall,
  kUi_ErrorCancelled, kUi_ErrorConnection, kUi_ErrorStart, kUi_ErrorChannel,
  kUi_Changes, kUi_Page,
  kUi_ChangeGameLanguage, kUi_No, kUi_Yes,
  kUi_Randomizer, kUi_OpenRandomizer, kUi_Seed, kUi_Preset,
  kUi_GenerateSeed, kUi_SavedSeeds, kUi_OrdinaryGame, kUi_NoSeeds,
  kUi_SkinsOnly, kUi_RandoOpen, kUi_RandoStandard, kUi_RandoInverted,
  kUi_RandoRetro, kUi_RandoTriforce, kUi_Delete, kUi_SeedList,
  kUi_ImportSeed, kUi_ImportHelp,
  kUi_RandoInvalidFile, kUi_RandoUnsupported, kUi_RandoMissingData,
  kUi_RandoUncertified, kUi_RandoStorageError, kUi_RandoSourceChanged,
  // developer saves, test scenarios and the seed in play
  kUi_SaveState, kUi_LoadDiagnostic, kUi_LoadSavedState, kUi_Tests,
  kUi_TestsHelp, kUi_ScenarioMissing, kUi_CurrentSeed, kUi_LeaveRandomizer,
  kUi_Start, kUi_Change, kUi_NoMods, kUi_Skins, kUi_DeveloperTool, kUi_Continue, kUi_Quit, kUi_Mods, kUi_Loading,
  // Quality of life. Option labels remain contiguous in QolOption order.
  kUi_QolTitle, kUi_QolFastSwitch, kUi_QolPegasus, kUi_QolKakariko, kUi_QolDimFlashes, kUi_QolFairyHearts, kUi_QolSwordPickup, kUi_QolBrightRooms, kUi_QolMuteBeep, kUi_QolRepeatBlocks, kUi_QolBombs99, kUi_QolArrows99, kUi_QolWallet9999, kUi_QolInventory, kUi_QolShovel, kUi_QolMirror, kUi_QolLive, kUi_QolRestart, kUi_QolRestartQuestion, kUi_QolExcessWarning, kUi_QolHelp,
  kUi_QolDescFastSwitch, kUi_QolDescPegasus, kUi_QolDescKakariko, kUi_QolDescDimFlashes, kUi_QolDescFairyHearts, kUi_QolDescSwordPickup, kUi_QolDescBrightRooms, kUi_QolDescMuteBeep, kUi_QolDescRepeatBlocks, kUi_QolDescBombs99, kUi_QolDescArrows99, kUi_QolDescWallet9999, kUi_QolDescInventory, kUi_QolDescShovel, kUi_QolDescMirror,
  kUi_Select, kUi_SelectHelp, kUi_LanguageDescription, kUi_MainMenu, kUi_ReturnMainMenu,kUi_Test,kUi_Life,kUi_Magic,
  kUiStringCount,
};

void UI_SetLanguage(int language);
int UI_GetLanguage(void);
const char *UI_Text(int id);

// Language codes for the MenuLanguage key in zelda3.ini. An unknown code falls
// back to English.
const char *UI_LanguageCode(int language);
int UI_LanguageFromCode(const char *code);

// Dungeon 0..13 in the current language, in the order the game numbers them.
// Out-of-range indices give the generic "dungeon" label.
const char *UI_DungeonName(int dungeon);

// Translate updater diagnostics while leaving unknown technical details intact.
const char *UI_UpdateError(const char *message);
