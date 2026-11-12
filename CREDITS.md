# Credits and acknowledgements

A Link to the Past 3DS is maintained by [EstebanPdN](https://github.com/EstebanPdN)
and builds on the work of engine developers, port authors, artists, translators,
mod creators and the GitHub community. Thank you for sharing code, artwork,
testing, reports and ideas.

## Engine and platform foundations

- [snesrev/zelda3](https://github.com/snesrev/zelda3), snesrev, elzo_d and its
  contributors: the reverse-engineered Zelda3 engine and extraction tools.
- [Waterdish](https://github.com/Waterdish/zelda3-android): the Android port base.
- [samyost1](https://github.com/samyost1/zelda3-android) and
  [JeodC](https://github.com/JeodC): the dual-screen Android/Linux work, map tab
  and shared lower-screen foundation.
- The devkitPro, devkitARM, libctru, Citro3D, Citro2D, 3ds-cmake, SDL2,
  SDL2_mixer, Opus, curl, mbedTLS, Jansson and cJSON authors and contributors.
  Their copyright and license notices remain with the corresponding components.
- Nintendo and the original A Link to the Past development team: the original
  game. Game content and trademarks remain the property of their rights holders.

## Port contributions and pull requests

- [@999sian](https://github.com/999sian): Old 3DS optimization work.
- [@arth78](https://github.com/arth78): WIDE contributions and draw-height cleanup
  ([PR #30](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/30)).
- [@Archaistic](https://github.com/Archaistic): WIDE vertical expansion and
  lower-screen Triforce improvements
  ([PR #31](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/31),
  [PR #32](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/32)).
- [@Phibonacci](https://github.com/Phibonacci): 3D HOME Menu banner work and
  presentation/HUD feedback, based on the model by
  [TiraArt](https://sketchfab.com/TiraArt).

Thank you also for these submitted patches and design proposals. Their inclusion
here acknowledges the contribution; it does not mean every proposal is shipped:

| Contributor | Submitted contribution |
| --- | --- |
| [@Alcam211](https://github.com/Alcam211) | Spanish graphical-text compatibility ([PR #50](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/50)). |
| [@frilxy](https://github.com/frilxy) | MSU-1 streaming audio proposal and review work ([PR #51](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/51)). |
| [@djaysan](https://github.com/djaysan) | Stereoscopic terrain depth, ALBW-style UI and GUIDE-tab proposal ([PR #52](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/52)). |
| [@bheppe](https://github.com/bheppe) | Stereoscopic layered-parallax proposal and Old 3DS testing ([PR #53](https://github.com/EstebanPdN/zelda-alttp-3ds/pull/53)). |

## Community pack and original mod authors

Special thanks to [@Snipy44300](https://github.com/Snipy44300) for sharing the
French Canadian/community patch series, mod tools, translations and community
pack in [issue #49](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/49), and
for French-language and performance feedback in issues #7 and #46. Credit for
packaging and port adaptation belongs separately from credit for each original
hack or artwork.

The following catalog preserves the pack's own `mods/LISEZ-MOI.md` attribution
and the original readmes included in its v10.0.11 archive. It describes the
community pack, **not a list of mods bundled in the 4.0 installer**.

| Community-pack entry | Original work and credited author(s) |
| --- | --- |
| WINTER LION | The Legend of Zelda: The Winter Lion — Alfonso De La Vega. |
| SECRETS OF PAST | Secrets of the Past 1.01 — Author not identified in the supplied pack. |
| ECHOES OF PAST | Echoes of the Past 1.1.0 — Author not identified in the supplied pack. |
| REDUX | A Link to the Past Redux — ShadowOne333. |
| REDUX NEW SCRIPT | A Link to the Past Redux (Retranslation) — ShadowOne333. |
| REFINED | A Link to the Past Refined 1.1 — Furo_19. |
| ZELDA HEROINE | Zelda Starring Zelda 2 — Author not identified in the supplied pack. |
| NATIVE FIXES | Native Fixes and Translation Improvement 1.1 — Rodimus Primal. |
| MAX CONSUMABLES | Zelda 3 Max Consumables Fix — Kazuto. |
| LINK MINISH CAP | Minish Cap Link — Author not identified in the supplied pack. |
| PRINCESS ZELDA | Zelda Starring Zelda 2 — Author not identified in the supplied pack. |
| OLD MAN LINK | The Winter Lion — Alfonso De La Vega. |
| SHADOWS FALL | The Shadows Fall 2.1 — Author not identified in the supplied pack. |
| ALLHALLOWS EVE | Allhallows Eve 1.10 — Author not identified in the supplied pack. |
| STRONGHOLDS | Interconnected Strongholds 1.00 — Author not identified in the supplied pack. |
| WISDOM 1.21 | Goddess of Wisdom 1.21 — PuzzleDude, SePH, Omega, DudeMan, Francais. |
| WISDOM 3.0 | Goddess of Wisdom 3.0 — PuzzleDude, SePH, Omega, DudeMan, Francais. |
| CONKER | Conker's High Rule Tail 1.29 — SePH. |
| PARALLEL WORLDS | Parallel Worlds 1.23 — Euclid, SePH, qwertymodo. |

For entries whose authors are not identified, no identity has been inferred from
a similar mod name. The original hack readmes remain the authority for their
full teams. The pack also preserves credits to Conn/Con, qwertymodo, SCD,
PuzzleDude, Kazuto, rainponcho, ShadowOne333, PowerPanda, Kandowontu and
Translation Quest (Chicken Knife, nejimakipiyo and Dattebayo) through Redux;
Conn and wiiqwertyuiop through Refined; and Super Zed and the Zelda3 Compendium
through Native Fixes. The Winter Lion readme credits @duckmansdrawings for its
promotional artwork.

### Character skins included in version 4.0

These are the authors recorded in the source ZSPR metadata and the converted
packages' `mod.ini`/provenance records. These six skin conversions have separate
provenance from similarly named entries in the community pack.

| Skin | Original artist(s) | Source |
| --- | --- | --- |
| Conker | Charmander106/SePH | [Original sprite](https://snesrev.github.io/sprites-gfx/snes/zelda3/link/sheets/conker.1.zspr) |
| Dark Link | iBazly | [Original sprite](https://snesrev.github.io/sprites-gfx/snes/zelda3/link/sheets/darklink.1.zspr) |
| Mario (Classic) | Damon | [Original sprite](https://snesrev.github.io/sprites-gfx/snes/zelda3/link/sheets/mario-classic.2.zspr) |
| Minish Cap Link | InTheBeef | [Original sprite](https://snesrev.github.io/sprites-gfx/snes/zelda3/link/sheets/minishcaplink.3.zspr) |
| Samus (Super Metroid) | Ben G | [Original sprite](https://snesrev.github.io/sprites-gfx/snes/zelda3/link/sheets/samus-sm.1.zspr) |
| Zelda | Myriachan | [Original sprite](https://snesrev.github.io/sprites-gfx/snes/zelda3/link/sheets/zelda.1.zspr) |

Thanks to the [snesrev sprite catalog](https://snesrev.github.io/sprites-gfx/)
and the original artists. Conversion and credit preservation do not replace
the original authors' licenses or permissions.

## Translations

| Translation | Credited authors |
| --- | --- |
| Spanish Re-traducción Final 1.11 | D_Skywalk; Pablito / Sayans for the base. |
| Italian 1.1 | SadNES cITy: mickey, \_Ombra\_ and Duke. |
| Portuguese 1.3 | GreenGoblin / Hexagon. |
| Russian 1.01 | Chief-Net: GD, Guyver, Tigran, alex231 and Mefistotel. |
| Official French, French Canadian and German releases | Nintendo and the original localization teams. |
| Japanese game text and font | Official Super Famicom release, Nintendo. |
| Japanese menu font | PixelMplus 10 and 12, Itou Hiroki; M+ bitmap fonts, Copyright (C) 2002-2013 M+ FONTS PROJECT, free to use, copy and distribute with or without modification, provided "as is" without warranty. |
| Simplified Chinese game text and font | LinkFish (@LinkFish1) — `tables/dialogue_cn.txt` and the 16x16 font of the translation; port, pack tooling and UI strings by frilxy. |

See [language provenance](platform/3ds/romfs/languages/README.md) for the
specific patch versions and the local extraction workflow. Thank you also to
the GitHub contributors who requested, tested and discussed these languages.

## Randomizer, quality-of-life references and artwork

- [Slashmolder/zelda3randomizer](https://github.com/Slashmolder/zelda3randomizer):
  the native randomizer foundation.
- [ALTTPR](https://github.com/sporchia/alttp_vt_randomizer), sporchia and its
  contributors; [aerinon/ALttPDoorRandomizer](https://github.com/aerinon/ALttPDoorRandomizer)
  and contributors: logic, location and rule provenance retained by the upstream
  randomizer.
- [KatDevsGames/z3randomizer](https://github.com/KatDevsGames/z3randomizer) and
  Equilateral IT: custom randomizer item and medallion artwork.
- [Zarby89/Enemizer](https://github.com/Zarby89/Enemizer): enemy-shuffle constraint
  provenance. Detailed upstream notices and licenses are preserved in
  [the randomizer NOTICE](engine/src/rando/upstream/NOTICE).
- [ShadowOne333 and the Redux contributors](https://github.com/ShadowOne333/A-Link-to-the-Past-Redux),
  including PowerPanda for the Kakariko shortcut used as a reference for the
  port's optional passage. Community-pack hack authors are credited separately
  above; this is not a claim that the entire Redux hack was incorporated.
- Ian Moody and Sonarpulse: the public-domain
  [Triforce vector geometry](https://commons.wikimedia.org/wiki/File:Triforce.svg)
  used as a reference for the port's native triangle raster.

## GitHub reports, testing and suggestions

Thank you to everyone below for opening issues. These acknowledgements include
bug reports, feature requests, questions and presentation feedback; they do not
imply that every requested feature has been implemented.

| Contributor | Reports and discussions |
| --- | --- |
| [@946923759](https://github.com/946923759) | [#29](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/29) |
| [@allende](https://github.com/allende) | [#12](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/12), [#22](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/22) |
| [@AnonymousUser98](https://github.com/AnonymousUser98) | [#41](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/41), [#42](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/42), [#43](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/43), [#44](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/44), [#45](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/45) |
| [@blabberflabber122](https://github.com/blabberflabber122) | [#39](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/39) |
| [@blastheaven2](https://github.com/blastheaven2) | [#40](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/40) |
| [@Catflap62](https://github.com/Catflap62) | [#11](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/11), [#38](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/38) |
| [@chronoss09](https://github.com/chronoss09) | [#5](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/5) |
| [@codemonkey85](https://github.com/codemonkey85) | [#27](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/27) |
| [@coolguy000](https://github.com/coolguy000) | [#15](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/15) |
| [@darkritual88-source](https://github.com/darkritual88-source) | [#25](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/25) |
| [@Darth-Koopa](https://github.com/Darth-Koopa) | [#23](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/23) |
| [@DewaveX](https://github.com/DewaveX) | [#10](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/10) |
| [@fjpeinado](https://github.com/fjpeinado) | [#24](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/24) |
| [@JohnnyRa1nbow](https://github.com/JohnnyRa1nbow) | [#33](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/33) |
| [@jorgegutisego95](https://github.com/jorgegutisego95) | [#36](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/36) |
| [@Koohiisan](https://github.com/Koohiisan) | [#4](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/4) |
| [@lavenderdotpet](https://github.com/lavenderdotpet) | [#26](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/26) |
| [@lincruste](https://github.com/lincruste) | [#34](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/34), [#35](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/35) |
| [@LinkFish1](https://github.com/LinkFish1) | [#37](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/37) |
| [@Phibonacci](https://github.com/Phibonacci) | [#6](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/6), [#20](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/20) |
| [@pinballgithub](https://github.com/pinballgithub) | [#28](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/28) |
| [@PititBambi](https://github.com/PititBambi) | [#21](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/21) |
| [@Snipy44300](https://github.com/Snipy44300) | [#7](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/7), [#46](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/46), [#49](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/49) |
| [@spamzilla](https://github.com/spamzilla) | [#18](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/18), [#19](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/19) |
| [@Steinyamite](https://github.com/Steinyamite) | [#2](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/2), [#3](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/3) |
| [@SweetJohnnyCage1](https://github.com/SweetJohnnyCage1) | [#13](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/13) |
| [@therealsamsquanch](https://github.com/therealsamsquanch) | [#16](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/16), [#17](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/17), [#47](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/47) |
| [@TimTH98](https://github.com/TimTH98) | [#8](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/8) |
| [@tram98](https://github.com/tram98) | [#48](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/48) |
| [@utopiaray2](https://github.com/utopiaray2) | [#14](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/14) |
| [@weirdvideogamer](https://github.com/weirdvideogamer) | [#9](https://github.com/EstebanPdN/zelda-alttp-3ds/issues/9) |

Additional testing, clarifications and suggestions in issue comments: [@Akira808State](https://github.com/Akira808State), [@alex-freitas](https://github.com/alex-freitas), [@apex06](https://github.com/apex06), [@bry003](https://github.com/bry003), [@CCCP1982](https://github.com/CCCP1982), [@Griizly](https://github.com/Griizly), [@JohanAzaryuki11](https://github.com/JohanAzaryuki11), [@kotbayun](https://github.com/kotbayun), [@Lop14](https://github.com/Lop14), [@MarioLuigi0404](https://github.com/MarioLuigi0404), [@nckforlan-hue](https://github.com/nckforlan-hue), [@OrdinaryCloverOC](https://github.com/OrdinaryCloverOC), [@P71](https://github.com/P71).

Thank you as well to the upstream contributors and people whose feedback reaches
this project outside GitHub. Original authorship, license notices and credits
inside each component or mod remain authoritative.

## Chinese UI font notices

The baked Chinese menu glyph subset uses [Fusion Pixel](https://github.com/TakWolf/fusion-pixel-font) by TakWolf and contributors at 10/12 pixels and [GNU Unifont 18.0.01](https://unifoundry.com/unifont/index.html) by its credited authors at 16 pixels. These glyph subsets retain the SIL Open Font License 1.1. Copyright and license notices are included in `platform/3ds/romfs/licenses/` and in the packaged RomFS. The in-game translation font is credited separately to LinkFish above.
