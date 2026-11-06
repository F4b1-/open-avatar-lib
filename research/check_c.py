"""Compare tests/avatar_dump's output with the Python prototypes (render.py's decode, texture.py, material.py).

  python3 -I check_c.py pack <dump.txt> <AvatarAssetPack.toc>
  python3 -I check_c.py asset <dump.txt> <asset_v2.bin> ...   (in the order given to avatar_dump)
  python3 -I check_c.py bones <dump.txt> <AvatarAssetPack.toc> (avatar_dump bones: skeletons against
                                                              avatar_skeleton.py and skeleton.py, a test pose)
  python3 -I check_c.py anims <dump.txt> <AvatarAssetPack.toc> (avatar_dump motion: every stock animation against
                                                              anim.py, and an interpolated pose)
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from material import parts  # noqa: E402
from model import expand  # noqa: E402
from model_tex import meshes_end  # noqa: E402
from render import meshes  # noqa: E402
from strb import blocks  # noqa: E402
from texture import HEADER, header, model_textures  # noqa: E402
from toc import records  # noqa: E402

K = (2.0, 2.0 / math.sqrt(1.5), math.sqrt(3.0))


def fnv1a(b):
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


def tex_line(h, px):
    return (h['fmt'] & 0x3F, h['width'], h['height'], h['count'], fnv1a(px) if px else 0)


def expected(strb):
    """('model', meshes, textures, parameters), ('texture', line) or None, as avatar_dump prints them."""
    ids = [b[0] for b in blocks(strb)]
    for bid, at, ln, _ in blocks(strb):
        if bid == 3:
            m = expand(strb[at:at + ln])
            ms = meshes(m)
            out = []
            for x in ms:
                sums = [sum(x['min'][a] + v[a] * x['step'] * K[a] for v in x['q']) for a in range(3)]
                out.append((len(x['q']), len(x['tris']), sums))
            texs, pars = [], []
            if ms:
                count = struct.unpack_from('<I', m, 24)[0]
                texs = [tex_line(h, px) for _, _, h, px in list(model_textures(m, meshes_end(m), count))[:-1]]
                for technique, recs, mesh_count in parts(m):
                    pars.append(('m', technique, len(recs), mesh_count))
                    for t, pid, v in recs:
                        pars.append((pid, 'T', v[0], v[1], v[2]) if t == 1 else (pid, 'F') + tuple(v))
            colors = None
            if ms:
                for cid, cat, cln, _ in blocks(strb):
                    count = struct.unpack_from('<I', strb, cat)[0] if cid == 7 else 0
                    if cid == 7 and 0 < count <= 64 and 4 + 24 * count == cln:
                        e = strb[cat + 4:cat + 28]
                        colors = (count,) + tuple(e[8 * c + 3] << 24 | e[8 * c + 2] << 16 | e[8 * c + 1] << 8 | e[8 * c]
                                                  for c in range(3))
            return ('model', out, texs, pars, colors)
    if 2 in ids:
        for bid, at, ln, _ in blocks(strb):
            if bid == 2:
                t = expand(strb[at:at + ln])
                return ('texture', tex_line(header(t), t[HEADER:]))
    return None


def parse(path):
    items, cur = [], None
    for line in open(path):
        f = line.split()
        if line.startswith('  m '):
            cur[3].append(('m', int(f[1]), int(f[2]), int(f[3])))
            continue
        if line.startswith('  k '):
            cur[4] = (int(f[1]),) + tuple(int(x, 16) for x in f[2:5])
            continue
        if line.startswith('  t '):
            cur[2].append((int(f[1], 16), int(f[2]), int(f[3]), int(f[4]), int(f[5], 16)))
        elif line.startswith('  p '):
            cur[3].append((int(f[1]), 'T', int(f[3]), int(f[4]), int(f[5])) if f[2] == 'T'
                          else (int(f[1]), 'F') + tuple(float(x) for x in f[3:7]))
        elif line.startswith('  '):
            cur[1].append((int(f[0]), int(f[1]), [float(x) for x in f[4:7]]))
        elif ' texture ' in line and 'FAILED' not in line:
            label, rest = line.split(' texture ')
            g = rest.split()
            items.append((label, ('texture', (int(g[0], 16), int(g[1]), int(g[2]), int(g[3]), int(g[4], 16)))))
            cur = None
        else:
            cur = [line.strip(), [], [], [], None]
            items.append((cur[0], cur))
    return [(label, x if x[0] == 'texture' else ('model',) + tuple(x[1:])) for label, x in items]


def close(a, b):
    return abs(a - b) <= 1e-3 * max(1.0, abs(b))


def same(got, want):
    if want is None or got[0] != want[0]:
        return False
    if got[0] == 'texture':
        return got[1] == want[1]
    _, gm, gt, gp, gc = got
    _, wm, wt, wp, wc = want
    meshes_ok = len(gm) == len(wm) and all(
        g[0] == w[0] and g[1] == w[1] and all(close(a, b) for a, b in zip(g[2], w[2])) for g, w in zip(gm, wm))
    pars_ok = len(gp) == len(wp) and all(
        g[:2] == w[:2] and all(close(a, b) if isinstance(b, float) else a == b for a, b in zip(g[2:], w[2:]))
        if g[0] != 'm' else g == w
        for g, w in zip(gp, wp))
    return meshes_ok and gt == wt and pars_ok and gc == wc


def test_pose(count):
    """The pose tests/avatar_dump.c's test_pose builds, as (T, R, S) per bone."""
    out = []
    for b in range(count):
        T = [0.01, 0.02, -0.03] if b == 0 else [0.0, 0.0, 0.0]
        S = [1.1, 0.9, 1.0] if b == 5 else [1.0, 1.0, 1.0]
        out.append((T, [0.05 * (b % 5 - 2), 0.04 * (b % 3 - 1), 0.03 * (b % 7 - 3)], S))
    return out


def bones(dump_path, toc_path):
    """Compares avatar_dump bones with avatar_skeleton.py (bodies), skeleton.py and pose.py (props)."""
    import avatar_skeleton
    import pose
    import skeleton
    got = {}
    for line in open(dump_path):
        f = line.split()
        if 'FAILED' in line:
            print('C failed:', line.strip())
            continue
        key = (f[0], int(f[1]))
        if f[2] == 'posed':
            got.setdefault(key, {}).setdefault('posed', {})[int(f[3])] = [float(x) for x in f[4:7]]
        else:
            got.setdefault(key, {}).setdefault('rest', {})[int(f[2])] = (int(f[3]), [float(x) for x in f[4:7]])
    d = open(toc_path, 'rb').read()
    strb = {r['i']: d[r['blob']:r['blob'] + r['size']] for r in records(d) if r['blob']}
    bad = 0
    for (kind, index), v in sorted(got.items()):
        s = strb[index]
        if kind == 'body':
            meshes = [m for bid, at, ln, _ in blocks(s) if bid == 3 for m in pose.skinned_meshes(expand(s[at:at + ln]))]
            P = avatar_skeleton.positions(meshes)
            want_rest = {b: (avatar_skeleton.PARENT.get(b, -1), P[b]) for b in range(71)}
            W = avatar_skeleton.pose(P, test_pose(71))
            want_posed = {b: W[b][1] for b in range(71)}
        else:
            blk = [s[at:at + ln] for bid, at, ln, _ in blocks(s) if bid == 5][0]
            rest = skeleton.rest(blk)
            want_rest = {b: (-1 if p == 255 else p, t) for b, (p, R, t) in enumerate(rest)}
            W = pose.pose(rest, test_pose(len(rest)))
            want_posed = {b: W[b][1] for b in range(len(rest))}
        for b, (parent, pos) in v['rest'].items():
            wp, wpos = want_rest[b]
            if parent != wp or not all(abs(x - y) < 1e-4 for x, y in zip(pos, wpos)):
                bad += 1
                print('DIFFERS', kind, index, 'bone', b, 'rest', parent, pos, 'want', wp, wpos)
        for b, pos in v['posed'].items():
            if not all(abs(x - y) < 1e-4 for x, y in zip(pos, want_posed[b])):
                bad += 1
                print('DIFFERS', kind, index, 'bone', b, 'posed', pos, 'want', want_posed[b])
        if len(v['rest']) != len(want_rest):
            bad += 1
            print('DIFFERS', kind, index, 'bone count', len(v['rest']), 'want', len(want_rest))
    print(f'{len(got)} skeletons compared ({", ".join(f"{k} {i}" for k, i in sorted(got))}), {bad} differences')


def quaternion(v):
    """A rotation stored as a quaternion's logarithm, as a quaternion (x, y, z, w)."""
    h = math.sqrt(sum(x * x for x in v))
    k = math.sin(h) / h if h > 1e-8 else 1.0
    return [x * k for x in v] + [math.cos(h)]


def canonical(q):
    return [-x for x in q] if q[3] < 0 else q


def anims(dump_path, toc_path):
    """Compares avatar_dump motion with anim.py: per animation the sums over all frames and bones, and a pose
    interpolated at 0.4 of its length (the shorter way, normalised)."""
    import anim
    got = {}
    for line in open(dump_path):
        f = line.split()
        if 'FAILED' in line:
            print('C failed:', line.strip())
            continue
        if f[1] == 'anim':
            got.setdefault(int(f[0]), {})['anim'] = [float(x) for x in f[2:4]] + [float(x) for x in f[5:8]] + \
                [float(x) for x in f[9:13]] + [float(x) for x in f[14:17]]
        else:
            got.setdefault(int(f[0]), {})['sample'] = [float(x) for x in f[3:6]] + [float(x) for x in f[7:11]]
    d = open(toc_path, 'rb').read()
    bad = count = 0
    for r in records(d):
        if not r['blob']:
            continue
        s = d[r['blob']:r['blob'] + r['size']]
        found = [s[at:at + ln] for bid, at, ln, _ in blocks(s) if bid == 1]
        if not found:
            continue
        A, P = anim.parts(found[0])
        if A['bones'] != 71:
            continue
        count += 1
        V = anim.values(A, P[0])
        F = A['frames']
        t, q, sc = [0.0] * 3, [0.0] * 4, [0.0] * 3
        for fr in range(F):
            for b in range(71):
                T, R, S = V[fr][b]
                cq = canonical(quaternion(R))
                for a in range(3):
                    t[a] += T[a]
                    sc[a] += S[a]
                for a in range(4):
                    q[a] += cq[a]
        want = [F, A['fps']] + t + q + sc
        frame = 0.4 * (F - 1)
        first = min(int(frame), F - 2)
        w = frame - first
        st, sq = [0.0] * 3, [0.0] * 4
        for b in range(71):
            (T0, R0, _), (T1, R1, _) = V[first][b], V[first + 1][b]
            a0, a1 = quaternion(R0), quaternion(R1)
            sign = -1.0 if sum(x * y for x, y in zip(a0, a1)) < 0 else 1.0
            m = [(1 - w) * x + w * sign * y for x, y in zip(a0, a1)]
            n = math.sqrt(sum(x * x for x in m))
            cq = canonical([x / n for x in m])
            for a in range(3):
                st[a] += T0[a] + w * (T1[a] - T0[a])
            for a in range(4):
                sq[a] += cq[a]
        g = got.get(r['i'])
        for key, wv in (('anim', want), ('sample', st + sq)):
            gv = g.get(key) if g else None
            if not gv or not all(abs(x - y) <= 2e-3 * max(1.0, abs(y)) for x, y in zip(gv, wv)):
                bad += 1
                print('DIFFERS', r['i'], key, gv, 'want', [round(x, 4) for x in wv])
    print(f'{count} animations compared ({len(got)} from C), {bad} differences')


def main():
    if sys.argv[1] == 'bones':
        return bones(sys.argv[2], sys.argv[3])
    if sys.argv[1] == 'anims':
        return anims(sys.argv[2], sys.argv[3])
    mode, dump = sys.argv[1], parse(sys.argv[2])
    if mode == 'pack':
        d = open(sys.argv[3], 'rb').read()
        strbs = [d[r['blob']:r['blob'] + r['size']] for r in records(d) if r['blob']]
        strbs = [s for s in strbs if any(b[0] in (2, 3) for b in blocks(s))]
    else:
        strbs = [open(p, 'rb').read() for p in sys.argv[3:]]
    if len(strbs) != len(dump):
        print('count differs:', len(dump), 'from C,', len(strbs), 'expected')
    bad = models = textures = 0
    for (label, got), strb in zip(dump, strbs):
        want = expected(strb)
        if got[0] == 'model':
            models += 1
            textures += len(got[2])
        else:
            textures += 1
        if not same(got, want):
            bad += 1
            print('DIFFERS', label, got, want)
    print(f'{len(dump)} assets compared ({models} models, {textures} textures), {bad} differ')


main()
