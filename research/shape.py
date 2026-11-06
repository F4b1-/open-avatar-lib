"""Shape override blocks (id 4): the hiding part, and the link from stock items to their hiding templates.

Block 4, little-endian (SPECS.md, section 3.7):
  u32 n, u32 the target model's index bytes (its header word 4), the target's 16-byte stock id (u32 mask, u16 record,
  u16 gender, the stock tail), u32 0, u32 w, then n triangle numbers of w bits each, LSB first: the target's
  triangles to hide. Then the vertex overrides (overrides(): a byte offset into the target's unpacked vertices and a
  whole vertex per entry).
A stock item's pack record holds its hiding template's stock id at head bytes 0x9C-0xAB.

Usage: python3 -I shape.py AvatarAssetPack.toc
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import expand  # noqa: E402
from strb import blocks  # noqa: E402
from toc import cstr, records  # noqa: E402

TAIL = bytes.fromhex('c1c8f109a19cb2e0')


def stock_id(b):
    """(mask, record, gender) of a 16-byte stock id stored little-endian (block 4) or None."""
    return struct.unpack_from('<IHH', b, 0) if b[8:16] == TAIL else None


def hidden(block):
    """(target id bytes, target index bytes, triangle list, the byte after the list)."""
    n, index_bytes = struct.unpack_from('<II', block, 0)
    target = block[8:24]
    zero, width = struct.unpack_from('<II', block, 24)
    assert zero == 0
    bits = int.from_bytes(block[32:32 + (n * width + 7) // 8], 'little')
    tris = [(bits >> (width * k)) & ((1 << width) - 1) for k in range(n)]
    return target, index_bytes, tris, 32 + (n * width + 7) // 8


def overrides(block, at, stride):
    """The vertex overrides after the hiding list (at: the byte after it). A dict with n, step, min, the position
    widths, the four elements' (base, width), the entries [(target vertex, offset remainder, quantised x y z, raw
    normal, weights, bone indices, colour)] and the byte after the last entry. An entry's first 16 bits are a byte
    offset into the target's unpacked vertex buffer; stride is the target's unpacked vertex size."""
    n, _ = struct.unpack_from('<II', block, at)
    zero, offset_width = struct.unpack_from('<II', block, at + 24)
    assert zero == 0
    step, mx, my, mz = struct.unpack_from('<4f', block, at + 32)
    s = (at + 48) * 8
    bits = int.from_bytes(block, 'little')

    def val(q, k):
        return (bits >> q) & ((1 << k) - 1)
    widths = [val(s + 6 * k, 6) for k in range(3)]
    elements = [(val(s + 18 + 64 * e, 32), val(s + 18 + 64 * e + 32, 32)) for e in range(4)]
    record = offset_width + sum(widths) + sum(w for _, w in elements)
    entries = []
    for i in range(n):
        q = s + 274 + record * i
        offset = val(q, offset_width)
        q += offset_width
        xyz = []
        for a in range(3):
            xyz.append(val(q, widths[a]))
            q += widths[a]
        raw = []
        for base, w in elements:
            raw.append((base + val(q, w)) & 0xFFFFFFFF)
            q += w
        entries.append((offset // stride, offset % stride, xyz, raw))
    end = (s + 274 + record * n + 7) // 8
    return dict(n=n, step=step, min=(mx, my, mz), widths=widths, elements=elements, entries=entries, end=end)


def template_link(head):
    """The hiding template's record from a pack record's head, or None."""
    g = head[0x9C:0xAC]
    if g[8:] != TAIL:
        return None
    mask, record, gender = struct.unpack('>IHH', g[:8])
    return record if mask == 0x01000000 else ('mask', hex(mask), record)


def main():
    d = open(sys.argv[1], 'rb').read()
    recs = {r['i']: r for r in records(d)}
    name = {i: cstr(d, r['names'][0]) for i, r in recs.items()}
    # Triangle counts of the possible targets (MaleBody, FemaleBody, Head), from their index bytes.
    targets = {}
    for i in (0, 1, 2):
        blob = d[recs[i]['blob']:recs[i]['blob'] + recs[i]['size']]
        for bid, at, ln, _ in blocks(blob):
            if bid == 3:
                m = expand(blob[at:at + ln])
                targets[i] = struct.unpack_from('<I', m, 16)[0]
    bad = 0
    kinds = collections.Counter()
    for i, r in recs.items():
        if not r['blob']:
            continue
        blob = d[r['blob']:r['blob'] + r['size']]
        for bid, at, ln, _ in blocks(blob):
            if bid != 4:
                continue
            target, index_bytes, tris, end = hidden(blob[at:at + ln])
            t = stock_id(target)
            ok = (t is not None and t[1] in targets and index_bytes == targets[t[1]]
                  and all(a < b for a, b in zip(tris, tris[1:])) and (not tris or tris[-1] < index_bytes // 6))
            kinds[(name[t[1]] if t else '?', 'hides' if tris else 'none')] += 1
            if not ok:
                bad += 1
                print('BAD', i, name[i], t, hex(index_bytes), len(tris))
    print('shape blocks by target:', dict(kinds), 'bad', bad)
    links = collections.Counter()
    for i, r in recs.items():
        link = template_link(r['head'])
        if link is None:
            continue
        if isinstance(link, int):
            tname = name.get(link) or ''
            links['to a template' if tname.endswith('Hiding Template') else 'to other: ' + tname] += 1
        else:
            links[str(link)] += 1
    print('record links at 0x9C:', dict(links))


def lines(d):
    """What tests/avatar_dump's shapes mode prints, for a diff (research/README.md)."""
    out = []
    for r in records(d):
        g = r['head'][0x9C:0xAC]
        if g[8:] == TAIL:
            mask, record, _ = struct.unpack('>IHH', g[:8])
            out.append(f"{r['i']} link {mask:#x} {record}")
        if not r['blob']:
            continue
        blob = d[r['blob']:r['blob'] + r['size']]
        for bid, at, ln, _ in blocks(blob):
            if bid == 4:
                block = blob[at:at + ln]
                target, index_bytes, tris, end = hidden(block)
                o = overrides(block, end, 1)  # stride 1: the raw byte offsets
                k = (2.0, 2.0 / 1.5 ** 0.5, 3.0 ** 0.5)
                positions = sum(o['min'][a] + xyz[a] * o['step'] * k[a] for _, _, xyz, _ in o['entries'] for a in range(3))
                out.append(f"{r['i']} shape {stock_id(target)[1]} {index_bytes:#x} {len(tris)} {sum(tris)} "
                           f"overrides {o['n']} {sum(v for v, _, _, _ in o['entries'])} {positions:.2f}")
                break
    return out


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[2] == '--lines':
        print('\n'.join(lines(open(sys.argv[1], 'rb').read())))
    else:
        main()
