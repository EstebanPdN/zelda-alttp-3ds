// The languages available in the startup menu. First boot follows the console.
// The player supplies
// only the USA cartridge; every other language is built from it, on the
// console, with a translation patch carried in romfs. The same choice sets the
// language of the port's own screens.
#pragma once

#include "ui_strings.h"

typedef struct GameLanguage {
  const char *code;   // dialogue language in the assets, Language = in zelda3.ini
  int menu;           // enum UiLanguage, -1 while the menus lack the language
  const char *name;   // native name, drawn with the setup font
  const char *patch;  // USA -> translation BPS in romfs, NULL for English
  const char *block;  // language pack in romfs, for a language with no cartridge
} GameLanguage;

static const GameLanguage kGameLanguages[] = {
  { "en", kUiLanguage_English,    "ENGLISH",   NULL, NULL },
  { "es", kUiLanguage_Spanish,    "ESPAÑOL",   "romfs:/languages/es.bps", NULL },
  { "pt", kUiLanguage_Portuguese, "PORTUGUÊS", "romfs:/languages/pt.bps", NULL },
  { "it", kUiLanguage_Italian,    "ITALIANO",  "romfs:/languages/it.bps", NULL },
  { "fr", kUiLanguage_French,     "FRANÇAIS",  "romfs:/languages/fr.bps", NULL },
  { "de", kUiLanguage_German,     "DEUTSCH",   "romfs:/languages/de.bps", NULL },
  { "ru", kUiLanguage_Russian,    "РУССКИЙ",   "romfs:/languages/ru.bps", NULL },
  { "ja", kUiLanguage_Japanese,   "日本語",     "romfs:/languages/ja.bps", NULL },
  // A Chinese cartridge does not exist; the pack carries its text and its
  // 16x16 glyphs, and zh.bps dresses it as the cartridge the extractor reads.
  { "zh", kUiLanguage_Chinese,    "简体中文",   "romfs:/languages/zh.bps", "romfs:/languages/zh.bin" },
};

enum {
  kGameLanguageCount = sizeof(kGameLanguages) / sizeof(kGameLanguages[0]),
};

// The selector's own lines, in each language. A line the setup font cannot
// draw yet falls back to English.
enum LanguageSelectorText {
  kSel_Title, kSel_Choose, kSel_Quit, kSel_Mods,
  kSel_Preparing, kSel_RomMissing, kSel_RomWhere, kSel_Unavailable,
  kSel_Failed,
  kSelCount,
};

static const char *const kLanguageSelectorText[kGameLanguageCount][kSelCount] = {
  { "LANGUAGE", "PRESS A TO SELECT", "PRESS START TO QUIT", "PRESS Y FOR MODS",
    "PREPARING...", "USA ROM NOT FOUND", "COPY IT TO /3DS/ZELDA 3DS/",
    "NOT AVAILABLE YET", "PREPARATION FAILED" },
  { "IDIOMA", "PULSA A PARA ELEGIR", "PULSA START PARA SALIR", "PULSA Y PARA MODS",
    "PREPARANDO...", "NO SE ENCONTRÓ LA ROM USA", "CÓPIALA EN /3DS/ZELDA 3DS/",
    "AÚN NO DISPONIBLE", "ERROR AL PREPARAR" },
  { "IDIOMA", "A PARA ESCOLHER", "START PARA SAIR", "Y PARA MODS",
    "PREPARANDO...", "ROM USA NÃO ENCONTRADA", "COPIE PARA /3DS/ZELDA 3DS/",
    "AINDA NÃO DISPONÍVEL", "ERRO AO PREPARAR" },
  { "LINGUA", "PREMI A PER SCEGLIERE", "PREMI START PER USCIRE", "PREMI Y PER LE MOD",
    "PREPARAZIONE...", "ROM USA NON TROVATA", "COPIALA IN /3DS/ZELDA 3DS/",
    "NON ANCORA DISPONIBILE", "PREPARAZIONE FALLITA" },
  { "LANGUE", "A POUR CHOISIR", "START POUR QUITTER", "Y POUR LES MODS",
    "PRÉPARATION...", "ROM USA INTROUVABLE", "COPIEZ-LA DANS /3DS/ZELDA 3DS/",
    "PAS ENCORE DISPONIBLE", "ÉCHEC DE LA PRÉPARATION" },
  { "SPRACHE", "A ZUM AUSWÄHLEN", "START ZUM BEENDEN", "Y FÜR MODS",
    "VORBEREITUNG...", "USA-ROM NICHT GEFUNDEN", "NACH /3DS/ZELDA 3DS/ KOPIEREN",
    "NOCH NICHT VERFÜGBAR", "VORBEREITUNG FEHLGESCHLAGEN" },
  { "ЯЗЫК", "A: ВЫБРАТЬ", "START: ВЫХОД", "Y: МОДЫ",
    "ПОДГОТОВКА...", "ROM USA НЕ НАЙДЕН", "СКОПИРУЙТЕ В /3DS/ZELDA 3DS/",
    "ПОКА НЕДОСТУПНО", "ОШИБКА ПОДГОТОВКИ" },
  { "言語", "Aで決定", "STARTで終了", "YでMOD",
    "準備中...", "USA ROMが見つかりません", "/3DS/ZELDA 3DS/にコピー",
    "まだ利用できません", "準備に失敗しました" },
  { "语言", "按 A 选择", "按 START 退出", "按 Y 查看模组",
    "准备中...", "未找到 USA ROM", "请复制到 /3DS/ZELDA 3DS/",
    "暂不可用", "准备失败" },
};
