"""Decode an avatar description (1000 bytes, big-endian) and name its items from AvatarAssetPack.toc.

Layout from All-Stars Avatar's avatar_desc.py (MIT; SPECS.md, section 5.1), field meanings partly inferred.
  python3 -I manifest.py <manifest.bin> <AvatarAssetPack.toc>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from toc import cstr, records  # noqa: E402

STOCK_TAIL = bytes.fromhex('c1c8f109a19cb2e0')
COLOURS = ['skin', 'hair', 'mouth', 'eyes', 'eyebrows', 'eye shadow', 'facial hair', 'feature 1', 'feature 2']


def item(g, names):
    if g == bytes(16):
        return None
    mask, index, gender = struct.unpack('>IHH', g[:8])
    if g[8:] == STOCK_TAIL:
        return f'stock #{index} {names.get(index, "?")!r} (mask {mask:#x}, gender {gender})'
    return f'other {g.hex()} (mask {mask:#x}; ends in title id {g[12:].hex().upper()})'


def main():
    m = open(sys.argv[1], 'rb').read()[:1000]
    toc = open(sys.argv[2], 'rb').read()
    names = {r['i']: cstr(toc, r['names'][0]) for r in records(toc)}
    w0, a, b = struct.unpack_from('>Iff', m, 0)
    print(f'word 0 {w0:#x}, sliders a {a} b {b}')
    print('face features:')
    for j in range(15):
        it = item(m[0x0C + 16 * j:0x1C + 16 * j], names)
        if it:
            print(f'  [{j:2d}] {it}')
    print('colours:', ', '.join(f'{n} #{m[0xFC + 4 * k:0x100 + 4 * k].hex().upper()}' for k, n in enumerate(COLOURS)))
    for title, start, count in (('items', 0x120, 15), ('items B', 0x300, 4)):
        print(title + ':')
        for j in range(count):
            e = m[start + 32 * j:start + 32 * (j + 1)]
            if e == bytes(32):
                continue
            custom = [e[20 + 4 * k:24 + 4 * k].hex().upper() for k in range(3)] if e[20:32] != bytes(12) else []
            print(f'  [{j:2d}] {item(e[:16], names)} slot {struct.unpack_from(">H", e, 16)[0]:#x}'
                  + (f' custom colours {custom}' if custom else ''))
    print('trailer', m[0x380:0x390].hex(), 'rest zero:', m[0x390:] == bytes(1000 - 0x390))


main()
