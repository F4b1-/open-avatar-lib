"""Skeleton blocks (STRB id 5), as the props carry them (the sleep animations' sheep and teddy bear, the user's
launcher): SPECS.md, section 4.4.

Little-endian. 8 u32 header words (the last the bone count), then a bit stream (LSB first) from byte 0x20:
  a position group: f32 step (the mesh step), f32 min x, y, z, three 6-bit widths;
  a rotation group, the same;
  per bone: u8 parent (255 for the root), the quantised position, the quantised rotation.

Both groups decode as min + q * step * K, with the meshes' per-axis K (SPECS.md 3.3). Positions are the bone's rest
position in the mesh's space. Rotations are the bone's rest orientation in the mesh's space (not relative to the
parent), stored as a quaternion logarithm: the rotation axis times half the angle. The bone's local +x points at its
child, local z is the hinge axis of the limbs.

Usage: python3 -I skeleton.py <strb file> ...
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from strb import blocks  # noqa: E402

K = (2.0, 2.0 / math.sqrt(1.5), math.sqrt(3.0))


def f32(v):
    return struct.unpack('<f', struct.pack('<I', v))[0]


def parse(b, k=(1.0, 1.0, 1.0)):
    """[(parent, position, rotation)], each value min + q * step * k (k per axis; pass K for the true values)."""
    n = struct.unpack_from('<I', b, 28)[0]
    bits = int.from_bytes(b, 'little')

    def val(q, w):
        return (bits >> q) & ((1 << w) - 1)
    q = 32 * 8
    groups = []
    for _ in range(2):
        step = f32(val(q, 32))
        mn = [f32(val(q + 32 + 32 * a, 32)) for a in range(3)]
        w = [val(q + 128 + 6 * a, 6) for a in range(3)]
        groups.append((step, mn, w))
        q += 146
    bones = []
    for _ in range(n):
        parent = val(q, 8)
        q += 8
        out = []
        for step, mn, w in groups:
            v = []
            for a in range(3):
                v.append(mn[a] + val(q, w[a]) * step * k[a])
                q += w[a]
            out.append(v)
        bones.append((parent, out[0], out[1]))
    return bones, groups, (q + 7) // 8


def rotvec(v):
    """3x3 matrix (column vectors) of a rotation vector: axis times angle."""
    th = math.sqrt(sum(x * x for x in v))
    if th < 1e-12:
        return [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    x, y, z = (a / th for a in v)
    c, s = math.cos(th), math.sin(th)
    C = 1 - c
    return [[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
            [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
            [z * x * C - y * s, z * y * C + x * s, c + z * z * C]]


def rest(b):
    """[(parent, rotation matrix, position)]: each bone's rest frame in the mesh's space."""
    bones, _, _ = parse(b, K)
    return [(p, rotvec([2 * x for x in r]), pos) for p, pos, r in bones]


if __name__ == '__main__':
    for f in sys.argv[1:]:
        d = open(f, 'rb').read()
        for bid, at, ln, _ in blocks(d):
            if bid == 5:
                blk = d[at:at + ln]
                bones, groups, end = parse(blk, K)
                frames = rest(blk)
                print(os.path.basename(f), len(bones), 'bones, ends at byte', end, 'of', ln)
                for i, ((p, pos, rot), (_, R, _)) in enumerate(zip(bones, frames)):
                    print('  %2d parent %3d  pos %s  qlog %s  +x %s' % (
                        i, p, ' '.join('%7.4f' % x for x in pos), ' '.join('%7.4f' % x for x in rot),
                        ' '.join('%5.2f' % R[a][0] for a in range(3))))
