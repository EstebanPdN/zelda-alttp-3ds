// Native interface strings. Keep short labels readable at 320x240.
#include <string.h>

#include "ui_strings.h"

static const char *const kUiStrings[kUiStringCount][kUiLanguageCount] = {
  [kUi_QolDescFastSwitch] = { "L AND R CYCLE THROUGH OWNED ITEMS.", "L ET R FONT DÉFILER LES OBJETS POSSÉDÉS.", "L Y R CAMBIAN ENTRE LOS OBJETOS QUE TIENES.", "L E R ALTERNAM ENTRE OS ITENS QUE VOCÊ TEM.", "L E R CAMBIANO GLI OGGETTI POSSEDUTI.", "L UND R WECHSELN ZWISCHEN DEINEN GEGENSTÄNDEN.", "L И R ПЕРЕКЛЮЧАЮТ ИМЕЮЩИЕСЯ ПРЕДМЕТЫ.", "LとRで、持っているアイテムを切り替えます。", "L/R 切换已拥有的道具。" },
  [kUi_QolDescPegasus] = { "TURN WHILE DASHING AND KEEP RUNNING AFTER A TRANSITION.", "TOURNEZ EN COURANT ET GARDEZ VOTRE ÉLAN ENTRE LES ZONES.", "PERMITE GIRAR AL CORRER Y SEGUIR CORRIENDO AL CAMBIAR DE ZONA.", "PERMITE VIRAR NA CORRIDA E CONTINUAR AO MUDAR DE ÁREA.", "GIRA DURANTE LA CORSA E CONTINUA TRA LE AREE.", "BEIM RENNEN ABBIEGEN UND NACH EINEM GEBIETSWECHSEL WEITERRENNEN.", "ПОВОРОТЫ ВО ВРЕМЯ БЕГА И БЕГ ПОСЛЕ ПЕРЕХОДА МЕЖДУ ЗОНАМИ.", "ダッシュ中に向きを変えられ、エリアを移動しても　走り続けます。", "冲刺时可转向，换区后继续奔跑。" },
  [kUi_QolDescKakariko] = { "OPENS A SHORTCUT NEAR KAKARIKO WITH THE REQUIRED GLOVE.", "OUVRE UN RACCOURCI PRÈS DE COCORICO AVEC LE GANT REQUIS.", "ABRE UN ATAJO JUNTO A KAKARIKO CON EL GUANTE NECESARIO.", "ABRE UM ATALHO PERTO DE KAKARIKO COM A LUVA NECESSÁRIA.", "APRE UNA SCORCIATOIA VICINO A KAKARIKO CON IL GUANTO RICHIESTO.", "ÖFFNET MIT DEM PASSENDEN HANDSCHUH EINE ABKÜRZUNG BEI KAKARIKO.", "ОТКРЫВАЕТ КОРОТКИЙ ПУТЬ У КАКАРИКО ПРИ НАЛИЧИИ НУЖНОЙ ПЕРЧАТКИ.", "必要なグラブがあれば、カカリコ村の近くの　近道が開きます。", "持有手套时在卡卡利科旁开启捷径。" },
  [kUi_QolDescDimFlashes] = { "REDUCES BRIGHT FLASHES DURING EFFECTS AND TRANSITIONS.", "ATTÉNUE LES FLASHES DES EFFETS ET TRANSITIONS.", "REDUCE LOS DESTELLOS DE LOS EFECTOS Y LAS TRANSICIONES.", "REDUZ OS CLARÕES DOS EFEITOS E DAS TRANSIÇÕES.", "RIDUCE I LAMPEGGI DI EFFETTI E TRANSIZIONI.", "VERRINGERT HELLE BLITZE BEI EFFEKTEN UND ÜBERGÄNGEN.", "УМЕНЬШАЕТ ЯРКИЕ ВСПЫШКИ ЭФФЕКТОВ И ПЕРЕХОДОВ.", "演出や場面切り替えの、まぶしい光をおさえます。", "减弱特效与切换时的强光闪烁。" },
  [kUi_QolDescFairyHearts] = { "FAIRIES RESTORE TEN HEARTS, INCLUDING WHEN REVIVING LINK.", "LES FÉES RENDENT DIX COEURS, MÊME EN RESSUSCITANT LINK.", "LAS HADAS RECUPERAN DIEZ CORAZONES, TAMBIÉN AL REVIVIR A LINK.", "AS FADAS RECUPERAM DEZ CORAÇÕES, INCLUSIVE AO REVIVER LINK.", "LE FATE RIPRISTINANO DIECI CUORI ANCHE QUANDO RIANIMANO LINK.", "FEEN HEILEN ZEHN HERZEN, AUCH BEIM WIEDERBELEBEN VON LINK.", "ФЕИ ВОССТАНАВЛИВАЮТ ДЕСЯТЬ СЕРДЕЦ, В ТОМ ЧИСЛЕ ПРИ ВОЗРОЖДЕНИИ.", "ヨウセイでハートが10回復します。　リンクが復活するときも同じです。", "精灵恢复10颗心，复活时同样有效。" },
  [kUi_QolDescSwordPickup] = { "COLLECT DROPPED ITEMS BY TOUCHING THEM WITH THE SWORD.", "RAMASSEZ LES OBJETS AU SOL AVEC VOTRE ÉPÉE.", "RECOGE OBJETOS CAÍDOS TOCÁNDOLOS CON LA ESPADA.", "RECOLHA ITENS CAÍDOS TOCANDO NELES COM A ESPADA.", "RACCOGLI GLI OGGETTI A TERRA CON LA SPADA.", "SAMMLE GEGENSTÄNDE AM BODEN MIT DEM SCHWERT EIN.", "СОБИРАЙТЕ ЛЕЖАЩИЕ ПРЕДМЕТЫ, КАСАЯСЬ ИХ МЕЧОМ.", "落ちているアイテムに剣を当てると、拾えます。", "用剑触碰即可拾取掉落的道具。" },
  [kUi_QolDescBrightRooms] = { "SHOWS THE WHOLE DARK ROOM. DOES NOT CHANGE PUZZLES OR LAMP OWNERSHIP.", "ÉCLAIRE LA SALLE ENTIÈRE SANS MODIFIER LES ÉNIGMES NI DONNER LA LAMPE.", "MUESTRA TODA LA SALA OSCURA. NO CAMBIA LOS PUZLES NI TE DA LA LÁMPARA.", "MOSTRA TODA A SALA ESCURA SEM MUDAR ENIGMAS NEM DAR A LANTERNA.", "MOSTRA TUTTA LA STANZA BUIA SENZA CAMBIARE ENIGMI O DARE LA LANTERNA.", "ZEIGT DUNKLE RÄUME GANZ AN. RÄTSEL UND LAMPENBESITZ BLEIBEN GLEICH.", "ПОКАЗЫВАЕТ ТЁМНУЮ КОМНАТУ ЦЕЛИКОМ. НЕ МЕНЯЕТ ЗАГАДКИ И НЕ ДАЁТ ЛАМПУ.", "暗い部屋の全体が見えます。　なぞ解きは変わらず、カンテラも手に入りません。", "照亮整个黑暗房间，不改谜题与油灯。" },
  [kUi_QolDescMuteBeep] = { "DISABLES THE ANNOYING BEEP THAT SOUNDS WHEN HEALTH IS LOW.", "COUPE LE BIP GÊNANT QUI SONNE QUAND LA VIE EST FAIBLE.", "DESACTIVA EL PITIDO MOLESTO QUE SUENA CUANDO QUEDA POCA VIDA.", "DESATIVA O APITO INCÔMODO QUE SOA QUANDO RESTA POUCA VIDA.", "DISATTIVA IL FASTIDIOSO SEGNALE QUANDO RESTA POCA VITA.", "SCHALTET DEN STÖRENDEN WARNTON BEI WENIG LEBEN AUS.", "ОТКЛЮЧАЕТ НАЗОЙЛИВЫЙ СИГНАЛ, КОГДА ОСТАЁТСЯ МАЛО ЗДОРОВЬЯ.", "体力が少ないときの、うるさい警告音を消します。", "关闭低血量时恼人的提示音。" },
  [kUi_QolDescRepeatBlocks] = { "ALLOWS DUNGEON BLOCKS TO BE PUSHED MORE THAN ONCE.", "PERMET DE POUSSER PLUSIEURS FOIS LES BLOCS DES DONJONS.", "PERMITE EMPUJAR VARIAS VECES LOS BLOQUES DE LAS MAZMORRAS.", "PERMITE EMPURRAR MAIS DE UMA VEZ OS BLOCOS DOS CALABOUÇOS.", "PERMETTE DI SPOSTARE PIÙ VOLTE I BLOCCHI DEI DUNGEON.", "BLÖCKE IN VERLIESEN LASSEN SICH MEHRFACH SCHIEBEN.", "ПОЗВОЛЯЕТ НЕСКОЛЬКО РАЗ ДВИГАТЬ БЛОКИ В ПОДЗЕМЕЛЬЯХ.", "ダンジョンのブロックを、何度でも押せます。", "迷宫中的方块可以反复推动。" },
  [kUi_QolDescBombs99] = { "RAISES BOMB CAPACITY TO 99 WITHOUT GIVING EXTRA BOMBS.", "PORTE LA CAPACITÉ À 99 BOMBES SANS EN DONNER.", "AUMENTA LA CAPACIDAD A 99 BOMBAS. NO REGALA BOMBAS.", "AUMENTA A CAPACIDADE PARA 99 BOMBAS SEM DAR BOMBAS EXTRAS.", "AUMENTA LA CAPACITÀ A 99 BOMBE SENZA REGALARNE.", "ERHÖHT DIE KAPAZITÄT AUF 99 BOMBEN, OHNE BOMBEN ZU VERGEBEN.", "УВЕЛИЧИВАЕТ ВМЕСТИМОСТЬ ДО 99 БОМБ, НЕ ДОБАВЛЯЯ САМИ БОМБЫ.", "バクダンを99コまで持てます。　バクダンは増えません。", "炸弹上限提至 99，不赠送炸弹。" },
  [kUi_QolDescArrows99] = { "RAISES ARROW CAPACITY TO 99 WITHOUT GIVING EXTRA ARROWS.", "PORTE LA CAPACITÉ À 99 FLÈCHES SANS EN DONNER.", "AUMENTA LA CAPACIDAD A 99 FLECHAS. NO REGALA FLECHAS.", "AUMENTA A CAPACIDADE PARA 99 FLECHAS SEM DAR FLECHAS EXTRAS.", "AUMENTA LA CAPACITÀ A 99 FRECCE SENZA REGALARNE.", "ERHÖHT DIE KAPAZITÄT AUF 99 PFEILE, OHNE PFEILE ZU VERGEBEN.", "УВЕЛИЧИВАЕТ ВМЕСТИМОСТЬ ДО 99 СТРЕЛ, НЕ ДОБАВЛЯЯ САМИ СТРЕЛЫ.", "矢を99本まで持てます。　矢は増えません。", "箭上限提至 99，不赠送箭。" },
  [kUi_QolDescWallet9999] = { "RAISES WALLET CAPACITY TO 9999 RUPEES WITHOUT GIVING MONEY.", "PORTE LA BOURSE À 9999 RUBIS SANS DONNER D'ARGENT.", "AUMENTA LA CARTERA A 9999 RUPIAS. NO REGALA DINERO.", "AUMENTA A CARTEIRA PARA 9999 RÚPIAS SEM DAR DINHEIRO.", "AUMENTA LA BORSA A 9999 RUPIE SENZA REGALARE DENARO.", "ERHÖHT DEN GELDBEUTEL AUF 9999 RUBINE, OHNE GELD ZU VERGEBEN.", "УВЕЛИЧИВАЕТ КОШЕЛЁК ДО 9999 РУПИЙ, НЕ ДОБАВЛЯЯ ДЕНЬГИ.", "サイフの上限を9999ルピーにします。　ルピーは増えません。", "钱包上限提至 9999，不赠送卢比。" },
  [kUi_QolDescInventory] = { "USES 24 SLOTS, WITH FOUR SEPARATE BOTTLES AND A SHOVEL SLOT.", "24 CASES AVEC QUATRE FLACONS SÉPARÉS ET UNE CASE POUR LA PELLE.", "USA 24 CASILLAS, CON CUATRO BOTELLAS SEPARADAS Y UNA CASILLA PARA LA PALA.", "USA 24 ESPAÇOS, COM QUATRO GARRAFAS SEPARADAS E UM ESPAÇO PARA A PÁ.", "USA 24 CASELLE CON QUATTRO AMPOLLE SEPARATE E UNA CASELLA PER LA PALA.", "24 FELDER MIT VIER EINZELNEN FLASCHEN UND EINEM SCHAUFELFELD.", "24 ЯЧЕЙКИ, ЧЕТЫРЕ ОТДЕЛЬНЫЕ БУТЫЛКИ И ЯЧЕЙКА ДЛЯ ЛОПАТЫ.", "アイテム欄が24マスになり、ビン4つと　シャベルに専用のマスができます。", "共 24 格，4瓶独立，另有铲子格。" },
  [kUi_QolDescShovel] = { "KEEPS THE SHOVEL AFTER FINDING THE FLUTE. RANDOMIZER OWNERSHIP IS RESPECTED.", "GARDE LA PELLE APRÈS LA FLÛTE. RESPECTE LES OBJETS ACQUIS EN RANDOMIZER.", "CONSERVA LA PALA AL CONSEGUIR LA FLAUTA. RESPETA LOS OBJETOS DEL RANDOMIZER.", "MANTÉM A PÁ APÓS OBTER A FLAUTA. RESPEITA OS ITENS DO RANDOMIZER.", "CONSERVA LA PALA DOPO IL FLAUTO. RISPETTA GLI OGGETTI DEL RANDOMIZER.", "BEHÄLT DIE SCHAUFEL NACH DER FLÖTE. BEACHTET DEN BESITZ IM RANDOMIZER.", "СОХРАНЯЕТ ЛОПАТУ ПОСЛЕ ПОЛУЧЕНИЯ ФЛЕЙТЫ. УЧИТЫВАЕТ ПРЕДМЕТЫ RANDOMIZER.", "オカリナを手に入れても、シャベルがなくなりません。　ランダマイザーの所持品はそのままです。", "拿到笛子后保留铲子，随机化归属照常。" },
  [kUi_QolDescMirror] = { "ALLOWS THE MAGIC MIRROR TO TRAVEL IN BOTH DIRECTIONS BETWEEN WORLDS.", "PERMET AU MIROIR DE PASSER ENTRE LES MONDES DANS LES DEUX SENS.", "PERMITE USAR EL ESPEJO MÁGICO EN AMBOS SENTIDOS ENTRE LOS MUNDOS.", "PERMITE USAR O ESPELHO MÁGICO NOS DOIS SENTIDOS ENTRE OS MUNDOS.", "PERMETTE DI USARE LO SPECCHIO MAGICO IN ENTRAMBE LE DIREZIONI TRA I MONDI.", "ERLAUBT DEM MAGISCHEN SPIEGEL DEN WELTENWECHSEL IN BEIDE RICHTUNGEN.", "ПОЗВОЛЯЕТ ВОЛШЕБНОМУ ЗЕРКАЛУ ПЕРЕМЕЩАТЬ МЕЖДУ МИРАМИ В ОБЕ СТОРОНЫ.", "マジカルミラーで、2つの世界を　どちらの方向にも行き来できます。", "魔镜可在两个世界间双向往返。" },
  [kUi_Select] = { "SELECT", "CHOISIR", "ELEGIR", "ESCOLHER", "SCEGLI", "WÄHLEN", "ВЫБРАТЬ", "決定", "选择" },
  [kUi_SelectHelp] = { "A / START: SELECT", "A / START : CHOISIR", "A / START: ELEGIR", "A / START: ESCOLHER", "A / START: SCEGLI", "A / START: WÄHLEN", "A / START: ВЫБРАТЬ", "A / START：決定", "A / START: 选择" },
  [kUi_Test] = { "TEST", "TEST", "PRUEBA", "TESTE", "TEST", "TEST", "ТЕСТ", "テスト", "测试" },
  [kUi_Magic] = { "MAGIC", "MAGIE", "MAGIA", "MAGIA", "MAGIA", "MAGIE", "МАГИЯ", "魔法", "魔力" },
  [kUi_Life] = { "LIFE", "VIE", "VIDA", "VIDA", "VITA", "LEBEN", "ЖИЗНЬ", "体力", "生命" },
  [kUi_MainMenu] = { "MAIN MENU", "MENU PRINCIPAL", "MENÚ INICIAL", "MENU INICIAL", "MENU INIZIALE", "STARTMENÜ", "ГЛАВНОЕ МЕНЮ", "メインメニュー", "主菜单" },
  [kUi_ReturnMainMenu] = { "RETURN TO THE MAIN MENU?", "REVENIR AU MENU PRINCIPAL?", "VOLVER AL MENÚ INICIAL?", "VOLTAR AO MENU INICIAL?", "TORNARE AL MENU INIZIALE?", "ZUM STARTMENÜ ZURÜCKKEHREN?", "ВЕРНУТЬСЯ В ГЛАВНОЕ МЕНЮ?", "メインメニューに戻りますか？", "返回主菜单？" },
  [kUi_LanguageDescription] = { "GAME AND MENUS WILL USE ENGLISH. CONFIRM TO APPLY.", "LE JEU ET LES MENUS SERONT EN FRANÇAIS. CONFIRMEZ POUR APPLIQUER.", "EL JUEGO Y LOS MENÚS ESTARÁN EN ESPAÑOL. CONFIRMA PARA APLICARLO.", "O JOGO E OS MENUS FICARÃO EM PORTUGUÊS. CONFIRME PARA APLICAR.", "IL GIOCO E I MENU SARANNO IN ITALIANO. CONFERMA PER APPLICARE.", "SPIEL UND MENÜS WERDEN AUF DEUTSCH ANGEZEIGT. BITTE BESTÄTIGEN.", "ИГРА И МЕНЮ БУДУТ НА РУССКОМ ЯЗЫКЕ. ПОДТВЕРДИТЕ ВЫБОР.", "ゲームとメニューを日本語で表示します。　決定すると反映されます。", "游戏与菜单将使用简体中文，确认后生效。" },
  [kUi_QolTitle] = { "QUALITY OF LIFE", "CONFORT DE JEU", "CALIDAD DE VIDA", "QUALIDADE DE VIDA", "MIGLIORIE", "KOMFORT", "УДОБСТВО ИГРЫ", "便利機能", "金手指" },
  [kUi_QolFastSwitch] = { "L/R ITEM SWITCH", "OBJETS AVEC L/R", "OBJETOS CON L/R", "ITENS COM L/R", "OGGETTI CON L/R", "L/R GEGENSTÄNDE", "СМЕНА ПРЕДМЕТОВ L/R", "L/Rアイテム切替", "L/R 切换道具" },
  [kUi_QolPegasus] = { "IMPROVED PEGASUS BOOTS", "BOTTES AMÉLIORÉES", "BOTAS MEJORADAS", "BOTAS MELHORADAS", "STIVALI MIGLIORATI", "BESSERE PEGASUSSTIEFEL", "УЛУЧШЕННЫЕ САПОГИ", "ペガサスのくつ強化", "改良飞马靴" },
  [kUi_QolKakariko] = { "KAKARIKO SHORTCUT", "RACCOURCI COCORICO", "ATAJO A KAKARIKO", "ATALHO A KAKARIKO", "SCORCIATOIA KAKARIKO", "ABKÜRZUNG KAKARIKO", "КОРОТКИЙ ПУТЬ КАКАРИКО", "カカリコ村の近道", "卡卡利科捷径" },
  [kUi_QolDimFlashes] = { "REDUCED FLASHES", "MOINS DE FLASHES", "MENOS DESTELLOS", "MENOS CLARÕES", "MENO LAMPEGGI", "WENIGER BLITZE", "МЕНЬШЕ ВСПЫШЕК", "点滅をおさえる", "减弱闪光" },
  [kUi_QolFairyHearts] = { "FAIRIES: 10 HEARTS", "FÉES : 10 COEURS", "HADAS: 10 CORAZONES", "FADAS: 10 CORAÇÕES", "FATE: 10 CUORI", "FEEN: 10 HERZEN", "ФЕИ: 10 СЕРДЕЦ", "ヨウセイ：ハート10", "精灵: 10 颗心" },
  [kUi_QolSwordPickup] = { "SWORD ITEM PICKUP", "RAMASSER AVEC ÉPÉE", "RECOGER CON ESPADA", "RECOLHER COM ESPADA", "RACCOGLI CON SPADA", "SAMMELN MIT SCHWERT", "СБОР ПРЕДМЕТОВ МЕЧОМ", "剣でアイテム回収", "挥剑拾取道具" },
  [kUi_QolBrightRooms] = { "BRIGHT DARK ROOMS", "ÉCLAIRER LES SALLES", "ILUMINAR SALAS OSCURAS", "ILUMINAR SALAS ESCURAS", "ILLUMINA STANZE BUIE", "DUNKLE RÄUME ERHELLEN", "ОСВЕЩАТЬ ТЁМНЫЕ КОМНАТЫ", "暗い部屋を明るく", "照亮黑暗房间" },
  [kUi_QolMuteBeep] = { "MUTE LOW HEALTH BEEP", "COUPER ALARME DE VIE", "QUITAR PITIDO DE VIDA", "SEM ALERTA DE VIDA", "SILENZIA ALLARME VITA", "KEIN WARNTON BEI NOT", "БЕЗ СИГНАЛА МАЛОГО HP", "体力警告音オフ", "关闭低血提示音" },
  [kUi_QolRepeatBlocks] = { "REPEATED BLOCK PUSH", "REPOUSSER LES BLOCS", "MOVER BLOQUES DE NUEVO", "MOVER BLOCOS DE NOVO", "SPOSTA ANCORA I BLOCCHI", "BLÖCKE MEHRFACH SCHIEBEN", "ДВИГАТЬ БЛОКИ ПОВТОРНО", "ブロックを何度でも", "方块可重复推" },
  [kUi_QolBombs99] = { "99 BOMBS", "99 BOMBES", "99 BOMBAS", "99 BOMBAS", "99 BOMBE", "99 BOMBEN", "99 БОМБ", "バクダン99", "炸弹上限 99" },
  [kUi_QolArrows99] = { "99 ARROWS", "99 FLÈCHES", "99 FLECHAS", "99 FLECHAS", "99 FRECCE", "99 PFEILE", "99 СТРЕЛ", "矢99本", "箭上限 99" },
  [kUi_QolWallet9999] = { "WALLET: 9999 RUPEES", "BOURSE : 9999 RUBIS", "CARTERA: 9999 RUPIAS", "CARTEIRA: 9999 RÚPIAS", "BORSA: 9999 RUPIE", "GELDBEUTEL: 9999 RUBINE", "КОШЕЛЁК: 9999 РУПИЙ", "サイフ：9999ルピー", "钱包: 9999 卢比" },
  [kUi_QolInventory] = { "EXPANDED INVENTORY", "INVENTAIRE ÉTENDU", "INVENTARIO AMPLIADO", "INVENTÁRIO AMPLIADO", "INVENTARIO AMPLIATO", "ERWEITERTES INVENTAR", "РАСШИРЕННЫЙ ИНВЕНТАРЬ", "アイテム欄の拡張", "扩展道具栏" },
  [kUi_QolShovel] = { "PERMANENT SHOVEL", "PELLE PERMANENTE", "PALA PERMANENTE", "PÁ PERMANENTE", "PALA PERMANENTE", "SCHAUFEL BEHALTEN", "СОХРАНЯТЬ ЛОПАТУ", "シャベルを保持", "永久铲子" },
  [kUi_QolMirror] = { "MIRROR: BOTH WORLDS", "MIROIR : DEUX MONDES", "ESPEJO: AMBOS MUNDOS", "ESPELHO: DOIS MUNDOS", "SPECCHIO: DUE MONDI", "SPIEGEL: BEIDE WELTEN", "ЗЕРКАЛО: ОБА МИРА", "ミラー：2つの世界", "魔镜: 两个世界" },
  [kUi_QolLive] = { "APPLIES WHILE PLAYING", "EFFET IMMÉDIAT", "CAMBIO INMEDIATO", "EFEITO IMEDIATO", "EFFETTO IMMEDIATO", "SOFORT ANWENDBAR", "ПРИМЕНЯЕТСЯ СРАЗУ", "プレイ中に反映", "游戏中即时生效" },
  [kUi_QolRestart] = { "RESTART REQUIRED", "REDÉMARRAGE REQUIS", "REQUIERE REINICIAR", "REQUER REINICIAR", "RIAVVIO NECESSARIO", "NEUSTART ERFORDERLICH", "ТРЕБУЕТСЯ ПЕРЕЗАПУСК", "再起動が必要", "需要重启" },
  [kUi_QolRestartQuestion] = { "RESTART THE GAME?", "REDÉMARRER LE JEU ?", "REINICIAR EL JUEGO?", "REINICIAR O JOGO?", "RIAVVIARE IL GIOCO?", "SPIEL NEU STARTEN?", "ПЕРЕЗАПУСТИТЬ ИГРУ?", "再起動しますか？", "重启游戏？" },
  [kUi_QolExcessWarning] = { "AMMO OR RUPEES ABOVE ORIGINAL LIMITS WILL BE LOST.", "LES MUNITIONS OU RUBIS AU-DELÀ DES LIMITES INITIALES SERONT PERDUS.", "SE PERDERÁN LAS BOMBAS, FLECHAS O RUPIAS QUE SUPEREN EL LÍMITE ORIGINAL.", "BOMBAS, FLECHAS OU RÚPIAS ACIMA DO LIMITE ORIGINAL SERÃO PERDIDAS.", "BOMBE, FRECCE O RUPIE OLTRE I LIMITI ORIGINALI ANDRANNO PERSE.", "BOMBEN, PFEILE ODER RUBINE ÜBER DEM ORIGINALEN LIMIT GEHEN VERLOREN.", "БОМБЫ, СТРЕЛЫ ИЛИ РУПИИ СВЕРХ ИСХОДНОГО ЛИМИТА БУДУТ ПОТЕРЯНЫ.", "元の上限をこえるバクダン、矢、ルピーは　失われます。", "超过原始上限的弹药或卢比将会丢失。" },
  [kUi_QolHelp] = { "A: CHANGE   B: BACK", "A : CHANGER   B : RETOUR", "A: CAMBIAR   B: VOLVER", "A: ALTERAR   B: VOLTAR", "A: CAMBIA   B: INDIETRO", "A: ÄNDERN   B: ZURÜCK", "A: ИЗМЕНИТЬ   B: НАЗАД", "A：変更   B：戻る", "A: 修改   B: 返回" },
  [kUi_DeveloperTool] = { "DEVELOPER TOOL", "OUTIL DE DÉVELOPPEMENT", "HERRAMIENTA DE DESARROLLADOR", "FERRAMENTA DE DESENVOLVEDOR", "STRUMENTO PER SVILUPPATORI", "ENTWICKLERWERKZEUG", "ИНСТРУМЕНТ РАЗРАБОТЧИКА", "開発ツール", "开发者工具" },
  [kUi_Start] = { "START", "DÉMARRER", "INICIAR", "INICIAR", "AVVIA", "STARTEN", "НАЧАТЬ", "開始", "开始" },
  [kUi_Change] = { "CHANGE", "CHANGER", "CAMBIAR", "ALTERAR", "CAMBIA", "ÄNDERN", "ИЗМЕНИТЬ", "変更", "修改" },
  [kUi_NoMods] = { "NO MODS", "AUCUN MOD", "SIN MODS", "SEM MODS", "NESSUNA MOD", "KEINE MODS", "НЕТ МОДОВ", "MODなし", "无模组" },
  [kUi_Skins] = { "SKINS", "APPARENCES", "ASPECTOS", "VISUAIS", "ASPETTI", "SKINS", "ОБЛИКИ", "スキン", "皮肤" },
  [kUi_Continue] = { "CONTINUE", "CONTINUER", "CONTINUAR", "CONTINUAR", "CONTINUA", "WEITER", "ПРОДОЛЖИТЬ", "続ける", "继续" },
  [kUi_Quit] = { "QUIT", "QUITTER", "SALIR", "SAIR", "ESCI", "BEENDEN", "ВЫХОД", "終了", "退出" },
  [kUi_Mods] = { "MODS", "MODS", "MODS", "MODS", "MOD", "MODS", "МОДЫ", "MOD", "模组" },
  [kUi_Loading] = { "LOADING", "CHARGEMENT", "CARGANDO", "CARREGANDO", "CARICAMENTO", "LADEN", "ЗАГРУЗКА", "読み込み中", "载入中" },
  [kUi_RandoInvalidFile] = { "THIS JSON FILE IS INVALID OR TOO LARGE.", "CE FICHIER JSON EST INVALIDE OU TROP GRAND.", "EL JSON NO ES VÁLIDO O ES DEMASIADO GRANDE.", "O JSON É INVÁLIDO OU MUITO GRANDE.", "IL FILE JSON NON È VALIDO O È TROPPO GRANDE.", "DIE JSON-DATEI IST UNGÜLTIG ODER ZU GROSS.", "ФАЙЛ JSON ПОВРЕЖДЁН ИЛИ СЛИШКОМ БОЛЬШОЙ.", "このJSONファイルは、無効か　大きすぎます。", "该 JSON 文件无效或过大。" },
  [kUi_RandoUnsupported] = { "THIS COMBINATION OF SEED RULES IS NOT AVAILABLE YET.", "CETTE COMBINAISON DE RÈGLES N'EST PAS ENCORE DISPONIBLE.", "ESTA COMBINACIÓN DE REGLAS AÚN NO ESTÁ DISPONIBLE.", "ESTA COMBINAÇÃO DE REGRAS AINDA NÃO ESTÁ DISPONÍVEL.", "QUESTA COMBINAZIONE DI REGOLE NON È ANCORA DISPONIBILE.", "DIESE KOMBINATION VON REGELN IST NOCH NICHT VERFÜGBAR.", "ЭТО СОЧЕТАНИЕ ПРАВИЛ ПОКА НЕ ПОДДЕРЖИВАЕТСЯ.", "このシードのルールの組み合わせには、まだ対応していません。", "此种子规则组合暂不可用。" },
  [kUi_RandoMissingData] = { "EXACT PLACEMENTS ARE MISSING. EXPORT THE FULL SPOILER JSON FROM ALttPR.", "PLACEMENTS MANQUANTS. EXPORTEZ LE JSON COMPLET DU SPOILER ALttPR.", "FALTAN LAS UBICACIONES DE LOS PREMIOS. EXPORTA EL JSON COMPLETO DE ALttPR.", "FALTAM AS POSIÇÕES DOS ITENS. EXPORTE O JSON COMPLETO DO ALttPR.", "MANCANO LE POSIZIONI DEGLI OGGETTI. ESPORTA IL JSON COMPLETO DI ALttPR.", "GEGENSTANDSORTE FEHLEN. VOLLSTÄNDIGEN ALttPR-SPOILER ALS JSON EXPORTIEREN.", "НЕТ РАСПОЛОЖЕНИЯ ПРЕДМЕТОВ. ЭКСПОРТИРУЙТЕ ПОЛНЫЙ JSON СПОЙЛЕРА ALttPR.", "アイテムの配置データがありません。　ALttPRのスポイラーJSONを　完全な形で書き出してください。", "缺少道具放置数据，请导出 ALttPR 完整剧透 JSON。" },
  [kUi_RandoUncertified] = { "THIS EXACT SEED COULD NOT BE VERIFIED. NO SAVE WAS CREATED.", "CETTE GRAINE N'A PAS PU ÊTRE VÉRIFIÉE. AUCUNE SAUVEGARDE CRÉÉE.", "NO SE PUDO VERIFICAR ESTA SEMILLA. NO SE CREÓ NINGUNA PARTIDA.", "NÃO FOI POSSÍVEL VERIFICAR A SEMENTE. NENHUM JOGO FOI CRIADO.", "IMPOSSIBILE VERIFICARE QUESTO SEME. NESSUN SALVATAGGIO CREATO.", "DIESER SEED KONNTE NICHT GEPRÜFT WERDEN. KEIN SPIELSTAND ERSTELLT.", "НЕ УДАЛОСЬ ПРОВЕРИТЬ ЭТОТ СИД. СОХРАНЕНИЕ НЕ СОЗДАНО.", "このシードを検証できませんでした。　セーブデータは作成されていません。", "无法校验此种子，未创建存档。" },
  [kUi_RandoStorageError] = { "UNABLE TO READ OR SAVE THIS SEED. PREVIOUS FILES WERE KEPT.", "LECTURE OU ÉCRITURE IMPOSSIBLE. LES FICHIERS PRÉCÉDENTS SONT CONSERVÉS.", "NO SE PUDO LEER O GUARDAR LA SEMILLA. SE CONSERVARON LOS ARCHIVOS ANTERIORES.", "NÃO FOI POSSÍVEL LER OU SALVAR. OS ARQUIVOS ANTERIORES FORAM MANTIDOS.", "IMPOSSIBILE LEGGERE O SALVARE. I FILE PRECEDENTI SONO STATI CONSERVATI.", "SEED KANN NICHT GELESEN ODER GESPEICHERT WERDEN. ALTE DATEIEN BLEIBEN ERHALTEN.", "НЕ УДАЛОСЬ ПРОЧИТАТЬ ИЛИ ЗАПИСАТЬ СИД. ПРЕЖНИЕ ФАЙЛЫ СОХРАНЕНЫ.", "このシードを読み込みまたは保存できません。　以前のファイルは　そのまま残っています。", "无法读取或保存此种子，原文件已保留。" },
  [kUi_RandoSourceChanged] = { "THE IMPORTED JSON IS MISSING OR CHANGED. RESTORE ITS ORIGINAL COPY.", "LE JSON IMPORTÉ MANQUE OU A CHANGÉ. RESTAUREZ SA COPIE D'ORIGINE.", "FALTA EL JSON IMPORTADO O HA CAMBIADO. RESTAURA SU COPIA ORIGINAL.", "O JSON IMPORTADO SUMIU OU MUDOU. RESTAURE A CÓPIA ORIGINAL.", "IL JSON IMPORTATO MANCA O È CAMBIATO. RIPRISTINA LA COPIA ORIGINALE.", "IMPORTIERTE JSON-DATEI FEHLT ODER WURDE GEÄNDERT. ORIGINAL WIEDERHERSTELLEN.", "ИМПОРТИРОВАННЫЙ JSON ИЗМЕНЁН ИЛИ ОТСУТСТВУЕТ. ВЕРНИТЕ ИСХОДНУЮ КОПИЮ.", "読み込んだJSONが、見つからないか変更されています。　元のファイルを戻してください。", "导入的 JSON 丢失或已改动，请恢复原始文件。" },
  [kUi_SaveState] = { "SAVE STATE", "SAUVER ÉTAT", "GUARDAR ESTADO", "SALVAR ESTADO", "SALVA STATO", "ZUSTAND SICHERN", "СОХРАНИТЬ", "ステート保存", "保存状态" },
  [kUi_LoadDiagnostic] = { "LOAD DUMP", "CHARGER DUMP", "CARGAR DUMP", "CARREGAR DUMP", "CARICA DUMP", "DUMP LADEN", "ЗАГРУЗИТЬ ДАМП", "ダンプ読込", "载入转储" },
  [kUi_LoadSavedState] = { "LOAD THE SAVED STATE", "CHARGER L'ÉTAT SAUVÉ", "CARGAR EL ESTADO GUARDADO", "CARREGAR O ESTADO SALVO", "CARICA LO STATO SALVATO", "GESICHERTEN ZUSTAND LADEN", "ЗАГРУЗИТЬ СОХРАНЕНИЕ", "保存ステート読み込み", "载入已存状态" },
  [kUi_Tests] = { "TESTS", "TESTS", "PRUEBAS", "TESTES", "TEST", "TESTS", "ТЕСТЫ", "テスト", "测试" },
  [kUi_TestsHelp] = { "A: LOAD   B: BACK   L/R: PAGE", "A : CHARGER   B : RETOUR", "A: CARGAR   B: VOLVER   L/R: PÁGINA", "A: CARREGAR   B: VOLTAR", "A: CARICA   B: INDIETRO", "A: LADEN   B: ZURÜCK", "A: ЗАГРУЗИТЬ   B: НАЗАД", "A：読込   B：戻る   L/R：ページ", "A: 载入   B: 返回   L/R: 翻页" },
  [kUi_ScenarioMissing] = { "NO STATE FILE IN THIS BUILD", "FICHIER D'ÉTAT ABSENT", "ESTA VERSIÓN NO TIENE EL ESTADO", "ESTADO AUSENTE NESTA VERSÃO", "STATO ASSENTE IN QUESTA VERSIONE", "ZUSTAND FEHLT IN DIESER VERSION", "НЕТ ФАЙЛА СОСТОЯНИЯ", "このバージョンにステートなし", "此版本无状态文件" },
  [kUi_CurrentSeed] = { "SEED IN PLAY", "GRAINE EN COURS", "SEMILLA ACTUAL", "SEMENTE ATUAL", "SEME IN CORSO", "AKTUELLER SEED", "ТЕКУЩИЙ СИД", "現在のシード", "当前种子" },
  [kUi_LeaveRandomizer] = { "RETURN TO THE NORMAL GAME?", "REVENIR AU JEU NORMAL?", "VOLVER AL JUEGO NORMAL?", "VOLTAR AO JOGO NORMAL?", "TORNARE AL GIOCO NORMALE?", "ZUM NORMALEN SPIEL?", "ВЕРНУТЬСЯ К ОБЫЧНОЙ ИГРЕ?", "通常のゲームに戻りますか？", "返回普通游戏？" },
  [kUi_ImportSeed] = { "IMPORT", "IMPORTER", "IMPORTAR", "IMPORTAR", "IMPORTA", "IMPORT", "ИМПОРТ", "インポート", "导入" },
  [kUi_ImportHelp] = { "COPY AN ALttPR SPOILER JSON HERE:", "COPIEZ UN JSON ALttPR ICI :", "COPIA UN JSON DE ALttPR AQUÍ:", "COPIE UM JSON DE ALttPR AQUI:", "COPIA UN JSON ALttPR QUI:", "ALttPR-SPOILER-JSON HIER ABLEGEN:", "СКОПИРУЙТЕ JSON ALttPR СЮДА:", "ALttPRのスポイラーJSONを　ここにコピー：", "将 ALttPR 剧透 JSON 复制到此处:" },
  [kUi_SeedList] = { "SEEDS", "GRAINES", "SEMILLAS", "SEMENTES", "SEMI", "SEEDS", "СИДЫ", "シード", "种子" },
  [kUi_Randomizer] = { "RANDOMIZER", "RANDOMIZER", "RANDOMIZER", "RANDOMIZER", "RANDOMIZER", "RANDOMIZER", "РАНДОМАЙЗЕР", "ランダマイザー", "随机化" },
  [kUi_OpenRandomizer] = { "START THIS SEED?", "LANCER CETTE GRAINE ?", "INICIAR ESTA SEMILLA?", "INICIAR ESTA SEMENTE?", "AVVIARE QUESTO SEME?", "DIESEN SEED STARTEN?", "ЗАПУСТИТЬ ЭТОТ СИД?", "シードを始めますか？", "开始此种子？" },
  [kUi_Seed] = { "SEED", "GRAINE", "SEMILLA", "SEMENTE", "SEME", "SEED", "СИД", "シード", "种子" },
  [kUi_Preset] = { "MODE", "MODE", "MODO", "MODO", "MODALITÀ", "MODUS", "РЕЖИМ", "モード", "模式" },
  [kUi_GenerateSeed] = { "GENERATE", "GÉNÉRER", "GENERAR", "GERAR", "GENERA", "ERSTELLEN", "СОЗДАТЬ", "生成", "生成" },
  [kUi_SavedSeeds] = { "SAVED SEEDS", "GRAINES SAUVÉES", "SEMILLAS GUARDADAS", "SEMENTES SALVAS", "SEMI SALVATI", "GESPEICHERTE SEEDS", "СОХРАНЁННЫЕ СИДЫ", "保存シード", "已存种子" },
  [kUi_OrdinaryGame] = { "NORMAL GAME", "JEU NORMAL", "JUEGO NORMAL", "JOGO NORMAL", "GIOCO NORMALE", "NORMALES SPIEL", "ОБЫЧНАЯ ИГРА", "通常ゲーム", "普通游戏" },
  [kUi_NoSeeds] = { "NO SAVED SEEDS", "AUCUNE GRAINE SAUVÉE", "NO HAY SEMILLAS GUARDADAS", "NENHUMA SEMENTE SALVA", "NESSUN SEME SALVATO", "KEINE GESPEICHERTEN SEEDS", "НЕТ СОХРАНЁННЫХ СИДОВ", "保存シードなし", "无已存种子" },
  [kUi_SkinsOnly] = { "GAME TEXT: ENGLISH. SKINS ONLY. NORMAL SAVES STAY SEPARATE.", "TEXTES DU JEU EN ANGLAIS. SKINS SEULS. SAUVEGARDES NORMALES SÉPARÉES.", "TEXTO EN INGLÉS. SOLO SKINS. PARTIDAS NORMALES SEPARADAS.", "TEXTO EM INGLÊS. SÓ SKINS. PARTIDAS NORMAIS SEPARADAS.", "TESTO IN INGLESE. SOLO SKIN. SALVATAGGI NORMALI SEPARATI.", "SPIELTEXT AUF ENGLISCH. NUR SKINS. NORMALE SPIELSTÄNDE GETRENNT.", "ТЕКСТ ИГРЫ НА АНГЛИЙСКОМ. ТОЛЬКО СКИНЫ. ОБЫЧНЫЕ СОХРАНЕНИЯ ОТДЕЛЬНО.", "ゲーム内の文章は英語です。　スキンのみ使えます。　通常のセーブデータとは別です。", "游戏文本为英文，仅皮肤，普通存档独立。" },
  [kUi_RandoOpen] = { "OPEN", "OUVERT", "ABIERTO", "ABERTO", "APERTO", "OFFEN", "ОТКРЫТЫЙ", "オープン", "开放" },
  [kUi_RandoStandard] = { "STANDARD", "STANDARD", "ESTÁNDAR", "PADRÃO", "STANDARD", "STANDARD", "СТАНДАРТНЫЙ", "スタンダード", "标准" },
  [kUi_RandoInverted] = { "INVERTED", "INVERSÉ", "INVERTIDO", "INVERTIDO", "INVERTITO", "INVERTIERT", "ИНВЕРСИЯ", "反転", "反转" },
  [kUi_RandoRetro] = { "RETRO", "RÉTRO", "RETRO", "RETRÔ", "RETRO", "RETRO", "РЕТРО", "レトロ", "复古" },
  [kUi_RandoTriforce] = { "TRIFORCE HUNT", "CHASSE TRIFORCE", "BUSCAR TRIFUERZA", "CAÇA À TRIFORCE", "TRIFORZA", "TRIFORCE-JAGD", "ТРИФОРС", "トライフォース探し", "三角神力" },
  [kUi_Delete] = { "DELETE", "EFFACER", "BORRAR", "APAGAR", "CANCELLA", "LÖSCHEN", "УДАЛИТЬ", "削除", "删除" },
  [kUi_ChangeGameLanguage] = { "CHANGE GAME LANGUAGE?", "CHANGER LA LANGUE DU JEU?", "CAMBIAR EL IDIOMA DEL JUEGO?", "MUDAR O IDIOMA DO JOGO?", "CAMBIARE LINGUA DEL GIOCO?", "SPIELSPRACHE ÄNDERN?", "СМЕНИТЬ ЯЗЫК ИГРЫ?", "ゲーム言語を変更しますか？", "更改游戏语言？" },
  [kUi_No] = { "NO", "NON", "NO", "NÃO", "NO", "NEIN", "НЕТ", "いいえ", "否" },
  [kUi_Yes] = { "YES", "OUI", "SÍ", "SIM", "SÌ", "JA", "ДА", "はい", "是" },
  [kUi_ErrorSdWrite] = { "SD CARD WRITE FAILED", "ÉCRITURE SD IMPOSSIBLE", "ERROR AL ESCRIBIR EN SD", "ERRO AO GRAVAR NO SD", "ERRORE SCRITTURA SD", "SD-SCHREIBFEHLER", "ОШИБКА ЗАПИСИ НА SD", "SDカードに書き込めません", "SD 卡写入失败" },
  [kUi_ErrorWifi] = { "NO WI-FI CONNECTION", "AUCUNE CONNEXION WI-FI", "SIN CONEXIÓN WI-FI", "SEM CONEXÃO WI-FI", "NESSUNA CONNESSIONE WI-FI", "KEINE WLAN-VERBINDUNG", "НЕТ СОЕДИНЕНИЯ WI-FI", "WI-FIに接続できません", "未连接 WI-FI" },
  [kUi_ErrorTls] = { "TLS SERVICE FAILED", "ÉCHEC DU SERVICE TLS", "ERROR DEL SERVICIO TLS", "ERRO NO SERVIÇO TLS", "ERRORE SERVIZIO TLS", "TLS-DIENST FEHLGESCHLAGEN", "ОШИБКА СЛУЖБЫ TLS", "TLSサービスエラー", "TLS 服务失败" },
  [kUi_ErrorRelease] = { "INVALID RELEASE DATA", "DONNÉES DE VERSION INVALIDES", "DATOS DE VERSIÓN INVÁLIDOS", "DADOS DE VERSÃO INVÁLIDOS", "DATI VERSIONE NON VALIDI", "UNGÜLTIGE VERSIONSDATEN", "НЕВЕРНЫЕ ДАННЫЕ ВЕРСИИ", "バージョン情報が無効です", "版本数据无效" },
  [kUi_ErrorSpace] = { "NOT ENOUGH SD SPACE", "ESPACE SD INSUFFISANT", "ESPACIO INSUFICIENTE EN SD", "ESPAÇO INSUFICIENTE NO SD", "SPAZIO SD INSUFFICIENTE", "ZU WENIG SD-SPEICHER", "НЕДОСТАТОЧНО МЕСТА НА SD", "SDの空き容量が不足", "SD 卡空间不足" },
  [kUi_ErrorLaunchPath] = { "3DSX LAUNCH PATH UNKNOWN", "CHEMIN DU 3DSX INCONNU", "RUTA DEL 3DSX DESCONOCIDA", "CAMINHO DO 3DSX DESCONHECIDO", "PERCORSO 3DSX SCONOSCIUTO", "3DSX-STARTPFAD UNBEKANNT", "ПУТЬ К 3DSX НЕИЗВЕСТЕН", "3DSXの起動パスが不明", "3DSX 启动路径未知" },
  [kUi_ErrorDownload] = { "DOWNLOAD CHECK FAILED", "TÉLÉCHARGEMENT NON VALIDE", "DESCARGA NO VÁLIDA", "DOWNLOAD INVÁLIDO", "DOWNLOAD NON VALIDO", "DOWNLOAD-PRÜFUNG FEHLGESCHLAGEN", "ОШИБКА ПРОВЕРКИ ЗАГРУЗКИ", "ダウンロードの検証に失敗", "下载校验失败" },
  [kUi_ErrorInstall] = { "INSTALLATION FAILED", "ÉCHEC DE L'INSTALLATION", "ERROR DE INSTALACIÓN", "ERRO DE INSTALAÇÃO", "INSTALLAZIONE FALLITA", "INSTALLATION FEHLGESCHLAGEN", "ОШИБКА УСТАНОВКИ", "インストール失敗", "安装失败" },
  [kUi_ErrorCancelled] = { "UPDATE CANCELLED", "MISE À JOUR ANNULÉE", "ACTUALIZACIÓN CANCELADA", "ATUALIZAÇÃO CANCELADA", "AGGIORNAMENTO ANNULLATO", "UPDATE ABGEBROCHEN", "ОБНОВЛЕНИЕ ОТМЕНЕНО", "更新を中止しました", "更新已取消" },
  [kUi_ErrorConnection] = { "CONNECTION FAILED - RETRY", "CONNEXION IMPOSSIBLE - RÉESSAYER", "FALLO DE CONEXIÓN - REINTENTA", "FALHA DE CONEXÃO - TENTE DE NOVO", "CONNESSIONE FALLITA - RIPROVA", "VERBINDUNGSFEHLER - ERNEUT VERSUCHEN", "НЕТ СВЯЗИ - ПОВТОРИТЕ", "接続に失敗しました - 再試行", "连接失败 - 重试" },
  [kUi_ErrorStart] = { "CANNOT START UPDATE", "DÉMARRAGE IMPOSSIBLE", "NO SE PUEDE INICIAR", "NÃO FOI POSSÍVEL INICIAR", "IMPOSSIBILE AVVIARE", "UPDATE-START FEHLGESCHLAGEN", "НЕ УДАЛОСЬ НАЧАТЬ", "更新を開始できません", "无法开始更新" },
  [kUi_ErrorChannel] = { "CANNOT SAVE UPDATE CHANNEL", "ENREGISTREMENT DU CANAL IMPOSSIBLE", "NO SE PUEDE GUARDAR EL CANAL", "NÃO FOI POSSÍVEL SALVAR O CANAL", "IMPOSSIBILE SALVARE IL CANALE", "KANAL KANN NICHT GESPEICHERT WERDEN", "НЕ УДАЛОСЬ СОХРАНИТЬ КАНАЛ", "更新チャンネルを保存できません", "无法保存更新通道" },
  [kUi_Changes] = { "CHANGES", "NOUVEAUTÉS", "CAMBIOS", "NOVIDADES", "NOVITÀ", "NEUERUNGEN", "ИЗМЕНЕНИЯ", "更新内容", "更新内容" },
  [kUi_Page] = { "PAGE", "PAGE", "PÁGINA", "PÁGINA", "PAGINA", "SEITE", "СТРАНИЦА", "ページ", "页" },
  [kUi_XHold] = { "X HOLD", "X MAINT.", "MANT. X", "SEGURAR X", "TIENI X", "X HALTEN", "ДЕРЖАТЬ X", "X長押し", "按住 X" },
  [kUi_Checking] = { "CHECKING FOR UPDATES", "RECHERCHE EN COURS", "BUSCANDO ACTUALIZACIÓN", "BUSCANDO ATUALIZAÇÃO", "RICERCA AGGIORNAMENTI", "SUCHE NACH UPDATES", "ПОИСК ОБНОВЛЕНИЙ", "更新を確認中", "正在检查更新" },
  [kUi_UpToDate] = { "YOU ARE UP TO DATE", "VERSION À JOUR", "ESTÁ ACTUALIZADO", "ESTÁ ATUALIZADO", "VERSIONE AGGIORNATA", "AUF DEM NEUESTEN STAND", "ВЕРСИЯ АКТУАЛЬНА", "最新の状態です", "已是最新版本" },
  [kUi_UpdateReady] = { "UPDATE AVAILABLE", "MISE À JOUR DISPONIBLE", "ACTUALIZACIÓN DISPONIBLE", "ATUALIZAÇÃO DISPONÍVEL", "AGGIORNAMENTO DISPONIBILE", "UPDATE VERFÜGBAR", "ЕСТЬ ОБНОВЛЕНИЕ", "更新があります", "有可用更新" },
  [kUi_NoUpdate] = { "NO UPDATE AVAILABLE", "AUCUNE MISE À JOUR", "SIN ACTUALIZACIONES", "SEM ATUALIZAÇÕES", "NESSUN AGGIORNAMENTO", "KEIN UPDATE VERFÜGBAR", "НЕТ ОБНОВЛЕНИЙ", "更新はありません", "没有可用更新" },
  [kUi_Downloading] = { "DOWNLOADING", "TÉLÉCHARGEMENT", "DESCARGANDO", "BAIXANDO", "SCARICAMENTO", "HERUNTERLADEN", "ЗАГРУЗКА", "ダウンロード中", "正在下载" },
  [kUi_Verifying] = { "VERIFYING DOWNLOAD", "VÉRIFICATION", "VERIFICANDO DESCARGA", "VERIFICANDO DOWNLOAD", "VERIFICA DOWNLOAD", "DOWNLOAD WIRD GEPRÜFT", "ПРОВЕРКА ЗАГРУЗКИ", "ダウンロードを検証中", "正在校验下载" },
  [kUi_Installing] = { "INSTALLING UPDATE", "INSTALLATION EN COURS", "INSTALANDO ACTUALIZACIÓN", "INSTALANDO ATUALIZAÇÃO", "INSTALLAZIONE IN CORSO", "UPDATE WIRD INSTALLIERT", "УСТАНОВКА ОБНОВЛЕНИЯ", "更新をインストール中", "正在安装更新" },
  [kUi_UpdateError] = { "UPDATE FAILED", "ÉCHEC DE LA MISE À JOUR", "ERROR AL ACTUALIZAR", "ERRO AO ATUALIZAR", "AGGIORNAMENTO FALLITO", "UPDATE FEHLGESCHLAGEN", "ОШИБКА ОБНОВЛЕНИЯ", "更新に失敗しました", "更新失败" },
  [kUi_StateLoaded] = { "LOADED", "CHARGÉ", "CARGADO", "CARREGADO", "CARICATO", "GELADEN", "ЗАГРУЖЕНО", "読込完了", "已载入" },
  [kUi_NoDump] = { "NO DUMP", "AUCUN VIDAGE", "SIN COPIA", "SEM CÓPIA", "NESSUNA COPIA", "KEINE KOPIE", "НЕТ КОПИИ", "ダンプなし", "无转储" },
  [kUi_NoState] = { "NO STATE", "AUCUN ÉTAT", "SIN ESTADO", "SEM ESTADO", "NESSUNO STATO", "KEIN ZUSTAND", "НЕТ СОСТОЯНИЯ", "ステートなし", "无状态" },
  [kUi_InvalidState] = { "INVALID", "INVALIDE", "INVÁLIDO", "INVÁLIDO", "NON VALIDO", "UNGÜLTIG", "НЕВЕРНЫЙ ФАЙЛ", "無効", "无效" },
  [kUi_WrongGame] = { "OTHER GAME", "AUTRE JEU", "OTRO JUEGO", "OUTRO JOGO", "ALTRO GIOCO", "ANDERES SPIEL", "ДРУГАЯ ИГРА", "別のゲーム", "其他游戏" },
  [kUi_IoError] = { "READ ERROR", "ERREUR LECTURE", "ERROR LECTURA", "ERRO LEITURA", "ERRORE LETTURA", "LESEFEHLER", "ОШИБКА ЧТЕНИЯ", "読込エラー", "读取错误" },
  [kUi_Gameplay] = { "GAME", "JEU", "JUEGO", "JOGO", "GIOCO", "SPIEL", "ИГРА", "ゲーム", "游戏" },
  [kUi_TurboControl] = { "TURBO BUTTON", "TOUCHE TURBO", "BOTÓN TURBO", "BOTÃO TURBO", "TASTO TURBO", "TURBOTASTE", "КНОПКА ТУРБО", "ターボボタン", "加速键" },
  [kUi_Autosave] = { "AUTO STATE", "ÉTAT AUTO", "ESTADO AUTO", "ESTADO AUTO", "STATO AUTO", "AUTO ZUSTAND", "АВТОСОСТ.", "自動ステート", "自动状态" },
  [kUi_GameLanguage] = { "GAME LANGUAGE", "LANGUE DU JEU", "IDIOMA JUEGO", "IDIOMA DO JOGO", "LINGUA GIOCO", "SPIELSPRACHE", "ЯЗЫК ИГРЫ", "ゲームの言語", "游戏语言" },
  [kUi_Gear] = { "GEAR", "\xC3\x89QUIP.", "EQUIPO", "EQUIP.", "STATO", "STATUS", "СТАТУС", "装備", "装备" },
  [kUi_Map] = { "MAP", "CARTE", "MAPA", "MAPA", "MAPPA", "KARTE", "КАРТА", "地図", "地图" },
  [kUi_Items] = { "ITEMS", "OBJETS", "OBJETOS", "ITENS", "OGGETTI", "ITEMS", "ВЕЩИ", "アイテム", "道具" },
  [kUi_Dungeon] = { "DUNGEON", "DONJON", "MAZMORRA", "MASMORRA", "DUNGEON", "VERLIES", "ПОДЗЕМЕЛЬЕ", "ダンジョン", "迷宫" },
  [kUi_Bottles] = { "BOTTLES", "FLACONS", "BOTELLAS", "GARRAFAS", "BOTTIGLIE", "FLASCHEN", "БУТЫЛКИ", "ビン", "瓶子" },
  [kUi_Pendants] = { "PENDANTS", "PENDENTIFS", "COLGANTES", "PINGENTES", "PENDENTI", "AMULETTE", "КУЛОНЫ", "紋章", "吊坠" },
  [kUi_Crystals] = { "CRYSTALS", "CRISTAUX", "CRISTALES", "CRISTAIS", "CRISTALLI", "KRISTALLE", "КРИСТАЛЛЫ", "クリスタル", "水晶" },
  [kUi_Settings] = { "SETTINGS", "OPTIONS", "AJUSTES", "OPÇÕES", "OPZIONI", "OPTIONEN", "НАСТРОЙКИ", "設定", "设置" },
  [kUi_Screen] = { "SCREEN", "\xC3\x89""CRAN", "PANTALLA", "TELA", "SCHERMO", "BILD", "ЭКРАН", "画面", "画面" },
  [kUi_TurboSpeed] = { "TURBO SPEED", "VITESSE TURBO", "VELOCIDAD", "VELOCIDADE", "VELOCITÀ", "TEMPO", "СКОРОСТЬ", "ターボ速度", "加速倍率" },
  [kUi_Developer] = { "DEVELOPER", "D\xC3\x89V.", "DESARROLLO", "DESENVOLV.", "SVILUPPO", "ENTWICKLER", "ОТЛАДКА", "開発者", "开发者选项" },
  [kUi_Update] = { "UPDATE", "MISE \xC3\x80 JOUR", "ACTUALIZAR", "ATUALIZAR", "AGGIORNA", "UPDATE", "ОБНОВЛЕНИЕ", "更新", "更新" },
  [kUi_Restart] = { "RESTART", "RED\xC3\x89MARRER", "REINICIAR", "REINICIAR", "RIAVVIA", "NEUSTART", "ПЕРЕЗАПУСК", "再起動", "重启" },
  [kUi_Language] = { "LANGUAGE", "LANGUE", "IDIOMA", "IDIOMA", "LINGUA", "SPRACHE", "ЯЗЫК", "言語", "语言" },
  [kUi_Back] = { "BACK", "RETOUR", "VOLVER", "VOLTAR", "INDIETRO", "ZURÜCK", "НАЗАД", "戻る", "返回" },
  [kUi_Off] = { "OFF", "NON", "NO", "NÃO", "NO", "AUS", "ВЫКЛ.", "オフ", "关闭" },
  [kUi_On] = { "ON", "OUI", "SÍ", "SIM", "SÌ", "AN", "ВКЛ.", "オン", "开启" },
  [kUi_Open] = { "OPEN", "OUVRIR", "ABRIR", "ABRIR", "APRI", "ÖFFNEN", "ОТКРЫТЬ", "開く", "打开" },
  [kUi_Cancel] = { "CANCEL", "ANNULER", "CANCELAR", "CANCELAR", "ANNULLA", "ABBRUCH", "ОТМЕНА", "キャンセル", "取消" },
  [kUi_DisplayMode] = { "DISPLAY MODE", "AFFICHAGE", "VISTA", "EXIBIÇÃO", "VISUALIZZA", "ANSICHT", "РЕЖИМ", "表示モード", "显示模式" },
  [kUi_EdgeMode] = { "EDGE MODE", "MODE BORDS", "CÁMARA", "CÂMERA", "CAMERA", "KAMERA", "КРАЯ", "カメラ", "边缘模式" },
  [kUi_Zoom] = { "ZOOM", "ZOOM", "ZOOM", "ZOOM", "ZOOM", "ZOOM", "МАСШТАБ", "ズーム", "缩放" },
  [kUi_TopHud] = { "TOP HUD", "HUD HAUT", "HUD SUPERIOR", "HUD SUPERIOR", "HUD IN ALTO", "OBERES HUD", "ВЕРХНИЙ HUD", "上部HUD", "上屏 HUD" },
  [kUi_Wide] = { "WIDE", "LARGE", "AMPLIA", "AMPLA", "AMPIO", "BREIT", "ШИРОКИЙ", "WIDE", "宽屏" },
  [kUi_Original] = { "ORIGINAL", "ORIGINAL", "ORIGINAL", "ORIGINAL", "ORIGINALE", "ORIGINAL", "ИСХОДНЫЙ", "オリジナル", "原始" },
  [kUi_Stretch] = { "STRETCH", "\xC3\x89TIR\xC3\x89", "ESTIRADA", "ESTICADA", "ESTESO", "GESTRECKT", "РАСТЯНУТЬ", "引きのばし", "拉伸" },
  [kUi_FixedCamera] = { "FIXED CAMERA", "CAM\xC3\x89RA FIXE", "FIJA", "FIXA", "FISSA", "FEST", "ФИКС.", "カメラ固定", "固定" },
  [kUi_Standard] = { "STANDARD", "STANDARD", "NORMAL", "PADRÃO", "NORMALE", "STANDARD", "ОБЫЧНЫЙ", "標準", "标准" },
  [kUi_MemDump] = { "DUMP", "DUMP", "DUMP", "DUMP", "DUMP", "DUMP", "ДАМП", "ダンプ", "转储" },
  [kUi_LoadState] = { "LOAD STATE", "CHARGER \xC3\x89TAT", "CARGAR ESTADO", "CARREGAR ESTADO", "CARICA STATO", "ZUSTAND LADEN", "ЗАГРУЗИТЬ", "ステート読込", "载入状态" },
  [kUi_ShowFps] = { "SHOW FPS", "AFFICHER FPS", "MOSTRAR FPS", "MOSTRAR FPS", "MOSTRA FPS", "FPS ANZEIGEN", "ПОКАЗ FPS", "FPS表示", "显示 FPS" },
  [kUi_Overlay] = { "OVERLAY", "INFOS", "INFORMACIÓN", "INFORMAÇÕES", "INFORMAZIONI", "INFORMATION", "СВЕДЕНИЯ", "情報表示", "叠加信息" },
  [kUi_Write] = { "WRITE", "\xC3\x89""CRIRE", "CREAR", "CRIAR", "CREA", "ERSTELLEN", "СОЗДАТЬ", "作成", "写入" },
  [kUi_Done] = { "DONE", "FAIT", "LISTO", "PRONTO", "FATTO", "FERTIG", "ГОТОВО", "完了", "完成" },
  [kUi_Wait] = { "WAIT", "ATTENTE", "ESPERA", "AGUARDE", "ATTENDI", "WARTEN", "ЖДИТЕ", "待機中", "等待" },
  [kUi_Load] = { "LOAD", "CHARGER", "CARGAR", "CARREGAR", "CARICA", "LADEN", "ЗАГРУЗИТЬ", "読み込む", "载入" },
  [kUi_LoadDumpLine1] = { "LOAD LATEST STATE", "CHARGER LE DERNIER ÉTAT", "CARGAR ÚLTIMO ESTADO", "CARREGAR ÚLTIMO ESTADO", "CARICA ULTIMO STATO", "LETZTEN ZUSTAND LADEN", "ЗАГРУЗИТЬ СОСТОЯНИЕ", "最新ステートを読み込む", "载入最新状态" },
  [kUi_LoadDumpLine2] = { "FOR THIS GAME", "POUR CE JEU", "DE ESTE JUEGO", "DESTE JOGO", "DI QUESTO GIOCO", "FÜR DIESES SPIEL", "ДЛЯ ЭТОЙ ИГРЫ", "このゲーム用", "本游戏" },
  [kUi_ProgressLost] = { "UNSAVED PROGRESS IS LOST", "PROGRESSION NON SAUV\xC3\x89""E PERDUE", "SE PIERDE LO NO GUARDADO", "PROGRESSO NÃO SALVO SERÁ PERDIDO", "PROGRESSI NON SALVATI PERSI", "UNGESPEICHERTES GEHT VERLOREN", "НЕСОХРАНЁННЫЙ ПРОГРЕСС БУДЕТ ПОТЕРЯН", "未保存の進行状況は失われます", "未保存的进度将丢失" },
  [kUi_Version] = { "VERSION", "VERSION", "VERSIÓN", "VERSÃO", "VERSIONE", "VERSION", "ВЕРСИЯ", "バージョン", "版本" },
  [kUi_Model] = { "MODEL", "MOD\xC3\x88LE", "MODELO", "MODELO", "MODELLO", "MODELL", "МОДЕЛЬ", "機種", "型号" },
  [kUi_FpsNow] = { "FPS NOW", "FPS ACT", "FPS ACTUAL", "FPS ATUAL", "FPS ATTUALI", "FPS AKTUELL", "ТЕКУЩИЙ FPS", "FPS現在", "当前 FPS" },
  [kUi_FpsAvg] = { "FPS AVG", "FPS MOY", "FPS MEDIO", "FPS MÉDIO", "FPS MEDI", "FPS MITTEL", "СРЕДНИЙ FPS", "FPS平均", "平均 FPS" },
  [kUi_Core1] = { "CORE1", "COEUR1", "NÚCLEO 1", "NÚCLEO 1", "CORE 1", "KERN 1", "ЯДРО 1", "コア1", "核心 1" },
  [kUi_Room] = { "ROOM", "SALLE", "SALA", "SALA", "STANZA", "RAUM", "КОМНАТА", "部屋", "房间" },
  [kUi_Module] = { "MODULE", "MODULE", "MÓDULO", "MÓDULO", "MODULO", "MODUL", "МОДУЛЬ", "モジュール", "模块" },
  [kUi_House] = { "HOUSE", "MAISON", "CASA", "CASA", "CASA", "HAUS", "ДОМ", "家", "房屋" },
  [kUi_Overworld] = { "OVERWORLD", "MONDE", "MUNDO", "MUNDO", "MONDO", "WELT", "МИР", "地上", "地上世界" },
  [kUi_RemapButtons] = { "REMAP BUTTONS", "TOUCHES", "CONTROLES", "CONTROLES", "COMANDI", "TASTEN", "КНОПКИ", "ボタン設定", "按键映射" },
  [kUi_More] = { "MORE", "SUITE", "MÁS", "MAIS", "ALTRO", "MEHR", "ДАЛЕЕ", "つづき", "更多" },
  [kUi_Top] = { "TOP", "D\xC3\x89""BUT", "INICIO", "INÍCIO", "INIZIO", "ANFANG", "ВВЕРХ", "先頭へ", "顶部" },
  [kUi_PressKey] = { "PRESS KEY", "APPUYEZ", "PULSA", "PRESSIONE", "PREMI", "DRÜCKEN", "НАЖМИТЕ", "ボタンを押す", "请按键" },
  [kUi_Channel] = { "CHANNEL", "CANAL", "CANAL", "CANAL", "CANALE", "KANAL", "КАНАЛ", "チャンネル", "通道" },
  [kUi_Stable] = { "STABLE", "STABLE", "ESTABLE", "ESTÁVEL", "STABILE", "STABIL", "СТАБИЛЬНЫЙ", "安定版", "稳定版" },
  [kUi_Prerelease] = { "PRE-RELEASE", "PR\xC3\x89VERSION", "PRUEBAS", "PRÉVIA", "ANTEPRIMA", "VORAB", "ТЕСТОВЫЙ", "プレリリース", "预发布" },
  [kUi_NoRelease] = { "NO RELEASE SELECTED", "AUCUNE VERSION CHOISIE", "SIN VERSIÓN", "SEM VERSÃO", "NESSUNA VERSIONE", "KEINE VERSION", "ВЕРСИЯ НЕ ВЫБРАНА", "バージョン未選択", "未选择版本" },
  [kUi_DownloadAndInstall] = { "DOWNLOAD AND INSTALL", "T\xC3\x89L\xC3\x89""CHARGER ET INSTALLER", "DESCARGAR E INSTALAR", "BAIXAR E INSTALAR", "SCARICA E INSTALLA", "LADEN UND INSTALLIEREN", "ЗАГРУЗИТЬ И УСТАНОВИТЬ", "ダウンロードとインストール", "下载并安装" },
  [kUi_CheckForUpdate] = { "CHECK FOR UPDATE", "CHERCHER UNE MISE \xC3\x80 JOUR", "BUSCAR ACTUALIZACIÓN", "BUSCAR ATUALIZAÇÃO", "CERCA AGGIORNAMENTI", "UPDATE SUCHEN", "ПРОВЕРИТЬ ОБНОВЛЕНИЯ", "更新を確認", "检查更新" },
  [kUi_DownloadUpdate] = { "DOWNLOAD UPDATE", "T\xC3\x89L\xC3\x89""CHARGER", "DESCARGAR", "BAIXAR", "SCARICA", "HERUNTERLADEN", "ЗАГРУЗИТЬ", "ダウンロード", "下载更新" },
  [kUi_InstallUpdate] = { "INSTALL UPDATE", "INSTALLER", "INSTALAR", "INSTALAR", "INSTALLA", "INSTALLIEREN", "УСТАНОВИТЬ", "インストール", "安装更新" },
  [kUi_PleaseWait] = { "PLEASE WAIT", "PATIENTEZ", "ESPERA", "AGUARDE", "ATTENDI", "BITTE WARTEN", "ПОДОЖДИТЕ", "お待ちください", "请稍候" },
  [kUi_UpdateInstalled] = { "UPDATE INSTALLED", "MISE \xC3\x80 JOUR INSTALL\xC3\x89""E", "ACTUALIZACIÓN INSTALADA", "ATUALIZAÇÃO INSTALADA", "AGGIORNAMENTO INSTALLATO", "UPDATE INSTALLIERT", "ОБНОВЛЕНИЕ УСТАНОВЛЕНО", "インストール完了", "更新已安装" },
  [kUi_NotesOnTop] = { "CHANGELOG ON TOP SCREEN", "NOTES SUR L\x27\xC3\x89""CRAN DU HAUT", "CAMBIOS EN PANTALLA SUPERIOR", "NOTAS NA TELA SUPERIOR", "NOVITÀ SULLO SCHERMO ALTO", "NEUERUNGEN OBEN", "ИЗМЕНЕНИЯ НА ВЕРХНЕМ ЭКРАНЕ", "更新内容は上画面に表示", "更新说明在上屏" },
  [kUi_TapForNotes] = { "TAP THE RELEASE FOR CHANGELOG", "TOUCHEZ LA VERSION POUR LES NOTES", "TOCA LA VERSIÓN PARA VER CAMBIOS", "TOQUE A VERSÃO PARA VER NOTAS", "TOCCA LA VERSIONE PER LE NOVITÀ", "VERSION FÜR NEUERUNGEN ANTIPPEN", "КОСНИТЕСЬ ВЕРСИИ ДЛЯ ОПИСАНИЯ", "バージョンにタッチで更新内容を表示", "点按版本查看更新说明" },
  [kUi_Prev] = { "PREV", "PR\xC3\x89""C.", "ANT.", "ANT.", "PREC.", "ZURÜCK", "НАЗАД", "前へ", "上一页" },
  [kUi_Next] = { "NEXT", "SUIV.", "SIG.", "PRÓX.", "SUCC.", "WEITER", "ДАЛЕЕ", "次へ", "下一页" },
  [kUi_SystemUpdates] = { "3DS UPDATES", "MISES \xC3\x80 JOUR 3DS", "ACTUALIZACIONES 3DS", "ATUALIZAÇÕES 3DS", "AGGIORNAMENTI 3DS", "3DS UPDATES", "ОБНОВЛЕНИЯ 3DS", "3DSの更新", "3DS 更新" },
  [kUi_UpdateAvailable] = { "UPDATE AVAILABLE - OPEN SETTINGS", "MISE \xC3\x80 JOUR DISPONIBLE - VOIR OPTIONS", "ACTUALIZACIÓN DISPONIBLE", "ATUALIZAÇÃO DISPONÍVEL", "AGGIORNAMENTO DISPONIBILE", "UPDATE VERFÜGBAR", "ЕСТЬ ОБНОВЛЕНИЕ", "更新があります - 設定を開いてください", "有更新 - 打开设置" },
  [kUi_PickReleaseLine1] = { "SELECT A RELEASE ON THE TOUCH SCREEN", "CHOISISSEZ UNE VERSION SUR L'\xC3\x89""CRAN TACTILE", "ELIGE UNA VERSIÓN ABAJO", "ESCOLHA UMA VERSÃO ABAIXO", "SCEGLI UNA VERSIONE IN BASSO", "UNTEN EINE VERSION WÄHLEN", "ВЫБЕРИТЕ ВЕРСИЮ ВНИЗУ", "タッチスクリーンでバージョンを選ぶと", "在触摸屏上选择版本" },
  [kUi_PickReleaseLine2] = { "TO READ ITS CHANGELOG HERE.", "POUR LIRE SES NOTES ICI.", "PARA LEER SUS CAMBIOS AQUÍ.", "PARA LER AS NOTAS AQUI.", "PER LEGGERE QUI LE NOVITÀ.", "UM DIE NEUERUNGEN ZU LESEN.", "ЧТОБЫ ПРОЧИТАТЬ ИЗМЕНЕНИЯ.", "ここに更新内容を表示します。", "以在此处查看更新说明。" },
};

// Dungeons in game order. Names are wrapped or use the small native face.
static const char *const kUiDungeonNames[14][kUiLanguageCount] = {
  { "SEWERS", "\xC3\x89GOUTS", "ALCANTARILLAS", "ESGOTOS", "FOGNE", "KANALISATION", "КАНАЛИЗАЦИЯ", "地下水路", "下水道" },
  { "HYRULE CASTLE", "CHATEAU D'HYRULE", "CASTILLO DE HYRULE", "CASTELO DE HYRULE", "CASTELLO DI HYRULE", "SCHLOSS HYRULE", "ЗАМОК ХАЙРУЛА", "ハイラル城", "海拉鲁城堡" },
  { "EASTERN PALACE", "PALAIS DE L'EST", "PALACIO DEL ESTE", "PALÁCIO DO LESTE", "PALAZZO ORIENTALE", "OSTPALAST", "ВОСТОЧНЫЙ ДВОРЕЦ", "東の神殿", "东部神殿" },
  { "DESERT PALACE", "PALAIS DU D\xC3\x89SERT", "PALACIO DEL DESIERTO", "PALÁCIO DO DESERTO", "PALAZZO DEL DESERTO", "WÜSTENPALAST", "ПУСТЫННЫЙ ДВОРЕЦ", "砂漠の神殿", "沙漠神殿" },
  { "CASTLE TOWER", "TOUR DU CHATEAU", "TORRE DEL CASTILLO", "TORRE DO CASTELO", "TORRE DEL CASTELLO", "SCHLOSSTURM", "БАШНЯ ЗАМКА", "城の塔", "城堡塔" },
  { "SWAMP PALACE", "PALAIS DES MARAIS", "PALACIO DEL PANTANO", "PALÁCIO DO PÂNTANO", "PALAZZO DELLA PALUDE", "SUMPFPALAST", "БОЛОТНЫЙ ДВОРЕЦ", "水の神殿", "沼泽神殿" },
  { "DARK PALACE", "PALAIS DES T\xC3\x89N\xC3\x88""BRES", "PALACIO OSCURO", "PALÁCIO DAS TREVAS", "PALAZZO OSCURO", "DUNKELPALAST", "ТЁМНЫЙ ДВОРЕЦ", "ヤミの神殿", "暗之神殿" },
  { "MISERY MIRE", "MARAIS SANS FIN", "PALACIO DE MISERY MIRE", "PALÁCIO DO PÂNTANO SOMBRIO", "PALAZZO DELLA PALUDE OSCURA", "ELENDSSUMPF", "БОЛОТО СТРАДАНИЙ", "沼の神殿", "苦难沼泽" },
  { "SKULL WOODS", "BOIS DU CRANE", "PALACIO DE LA CALAVERA", "PALÁCIO DA CAVEIRA", "PALAZZO DEL TESCHIO", "SCHÄDELWALD", "ЛЕС КОСТЕЙ", "森の神殿", "头骨之森" },
  { "ICE PALACE", "PALAIS DE GLACE", "PALACIO DE HIELO", "PALÁCIO DE GELO", "PALAZZO DI GHIACCIO", "EISPALAST", "ЛЕДЯНОЙ ДВОРЕЦ", "氷の神殿", "冰之神殿" },
  { "TOWER OF HERA", "TOUR D'H\xC3\x89RA", "TORRE DE HERA", "TORRE DE HERA", "TORRE DI HERA", "HERATURM", "БАШНЯ ГЕРЫ", "ヘラの塔", "赫拉之塔" },
  { "THIEVES TOWN", "VILLAGE DES VOLEURS", "PUEBLO DE LADRONES", "VILA DOS LADRÕES", "VILLAGGIO DEI LADRI", "DIEBESVERSTECK", "ГОРОД ВОРОВ", "盗賊の村", "盗贼之村" },
  { "TURTLE ROCK", "ROCHER DE LA TORTUE", "ROCA DE LA TORTUGA", "ROCHA DA TARTARUGA", "ROCCIA DELLA TARTARUGA", "SCHILDKRÖTENFELS", "ЧЕРЕПАШЬЯ СКАЛА", "カメイワ", "乌龟岩" },
  { "GANONS TOWER", "TOUR DE GANON", "TORRE DE GANON", "TORRE DE GANON", "TORRE DI GANON", "GANONS TURM", "БАШНЯ ГАНОНА", "ガノンの塔", "加侬之塔" },
};

static int g_ui_language = kUiLanguage_English;

void UI_SetLanguage(int language) {
  g_ui_language = (language > 0 && language < kUiLanguageCount) ? language
                                                                : kUiLanguage_English;
}

int UI_GetLanguage(void) {
  return g_ui_language;
}

const char *UI_Text(int id) {
  if (id < 0 || id >= kUiStringCount)
    return "";
  const char *text = kUiStrings[id][g_ui_language];
  if (!text)
    text = kUiStrings[id][kUiLanguage_English];
  return text ? text : "";
}

const char *UI_LanguageCode(int language) {
  static const char *const codes[] = {"en", "fr", "es", "pt", "it", "de", "ru", "ja", "zh"};
  return codes[language >= 0 && language < kUiLanguageCount ? language : 0];
}

const char *UI_DungeonName(int dungeon) {
  if (dungeon < 0 || dungeon >= 14)
    return UI_Text(kUi_Dungeon);
  return kUiDungeonNames[dungeon][g_ui_language];
}

int UI_LanguageFromCode(const char *code) {
  if (code) for (int i = 0; i < kUiLanguageCount; i++)
    if (strcmp(code, UI_LanguageCode(i)) == 0) return i;
  return kUiLanguage_English;
}

const char *UI_UpdateError(const char *message) {
  static const struct { const char *message; int id; } errors[] = {
    {"SD CARD WRITE FAILED", kUi_ErrorSdWrite},
    {"CANNOT WRITE TO SD CARD", kUi_ErrorSdWrite},
    {"NO WI-FI CONNECTION", kUi_ErrorWifi},
    {"TLS SERVICE FAILED", kUi_ErrorTls},
    {"INVALID RELEASE DATA", kUi_ErrorRelease},
    {"NOT ENOUGH SD SPACE", kUi_ErrorSpace},
    {"3DSX LAUNCH PATH UNKNOWN", kUi_ErrorLaunchPath},
    {"DOWNLOAD CHECK FAILED", kUi_ErrorDownload},
    {"INSTALLATION FAILED", kUi_ErrorInstall},
    {"UPDATE CANCELLED", kUi_ErrorCancelled},
    {"CONNECTION FAILED - RETRY", kUi_ErrorConnection},
    {"CANNOT START UPDATE", kUi_ErrorStart},
    {"CANNOT SAVE UPDATE CHANNEL", kUi_ErrorChannel},
  };
  if (!message) return "";
  for (unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++)
    if (strcmp(message,errors[i].message)==0) return UI_Text(errors[i].id);
  return message;
}
