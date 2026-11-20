#!/usr/bin/env python3
"""Build the Swedish cartridge for sv.bps from the USA one and svz1001.ips.

The Translators' "Legenden om Zelda: Gudarnas trekraft" 1.001 (Mattias Thorelli,
2004) was made with Hyrule Magic against a headered cartridge, and it grows the
cartridge past 1 MiB: its name-entry screen is a stripe at $20:8080, pointed to
by entry 5 of the screen pointer columns at $80:937A. The port reads exactly
1 MiB cartridges, so the stripe moves into the free end of bank $07 and the
pointer follows it. Usage:

  python3 prepare_sv.py USA.sfc svz1001.ips sv.sfc
  python3 make_bps.py USA.sfc sv.sfc platform/3ds/romfs/languages/sv.bps
"""
import hashlib
import sys
from pathlib import Path

USA_SHA1 = '6d4f10a8b10e10dbe624cb23cf03b88bb8252973'
IPS_SHA1 = 'd64b291eff67def6f38fcd5aca6d3f48d7ee35ba'
OUT_SHA1 = 'e4979cfca3a05a67539ab3c3885edeabdc4235dd'
HEADER = 0x200
CART = 0x100000
STRIPE = 0x100080          # $20:8080 in the patched cartridge
NEW = 0x03fa00             # $07:FA00, free (0xFF) in both cartridges
POINTERS = 0x00137a        # $80:937A: three columns of 9 (low, high, bank)
ENTRY = 5                  # the name-entry screen


def apply_ips(rom, ips):
    if ips[:5] != b'PATCH':
        raise SystemExit('not an IPS patch')
    p = 5
    while ips[p:p + 3] != b'EOF':
        off = int.from_bytes(ips[p:p + 3], 'big')
        size = int.from_bytes(ips[p + 3:p + 5], 'big')
        p += 5
        if size:
            data = ips[p:p + size]
            p += size
        else:
            data = ips[p + 2:p + 3] * int.from_bytes(ips[p:p + 2], 'big')
            p += 3
        if len(rom) < off + len(data):
            rom.extend(bytes(off + len(data) - len(rom)))
        rom[off:off + len(data)] = data


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    usa = Path(sys.argv[1]).read_bytes()
    ips = Path(sys.argv[2]).read_bytes()
    if hashlib.sha1(usa).hexdigest() != USA_SHA1:
        raise SystemExit('expected the headerless USA cartridge (SHA-1 %s)' % USA_SHA1)
    if hashlib.sha1(ips).hexdigest() != IPS_SHA1:
        raise SystemExit('expected svz1001.ips 1.001 (SHA-1 %s)' % IPS_SHA1)

    rom = bytearray(HEADER) + bytearray(usa)
    apply_ips(rom, ips)
    rom = rom[HEADER:]

    # The stripe runs to the end of what the patch wrote; tiles may be 0xFF too,
    # so its terminator is the last byte, not the first 0xFF.
    stripe = bytes(rom[STRIPE:])
    if stripe[-1] != 0xff:
        raise SystemExit('the name-entry stripe has no terminator')
    if any(b != 0xff for b in rom[NEW:NEW + len(stripe)]) or NEW + len(stripe) > 0x040000:
        raise SystemExit('the free space at $07:FA00 is not free')
    entry = [rom[POINTERS + c * 9 + ENTRY] for c in range(3)]
    if entry != [0x80, 0x80, 0x20]:
        raise SystemExit('unexpected name-screen pointer %r' % entry)
    rom[NEW:NEW + len(stripe)] = stripe
    target = 0x078000 | (NEW & 0x7fff)
    for c in range(3):
        rom[POINTERS + c * 9 + ENTRY] = (target >> (8 * c)) & 0xff
    rom = rom[:CART]

    digest = hashlib.sha1(rom).hexdigest()
    if digest != OUT_SHA1:
        raise SystemExit('result SHA-1 %s, expected %s' % (digest, OUT_SHA1))
    if Path(sys.argv[3]).resolve() in (Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()):
        raise SystemExit('the output must be a new file')
    Path(sys.argv[3]).write_bytes(rom)
    print('wrote %s (%d bytes, SHA-1 %s); name-entry stripe of %d bytes moved to $07:FA00'
          % (sys.argv[3], len(rom), digest, len(stripe)))


if __name__ == '__main__':
    main()
