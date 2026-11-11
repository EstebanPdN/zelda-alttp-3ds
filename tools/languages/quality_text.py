#!/usr/bin/env python3
"""Export, correct and refit the dialogue of a USA-layout translated cartridge.

  quality_text.py export LANG ROM.sfc > text.txt
  quality_text.py check  LANG ROM.sfc
  quality_text.py apply  LANG INPUT.sfc EDITS.json OUTPUT.sfc

Works on private copies, never on the checkout's data. EDITS maps a message
number to its new text (see `export` for the notation). Every message of the
output is checked to fit the 168-pixel box; a line that does not fit moves its
last words to the next line, or to a new scrolled line. Messages with a choice
keep their lines, so a choice that does not fit is an error to fix by hand.
Unedited messages keep their original bytes.

Notation: {1} {2} {3} line, {S} scroll one line, {W} wait for a key, {N} the
player's name, {xx:yy} any other command byte (xx) and argument (yy).
"""
import json
import re
import sys
from pathlib import Path

TEXT_BANK1, TEXT_BANK1_END = 0xe0000, 0xe8000
TEXT_BANK2 = 0x75f40
DICT_POINTERS, DICT_BANK = 0x74703, 0x70000
FONT_WIDTHS = 0x74adf
COMMAND_ARGS = [0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0]
CMD_LINE1, CMD_LINE2, CMD_LINE3, CMD_SCROLL, CMD_WAITKEY, CMD_NAME = 0x74, 0x75, 0x76, 0x73, 0x7e, 0x6a
CHOICE_COMMANDS = {0x68, 0x69, 0x6f, 0x71, 0x72}
BOX_WIDTH = 168
TOLERATED_WIDTH = 169  # its last column is the letter spacing; the USA text has one
LIMITS = {'en': BOX_WIDTH, 'es': BOX_WIDTH, 'pt': BOX_WIDTH, 'it': BOX_WIDTH, 'ru': BOX_WIDTH}

LATIN = {**{i: chr(65 + i) for i in range(26)}, **{26 + i: chr(97 + i) for i in range(26)},
         **{0x34 + i: str(i) for i in range(10)},
         0x3e: '!', 0x3f: '?', 0x40: '-', 0x41: '.', 0x42: ',', 0x43: '…', 0x44: '▶', 0x45: '(', 0x46: ')',
         0x47: '☥', 0x48: '≈', 0x49: '§', 0x4c: '"', 0x4d: '↑', 0x4e: '↓', 0x4f: '←', 0x50: '→', 0x51: "'",
         0x59: ' ', 0x5a: '◀', 0x5b: 'Ⓐ', 0x5c: 'Ⓑ', 0x5d: 'Ⓧ', 0x5e: 'Ⓨ', 0x5f: '|', 0x60: '¡', 0x61: '❗', 0x62: '♦', 0x66: '■',
         0x4a: '⟨K⟩', 0x4b: '⟨P⟩', 0x52: '♥1', 0x53: '♥2', 0x54: '♥3', 0x55: '♥4', 0x56: '♥5', 0x57: '♥6', 0x58: '♥7'}
RUSSIAN = 'БГДЕЖИЙКЛМНПРСТУФЦёШЩЪЫЬЭЮЯавгдежзийклмнопрстуфхцчшщ'
CHARS = {
    'en': LATIN,
    'es': {**LATIN, 0x17: 'é', 0x30: 'ó', 0x40: '≈', 0x47: 'ñ', 0x48: 'ú', 0x49: 'á', 0x51: 'í', 0x57: '☥', 0x5a: '§', 0x60: '¡', 0x61: '¿', 0x62: 'Ñ'},
    'pt': {**LATIN, 0x17: 'ú', 0x24: 'ó', 0x30: 'õ', 0x45: 'ç', 0x46: 'á', 0x47: 'ê', 0x48: 'é', 0x49: 'ã', 0x51: 'í', 0x61: '❗', 0x62: 'É', 0x63: 'â', 0x64: 'ô', 0x65: 'à'},
    'it': {**LATIN, 0x51: "'", 0x60: 'é', 0x62: 'ì', 0x63: 'à', 0x64: 'è', 0x65: 'ò', 0x66: 'ù'},
    'ru': {**{i: c for i, c in enumerate(RUSSIAN)}, **{0x34 + i: str(i) for i in range(10)}, 0x37: 'З', 0x3a: 'б',
           0x3e: '!', 0x3f: '?', 0x40: 'ю', 0x41: '.', 0x42: 'я', 0x43: 'ь', 0x44: '▶', 0x45: '(', 0x46: ')',
           0x47: '☥', 0x48: '≈', 0x49: '§', 0x4c: '"', 0x4d: '↑', 0x4e: '↓', 0x4f: 'ы', 0x50: 'э', 0x51: 'ъ',
           0x59: ' ', 0x5a: '◀', 0x5b: 'А', 0x5c: 'В', 0x5d: 'Х', 0x5e: 'Ү', 0x5f: '█', 0x60: '-', 0x61: ',', 0x62: ':', 0x65: '⟨65⟩', 0x66: '⟨66⟩',
           0x4a: '⟨K⟩', 0x4b: '⟨P⟩', 0x52: '♥1', 0x53: '♥2', 0x54: '♥3', 0x55: '♥4', 0x56: '♥5', 0x57: '♥6', 0x58: '♥7'},
}
# 'ru': 0x37 and 0x3a draw З and б, so these digits are not available.


# Letters this tool adds to a language's font, in codes nothing else uses (the
# file screens and keyboards draw from the same sheet: 0x5a and 0x5f are theirs):
# (code, base letter code, mark). The strokes follow the font's own accents.
MARKS = {
    'acute': {2: 0x08, 3: 0x10}, 'grave': {2: 0x10, 3: 0x08}, 'circumflex': {2: 0x10, 3: 0x28},
    'cap_acute': {0: 0x08, 1: 0x10}, 'narrow_grave': {2: 0x80, 3: 0x40},
}
ADDED_GLYPHS = {
    'it': [(0x60, 0x1e, 'acute'), (0x62, 0x22, 'narrow_grave'), (0x63, 0x1a, 'grave'), (0x64, 0x1e, 'grave'),
           (0x65, 0x28, 'grave'), (0x66, 0x2e, 'grave')],
    'pt': [(0x62, 0x04, 'cap_acute'), (0x63, 0x1a, 'circumflex'), (0x64, 0x28, 'circumflex'), (0x65, 0x1a, 'grave')],
}
FONT = 0x70000


def font_offset(c, lower):
    return FONT + (((c & 0x70) * 2 + (c & 0xf)) + (0x10 if lower else 0)) * 16


def add_glyphs(rom, language):
    for code, base, mark in ADDED_GLYPHS.get(language, ()):
        top = bytearray(rom[font_offset(base, False):font_offset(base, False) + 16])
        if base == 0x22:  # the mark takes the place of the i's dot
            for y in range(5):
                top[y * 2] = top[y * 2 + 1] = 0
        for y, bits in MARKS[mark].items():
            top[y * 2 + 1] |= bits
        # Plane 0 holds the outline around plane 1's strokes.
        strokes = [top[y * 2 + 1] for y in range(8)]
        for y in range(8):
            around = strokes[y] << 1 | strokes[y] >> 1
            if y:
                around |= strokes[y - 1]
            if y < 7:
                around |= strokes[y + 1]
            top[y * 2] = around & ~strokes[y] & 0xff
        rom[font_offset(code, False):font_offset(code, False) + 16] = top
        rom[font_offset(code, True):font_offset(code, True) + 16] = rom[font_offset(base, True):font_offset(base, True) + 16]
        rom[FONT_WIDTHS + code] = rom[FONT_WIDTHS + base]


# Italian: the 2001 translation has no accented letters and writes e' and piu'.
ITALIAN_KEEP = {"po'", "Po'", "da'", "Da'", "di'", "Di'", "fa'", "Fa'", "sta'", "Sta'", "va'", "Va'", "mo'", "be'", "Be'"}
ITALIAN_ACUTE = ("perche'", "Perche'", "poiche'", "Poiche'", "benche'", "Benche'", "affinche'", "Affinche'",
                 "sicche'", "Sicche'", "finche'", "Finche'", "giacche'", "nonche'", "cosicche'", "ne'", "se'", "Ne'")


def italian_accents(text):
    def fix(match):
        word = match.group(0)
        if word in ITALIAN_KEEP:
            return word
        if word in ITALIAN_ACUTE:
            return word[:-2] + 'é'
        if word[-2] == 'E':
            return word
        return word[:-2] + {'a': 'à', 'e': 'è', 'i': 'ì', 'o': 'ò', 'u': 'ù'}.get(word[-2], word[-2] + "'")
    return re.sub(r"[A-Za-z]*[aeEiou]'(?![A-Za-z])", fix, text)


def offset(addr):
    return ((addr >> 16) & 0x7f) * 0x8000 + (addr & 0x7fff)


# Names in the item box: two rows of 8 tiles in the table at $0D:F1C9 and those
# that follow it (first tile per letter 0x2550). `Y`, `J`, `W` are the tiles the
# Portuguese patch redrew as Ã, Ç and Á.
ITEM_TABLES = {'item': 0x8df1c9, 'bottle': 0x8df449, 'mirror': 0x8df5a9, 'bow': 0x8df5c9}
ITEM_NAMES = {
    'it': {('item', 7): ('MEDAGLIA', 'BOMBOS'), ('item', 9): ('MEDAGLIA', 'TREMOTO'), ('bottle', 1): ('BOCCETTA', 'VUOTA'),
           ('item', 16): ('BASTONE', 'SOMARIA'), ('bow', 2): ('ARCO DI', 'ARGENTO')},
    'pt': {('item', 16): ('BASTYO', 'SOMARIA'), ('bottle', 4): ('POJYO', 'CURATUDO'),
           ('item', 13): ('REDE', 'CAPTURA'), ('bow', 2): ('ARCO DE', 'PRATA')},
}
# Lettering of the inventory's abilities at $0D:F959 (first tile per letter 0x2d50).
ABILITIES = {'es': {'COGE': 'TOMA'}}
# The file screens' words: `(` and `)` of the USA text were kept in Portuguese,
# where these tiles draw ç and á.
FILE_WORDS = {'pt': {'(PARA QUAL)': ' PARA QUAL?'},
              'it': {'Stop': 'Esci', 'giocatore tu vuoi': 'giocatore vuoi   '}}
# Epilogue captions: whole rows, old to new.
CAPTIONS = {
    'pt': {'C DE HYRULE': 'CASTELO HYRULE', 'P DO DESERTO': 'PALACIO DESERTO',
           'SAHASRALAH VAI PRA CASA': 'SAHASRAHLA VAI PRA CASA',
           'TORRE DA MONT': 'TORRE MONTANHA', 'LOJA DE NADADEIRA': 'LOJA DE NADADEIRAS',
           'LOJA MAGIC': 'LOJA MAGICA', 'CASA DOS LENHA': 'CASA LENHADORES',
           'FLAUTISTA TOCA DENOVO': 'FLAUTISTA TOCA DE NOVO', 'ARV ASSOMBRAD': 'ARVORE ASSOMBRADA',
           'FONTE DO DES': 'FONTE DESEJOS', 'GATUNO FLORESTA': 'GATUNO DA FLORESTA',
           'F PERDIDA': 'MATA PERDIDA', 'DORME DENOVO...': 'DORME DE NOVO...'},
    'it': {'IL FLAUTO SUONA ANCORA': 'OCARINA SUONA ANCORA', 'BOTTEGA MAGICA': 'NEGOZIO MAGICO',
           'BOSCO PERDUTO': 'FORESTA PERDUTA',
           'E LA SPADA SUPREMA': 'E LA SPADA MAESTRA'},
}


def set_item_names(rom, language):
    for (table, index), (top, bottom) in ITEM_NAMES.get(language, {}).items():
        at = offset(ITEM_TABLES[table]) + index * 32
        for row, text in enumerate((top, bottom)):
            assert len(text) <= 8, text
            x = (8 - len(text)) // 2
            for i in range(8):
                c = text[i - x] if x <= i < x + len(text) else ' '
                word = 0x24f5 if c == ' ' else 0x2550 + ord(c) - 65
                rom[at + (row * 8 + i) * 2:at + (row * 8 + i) * 2 + 2] = word.to_bytes(2, 'little')


def set_abilities(rom, language):
    start = offset(0x8df959)
    for old, new in ABILITIES.get(language, {}).items():
        a = b''.join((0x2d50 + ord(c) - 65).to_bytes(2, 'little') for c in old)
        b = b''.join((0x2d50 + ord(c) - 65).to_bytes(2, 'little') for c in new)
        found = rom.find(a, start, start + 160)
        if found < 0:
            raise ValueError('ability word absent: ' + old)
        rom[found:found + len(b)] = b


def set_file_words(rom, language):
    values = list(range(8)) + [0xaf] + list(range(9, 16)) + list(range(0x20, 0x2a))
    chars = dict(zip('ABCDEFGHIJKLMNOPQRSTUVWXYZ', values))
    chars.update({' ': 0xa9, '?': 0x6f, '(': 0x85, ')': 0x86})
    chars.update({chr(97 + i): ((26 + i) & 0x70) * 2 + ((26 + i) & 0xf) for i in range(26)})

    def encode(text, bottom, palette, blank):
        return bytes(b for c in text for b in (blank if c == ' ' else chars[c] + (16 if bottom else 0), palette))
    for old, new in FILE_WORDS.get(language, {}).items():
        assert len(old) == len(new)
        found = 0
        for bottom, blank in [(False, 0xa9), (True, 0xa9), (True, 0xb9)]:
            for palette in [0, 0x18, 0x1c]:
                a, b = encode(old, bottom, palette, blank), encode(new, bottom, palette, blank)
                found += rom.count(a)
                rom[:] = rom.replace(a, b)
        if not found:
            raise ValueError('file word absent: ' + old)


def set_captions(rom, language):
    mapping = CAPTIONS.get(language)
    if not mapping:
        return
    start, end = offset(0x8ebf4c), offset(0x8ec2e1)
    offsets = [int.from_bytes(rom[end + i * 2:end + i * 2 + 2], 'little') for i in range(17)]
    data, new_offsets, used = bytearray(), [], set()
    for scene in range(16):
        new_offsets.append(len(data))
        p, stop = start + offsets[scene], start + offsets[scene + 1]
        while p < stop:
            addr = rom[p] << 8 | rom[p + 1]
            n = ((rom[p + 2] << 8 | rom[p + 3]) + 1) // 2
            tiles = bytes(rom[p + 4:p + 4 + n])
            p += 4 + n
            base = next((b for b in (26, 93, 131, 56) if any(b <= c < b + 26 for c in tiles)), 26)
            dot = {26: 82, 56: 82, 93: 120, 131: 158}[base]
            # The Portuguese patch left the apostrophe of the USA caption and a
            # row at a wrong address.
            if language == 'pt' and tiles == b'\x34':
                continue
            if language == 'pt' and addr == 0x9f09:
                addr = 0x6309
            text = ''.join(chr(65 + c - base) if base <= c < base + 26 else ' ' if c == 159 else '.' if c == dot else '\0' for c in tiles)
            if text.strip() in mapping:
                used.add(text.strip())
                new = mapping[text.strip()]
                row = (addr - 0x6000) // 32
                addr = 0x6000 + row * 32 + (32 - len(new)) // 2
                tiles = bytes(159 if c == ' ' else dot if c == '.' else base + ord(c) - 65 for c in new)
            # Blank tiles at the ends draw nothing.
            lead = len(tiles) - len(tiles.lstrip(b'\x9f'))
            addr += lead
            tiles = tiles.strip(b'\x9f')
            data += bytes([addr >> 8, addr & 255, (len(tiles) * 2 - 1) >> 8, (len(tiles) * 2 - 1) & 255]) + tiles
    new_offsets.append(len(data))
    if len(data) > end - start:
        raise ValueError('captions exceed their table by %d' % (len(data) - end + start))
    missing = set(mapping) - used
    if missing:
        raise ValueError('captions absent: %s' % missing)
    rom[start:end] = data + bytes(end - start - len(data))
    rom[end:end + 34] = b''.join(v.to_bytes(2, 'little') for v in new_offsets)


def is_command(c):
    return 0x67 <= c < 0x80


def dictionary(rom):
    first = rom[DICT_POINTERS] | rom[DICT_POINTERS + 1] << 8
    words = []
    for i in range((first - 0xc703) // 2 - 1):
        start = rom[DICT_POINTERS + i * 2] | rom[DICT_POINTERS + i * 2 + 1] << 8
        end = rom[DICT_POINTERS + i * 2 + 2] | rom[DICT_POINTERS + i * 2 + 3] << 8
        words.append(bytes(rom[DICT_BANK + start - 0x8000:DICT_BANK + end - 0x8000]))
    return words


def read_messages(rom):
    messages, current, p = [], bytearray(), TEXT_BANK1
    end1 = None
    while True:
        c = rom[p]
        if c == 0xff:
            return messages, end1, p
        if c == 0x80:
            end1, p, current = p, TEXT_BANK2, bytearray()
            continue
        if c == 0x7f:
            messages.append(bytes(current))
            current, p = bytearray(), p + 1
            continue
        n = 1 + COMMAND_ARGS[c - 0x67] if is_command(c) else 1
        current += rom[p:p + n]
        p += n


def expand(raw, words):
    """A message without dictionary words: a list of ints and (command, arg) pairs."""
    out, i = [], 0
    while i < len(raw):
        c = raw[i]
        if c >= 0x88:
            out.extend(words[c - 0x88])
            i += 1
        elif is_command(c):
            n = COMMAND_ARGS[c - 0x67]
            out.append((c, raw[i + 1] if n else None))
            i += 1 + n
        else:
            out.append(c)
            i += 1
    return out


def flatten(tokens):
    out = bytearray()
    for t in tokens:
        if isinstance(t, tuple):
            out.append(t[0])
            if t[1] is not None:
                out.append(t[1])
        else:
            out.append(t)
    return bytes(out)


def compress(tokens, words):
    """Greedy use of the cartridge's own dictionary."""
    order = sorted(range(len(words)), key=lambda w: -len(words[w]))
    out, i = bytearray(), 0
    while i < len(tokens):
        t = tokens[i]
        if isinstance(t, tuple):
            out += flatten([t])
            i += 1
            continue
        for w in order:
            word = words[w]
            if word and all(i + k < len(tokens) and tokens[i + k] == word[k] for k in range(len(word))):
                out.append(0x88 + w)
                i += len(word)
                break
        else:
            out.append(t)
            i += 1
    return bytes(out)


def to_text(tokens, chars):
    out = []
    for t in tokens:
        if isinstance(t, tuple):
            c, a = t
            name = {CMD_LINE1: '1', CMD_LINE2: '2', CMD_LINE3: '3', CMD_SCROLL: 'S', CMD_WAITKEY: 'W', CMD_NAME: 'N'}.get(c)
            out.append('{%s}' % name if name else '{%02x}' % c if a is None else '{%02x:%02x}' % (c, a))
        else:
            out.append(chars.get(t, '<%02x>' % t))
    return ''.join(out)


def from_text(text, chars):
    reverse = {v: k for k, v in chars.items()}
    named = {'1': CMD_LINE1, '2': CMD_LINE2, '3': CMD_LINE3, 'S': CMD_SCROLL, 'W': CMD_WAITKEY, 'N': CMD_NAME}
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c == '{':
            end = text.index('}', i)
            body = text[i + 1:end]
            if body in named:
                out.append((named[body], None))
            elif ':' in body:
                a, b = body.split(':')
                out.append((int(a, 16), int(b, 16)))
            else:
                out.append((int(body, 16), None))
            i = end + 1
        elif c == '<':
            end = text.index('>', i)
            out.append(int(text[i + 1:end], 16))
            i = end + 1
        else:
            for size in (3, 2, 1):
                if text[i:i + size] in reverse:
                    out.append(reverse[text[i:i + size]])
                    i += size
                    break
            else:
                raise ValueError('no letter for %r in %r' % (c, text))
    return out


def width(tokens, widths):
    return sum(widths[t] for t in tokens if not isinstance(t, tuple))


def split_lines(tokens):
    """[(separator or None, line tokens)]"""
    lines, separator, line = [], None, []
    for t in tokens:
        if isinstance(t, tuple) and t[0] in (CMD_LINE1, CMD_LINE2, CMD_LINE3, CMD_SCROLL):
            lines.append((separator, line))
            separator, line = t, []
        else:
            line.append(t)
    lines.append((separator, line))
    return lines


def fit(tokens, widths, limit, space=0x59):
    """Move what does not fit to the next line, keeping the words whole."""
    if any(isinstance(t, tuple) and t[0] in CHOICE_COMMANDS for t in tokens):
        return tokens
    lines = split_lines(tokens)
    i = 0
    while i < len(lines):
        separator, line = lines[i]
        if width(line, widths) > limit:
            total, cut = 0, None
            for k, t in enumerate(line):
                if not isinstance(t, tuple):
                    if t == space and total <= limit:
                        cut = k
                    total += widths[t]
            if cut is None:
                raise ValueError('a word wider than the box')
            head, tail = line[:cut], line[cut + 1:]
            while tail and tail[0] == space:
                tail = tail[1:]
            while head and head[-1] == space:
                head = head[:-1]
            lines[i] = (separator, head)
            has_wait = any(isinstance(t, tuple) for t in tail)
            following = lines[i + 1] if i + 1 < len(lines) else None
            if following and following[0][0] in (CMD_LINE2, CMD_LINE3) and not has_wait:
                lines[i + 1] = (following[0], tail + [space] + following[1])
            else:
                lines.insert(i + 1, ((CMD_SCROLL, None), tail))
        i += 1
    out = []
    for separator, line in lines:
        if separator:
            out.append(separator)
        out.extend(line)
    return out


def write_messages(rom, messages, end2_limit):
    bank1 = bytearray()
    index = 0
    for index, m in enumerate(messages):
        if TEXT_BANK1 + len(bank1) + len(m) + 2 > TEXT_BANK1_END:
            break
        bank1 += m + b'\x7f'
    else:
        index = len(messages)
    bank2 = bytearray()
    for m in messages[index:]:
        bank2 += m + b'\x7f'
    if TEXT_BANK2 + len(bank2) + 1 > end2_limit:
        raise ValueError('the text is %d bytes too long' % (TEXT_BANK2 + len(bank2) + 1 - end2_limit))
    rom[TEXT_BANK1:TEXT_BANK1_END] = bank1 + b'\x80' + b'\xff' * (TEXT_BANK1_END - TEXT_BANK1 - len(bank1) - 1)
    rom[TEXT_BANK2:end2_limit] = bank2 + b'\xff' + b'\xff' * (end2_limit - TEXT_BANK2 - len(bank2) - 1)


def free_end(rom, end2):
    p = end2 + 1
    while p < 0x78000 and rom[p] == 0xff:
        p += 1
    return p


def main():
    action, language, source = sys.argv[1:4]
    chars = CHARS[language]
    rom = bytearray(Path(source).read_bytes())
    words = dictionary(rom)
    if action == 'apply':
        add_glyphs(rom, language)
        set_item_names(rom, language)
        set_abilities(rom, language)
        set_file_words(rom, language)
        set_captions(rom, language)
    widths = rom[FONT_WIDTHS:FONT_WIDTHS + 0x67]
    messages, _, end2 = read_messages(rom)
    limit = LIMITS.get(language, BOX_WIDTH)
    if action == 'export':
        for n, raw in enumerate(messages):
            print('%03d: %s' % (n, to_text(expand(raw, words), chars)))
    elif action == 'check':
        bad = 0
        for n, raw in enumerate(messages):
            tokens = expand(raw, words)
            assert flatten(tokens) == flatten(from_text(to_text(tokens, chars), chars)), n
            over = [width(l, widths) for _, l in split_lines(tokens) if width(l, widths) > TOLERATED_WIDTH]
            if over:
                bad += 1
                print(n, over, to_text(tokens, chars))
        print(language, len(messages), 'messages,', bad, 'with lines over the box')
        raise SystemExit(1 if bad else 0)
    elif action == 'apply':
        edits = json.loads(Path(sys.argv[4]).read_text(encoding='utf-8'))
        output = Path(sys.argv[5])
        for n, raw in enumerate(messages):
            original = tokens = expand(raw, words)
            if str(n) in edits:
                tokens = from_text(edits[str(n)], chars)
            elif language == 'it':
                tokens = from_text(italian_accents(to_text(tokens, chars)), chars)
            fitted = fit(tokens, widths, TOLERATED_WIDTH) if width_over(tokens, widths) else tokens
            if fitted != original:
                messages[n] = compress(fitted, words)
        write_messages(rom, messages, free_end(rom, end2))
        # Read back everything written.
        again, _, _ = read_messages(rom)
        assert len(again) == len(messages)
        for a, b in zip(again, messages):
            assert expand(a, words) == expand(b, words)
        output.write_bytes(rom)
        print(language, 'written', output)
    else:
        raise SystemExit(__doc__)


def width_over(tokens, widths):
    return any(width(l, widths) > TOLERATED_WIDTH for _, l in split_lines(tokens))


if __name__ == '__main__':
    main()
