#!/usr/bin/env python3
"""Turn a plain-text GameFAQs walkthrough into guide.txt for the GUIDE tab.

    python3 tools/secondscreen/make_guide.py FAQ.txt guide.txt --author "Name" \
        [--title "Game"] [--no-toc Overworld]

Copy guide.txt to sdmc:/3ds/Zelda 3DS/ (desktop: next to zelda3.ini). Guides are
their authors' work; keep them on your own card, do not commit them.

Output format, one line per screen line, already wrapped to WIDTH columns:
    ZGUIDE 1
    @T title
    @A author
    \x01Chapter heading      (contents entry, level 0)
    \x02Section heading      (contents entry, level 1)
    \x03Section heading      (shown, but not listed in the contents)
    text...
Chapters are "-=  N. Title -=" banners; sections are a short line framed by
two rules of '=' (the layout most GameFAQs walkthroughs use).
"""
import argparse, re, textwrap

WIDTH = 44   # 264 px of the guide panel at 6 px per character, beside the scrollbar

ASCII = {'©': '(c)', '®': '(r)', '’': "'", '‘': "'", '“': '"', '”': '"',
         '–': '-', '—': '-', 'é': 'e', 'è': 'e', 'à': 'a', 'ü': 'u', 'ö': 'o'}

def to_ascii(s):
    s = ''.join(ASCII.get(ch, ch) for ch in s)
    return ''.join(ch if 32 <= ord(ch) < 127 else '?' for ch in s.replace('\t', '    '))

def is_rule(s):
    t = s.strip()
    return len(t) >= 4 and set(t) <= set('-=*~_+#')

def structured(line):
    """Lines that keep their own line break: lists, tables, indented blocks."""
    t = line.strip()
    return (line.startswith('  ') or re.match(r'^([-*+o>]|\d+[.)]|[A-Za-z][.)])\s', t) is not None
            or '   ' in t or t.endswith(':'))

def convert(src, title, author, no_toc):
    lines = [to_ascii(l.rstrip()) for l in src.splitlines()]
    out = ['ZGUIDE 1', '@T ' + title, '@A ' + author]
    para = []

    def flush():
        if para:
            out.extend(textwrap.wrap(' '.join(para), WIDTH) or [''])
            para.clear()

    def blank():
        flush()
        if out[-1] != '' and not out[-1].startswith('@'): out.append('')

    i = 0
    while i < len(lines):
        l = lines[i]; t = l.strip()
        m = re.match(r'^-=\s+(.*?)\s*-=$', t)
        if m:                                             # chapter banner
            blank(); out.append('\x01' + m.group(1)); out.append(''); i += 1; continue
        if (t and not is_rule(t) and len(t) < 50 and i > 0 and i + 1 < len(lines)
                and re.fullmatch(r'=+', lines[i - 1].strip() or 'x') and re.fullmatch(r'=+', lines[i + 1].strip() or 'x')):
            blank(); out.append(('\x03' if t in no_toc else '\x02') + t); out.append(''); i += 2; continue
        if not t or is_rule(t):
            blank(); i += 1; continue
        if structured(l):
            flush()
            indent = min(len(l) - len(l.lstrip()), 4)
            out.extend(textwrap.wrap(t, WIDTH, initial_indent=' ' * indent,
                                     subsequent_indent=' ' * (indent + 2)) or [''])
        else:
            para.append(t)
        i += 1
    flush()
    while out and out[-1] == '': out.pop()
    return '\n'.join(out) + '\n'

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('faq'); ap.add_argument('out')
    ap.add_argument('--title', default='The Legend of Zelda: A Link to the Past')
    ap.add_argument('--author', required=True)
    ap.add_argument('--no-toc', nargs='*', default=[])
    a = ap.parse_args()
    text = open(a.faq, 'rb').read().decode('latin-1')
    res = convert(text, a.title, a.author, set(a.no_toc))
    open(a.out, 'w', newline='\n').write(res)
    n = res.count('\n'); c = res.count('\x01'); s2 = res.count('\x02')
    print(f'wrote {a.out}: {n} lines, {c} chapters, {s2} listed sections, {len(res)} bytes')
