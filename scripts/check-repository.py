#!/usr/bin/env python3
"""Validate repository layout and current contributor-facing local links."""
from pathlib import Path
from urllib.parse import unquote, urlsplit
import json
import hashlib
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    layout = json.loads((ROOT / 'docs/repository-layout.json').read_text())
    raw = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT
    ).decode().split('\0')
    files = sorted({x for x in raw if x and (ROOT / x).exists()})
    errors = []
    roots = {name.split('/')[0] for name in files}
    for name in sorted(roots - set(layout['root_entries'])):
        errors.append(f'Unclassified root entry: {name}; group it or update the repository map.')
    for name in files:
        path = Path(name)
        if path.name in {'AGENTS.md', 'PROTOCOL.md', 'WORKFLOW.md', 'WORKFLOW.txt', 'GITHUB_RELEASE_WORKFLOW.md'}:
            errors.append(f'Private operating instructions must remain outside the source tree: {name}')
        pinned = layout.get('reference_fixtures', {}).get(name)
        reference_fixture = pinned is not None and hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == pinned
        if path.suffix.lower() in {'.gba', '.z64', '.n64', '.v64', '.sav', '.ppu1', '.dmp'} and not reference_fixture:
            errors.append(f'Runtime/game input is not part of the maintained source tree: {name}')
    for name in layout['current_documents']:
        path = ROOT / name
        if not path.is_file():
            errors.append(f'Missing current document: {name}')
            continue
        text = path.read_text()
        links = re.findall(r'\]\(([^)]+)\)', text) + re.findall(r'(?:src|href)="([^"]+)"', text)
        for link in links:
            parsed = urlsplit(link.strip('<>'))
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            target = path.parent / unquote(parsed.path)
            if not target.exists():
                errors.append(f'{name}: missing local destination {link}')
    for name in layout['required_paths']:
        if not (ROOT / name).exists():
            errors.append(f'Missing documented entry point: {name}')
    for error in errors:
        print(error, file=sys.stderr)
    print(f'{len(files)} maintained entries; {len(roots)} classified root entries; '
          f'{len(layout["current_documents"])} current documents checked.')
    return int(bool(errors))


if __name__ == '__main__':
    sys.exit(main())
