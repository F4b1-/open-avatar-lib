#!/usr/bin/env python3
"""Convert an animation from a glTF binary (.glb) into the library's own animation file (FORMATS.md, section 4.8),
for xsdk_avatar_animation_load. Python 3 standard library only, as xsdk's host tools.

  python3 anim_convert.py <in.glb> <out.xaan> [--animation NAME] [--fps N] [--list]

The rig is the avatar's skeleton of 71 bones (FORMATS.md, section 4.4): a 360sona export (section 4.7), or anything
skinned the same way. Its joints are matched to the library's bones by their order when the skin has 71 joints whose
parents are the library's, else by name (360sona's humanoid names, "shape_NN", or a number at the end of the name).

Each joint's rotation becomes the library's pose rotation, the change from the rest pose in the bone's own rest
frame: B_parent * L * B^-1, where L is the joint's animated local rotation and B the bind orientations from the inverse
bind matrices (the identity in a 360sona export, so L as it is). The root's motion is kept relative to the rig's own
root pivot, which the file carries; the library moves it onto the playing avatar's skeleton when it loads. A scale on
the skeleton (360sona scales it by the avatar's height) is divided out. Only the root moves; other translations and
all scales in the animation are left out, as in the stock animations.
"""

import argparse
import json
import math
import re
import struct
import sys

BONES = 71
MAGIC = b"XAAN"
VERSION = 1

# The library's bone tree (src/avatar_skeleton.c): each bone's parent, -1 for the root.
PARENTS = [-1, 0, 0, 0, 0, 1, 2, 2, 3, 3, 1, 6, 5, 6, 5, 8, 5, 8, 5, 14, 12, 11, 16, 15, 14, 20, 20, 20, 22, 22, 22,
           25, 25, 25, 28, 28, 28, 33, 33, 33, 33, 33, 33, 33, 36, 36, 36, 36, 36, 36, 36, 37, 38, 39, 40, 43, 44, 45,
           46, 47, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60]

# 360sona's humanoid names (FORMATS.md, section 4.7), by the library's bone number.
HUMANOID = {
    "Hips": 0, "Spine": 1, "LeftUpperLeg": 2, "RightUpperLeg": 3, "Chest": 5, "LeftLowerLeg": 6, "RightLowerLeg": 8,
    "LeftFoot": 11, "LeftShoulder": 12, "Neck": 14, "RightFoot": 15, "RightShoulder": 16, "Head": 19,
    "LeftUpperArm": 20, "LeftToes": 21, "RightUpperArm": 22, "RightToes": 23, "LeftLowerArm": 25, "RightLowerArm": 28,
    "LeftHand": 33, "RightHand": 36, "PropLeft": 41, "SpecialLeft": 42, "PropRight": 48, "SpecialRight": 49,
}
# The fingers' first bones of each side, by their joint: index, middle, ring and little in a row, then the thumb (the
# proximal row has the prop and special bones between the little finger and the thumb).
for side, proximal, intermediate, distal in (("Left", 37, 51, 61), ("Right", 44, 56, 66)):
    for k, finger in enumerate(("Index", "Middle", "Ring", "Little")):
        HUMANOID[f"{side}{finger}Proximal"] = proximal + k
        HUMANOID[f"{side}{finger}Intermediate"] = intermediate + k
        HUMANOID[f"{side}{finger}Distal"] = distal + k
    HUMANOID[f"{side}ThumbProximal"] = proximal + 6
    HUMANOID[f"{side}ThumbIntermediate"] = intermediate + 4
    HUMANOID[f"{side}ThumbDistal"] = distal + 4
assert sorted(HUMANOID.values()) == sorted(set(HUMANOID.values())) and max(HUMANOID.values()) < BONES


def read_glb(path):
    data = open(path, "rb").read()
    magic, _, length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        sys.exit(f"{path}: not a glTF binary")
    gltf, binary, at = None, b"", 12
    while at < length:
        size, kind = struct.unpack_from("<I4s", data, at)
        chunk = data[at + 8:at + 8 + size]
        if kind == b"JSON":
            gltf = json.loads(chunk)
        elif kind == b"BIN\0":
            binary = chunk
        at += 8 + size
    return gltf, binary


def accessor(gltf, binary, index):
    """An accessor's elements as tuples of floats (normalised integers scaled to 0-1 as glTF says)."""
    a = gltf["accessors"][index]
    view = gltf["bufferViews"][a["bufferView"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    fmt, scale = {5126: ("f", 1.0), 5120: ("b", 127.0), 5121: ("B", 255.0), 5122: ("h", 32767.0),
                  5123: ("H", 65535.0)}[a["componentType"]]
    size = struct.calcsize(fmt)
    stride = view.get("byteStride", width * size)
    base = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    out = []
    for i in range(a["count"]):
        values = struct.unpack_from(f"<{width}{fmt}", binary, base + i * stride)
        out.append(tuple(v / scale if fmt != "f" and a.get("normalized") else float(v) for v in values))
    return out


# Quaternions as (x, y, z, w).
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qnorm(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)


def qrot(q, v):
    return qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qconj(q))[:3]


def qslerp(a, b, f):
    if sum(x * y for x, y in zip(a, b)) < 0:
        b = tuple(-c for c in b)
    return qnorm(tuple(x + (y - x) * f for x, y in zip(a, b)))


def quat_of_matrix(m):
    """A rotation's quaternion from a 3x3 matrix (rows), its columns' scale divided out first."""
    cols = [math.sqrt(sum(m[r][c] ** 2 for r in range(3))) or 1.0 for c in range(3)]
    r = [[m[i][j] / cols[j] for j in range(3)] for i in range(3)]
    t = r[0][0] + r[1][1] + r[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        return qnorm(((r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25 * s))
    i = max(range(3), key=lambda k: r[k][k])
    j, k = (i + 1) % 3, (i + 2) % 3
    s = math.sqrt(1.0 + r[i][i] - r[j][j] - r[k][k]) * 2
    q = [0.0, 0.0, 0.0, (r[k][j] - r[j][k]) / s]
    q[i] = 0.25 * s
    q[j] = (r[j][i] + r[i][j]) / s
    q[k] = (r[k][i] + r[i][k]) / s
    return qnorm(tuple(q))


def bind_pose(gltf, binary, skin):
    """Each joint's bind orientation and position in the mesh's space, from the inverse bind matrices."""
    count = len(skin["joints"])
    mats = accessor(gltf, binary, skin["inverseBindMatrices"]) if "inverseBindMatrices" in skin else \
        [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)] * count
    out = []
    for m in mats:
        r = [[m[c * 4 + row] for c in range(3)] for row in range(3)]  # column-major
        t = (m[12], m[13], m[14])
        q = quat_of_matrix(r)
        scale = math.sqrt(sum(r[row][0] ** 2 for row in range(3))) or 1.0
        # The bind matrix is the inverse: orientation conj(q), position -(R^-1 t) with the scale taken out.
        inv = qconj(q)
        p = qrot(inv, t)
        out.append((inv, tuple(-c / scale for c in p)))
    return out


def map_joints(gltf, skin):
    """Each skin joint's library bone, or None."""
    joints = skin["joints"]
    parent_of = {}
    for i, node in enumerate(gltf["nodes"]):
        for c in node.get("children", []):
            parent_of[c] = i
    position = {n: k for k, n in enumerate(joints)}
    if len(joints) == BONES and all(position.get(parent_of.get(n), -1) == PARENTS[k] for k, n in enumerate(joints)):
        return list(range(BONES)), "by order (the skin's tree is the library's)"
    mapped = []
    for n in joints:
        name = gltf["nodes"][n].get("name", "")
        bone = HUMANOID.get(name)
        if bone is None:
            number = re.search(r"(\d+)$", name)
            bone = int(number.group(1)) if number and int(number.group(1)) < BONES else None
        mapped.append(bone)
    return mapped, "by name"


def node_trs(node):
    return (tuple(node.get("translation", (0.0, 0.0, 0.0))), tuple(node.get("rotation", (0.0, 0.0, 0.0, 1.0))),
            tuple(node.get("scale", (1.0, 1.0, 1.0))))


def ancestors_transform(gltf, node_index):
    """The world transform of a node's parents (rotation, translation, uniform scale): the armature above the root."""
    parent_of = {}
    for i, node in enumerate(gltf["nodes"]):
        for c in node.get("children", []):
            parent_of[c] = i
    chain = []
    n = parent_of.get(node_index)
    while n is not None:
        chain.append(n)
        n = parent_of.get(n)
    rot, pos, scale = (0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0), 1.0
    for n in reversed(chain):
        t, r, s = node_trs(gltf["nodes"][n])
        offset = qrot(rot, tuple(c * scale for c in t))
        pos = tuple(pos[k] + offset[k] for k in range(3))
        rot = qmul(rot, r)
        scale *= sum(s) / 3.0
    return rot, pos, scale


def sampler_track(gltf, binary, animation, sampler_index):
    s = animation["samplers"][sampler_index]
    times = [t[0] for t in accessor(gltf, binary, s["input"])]
    values = accessor(gltf, binary, s["output"])
    mode = s.get("interpolation", "LINEAR")
    if mode == "CUBICSPLINE":  # in-tangent, value, out-tangent per key: the values, joined linearly
        values = values[1::3]
    return times, values, mode


def sample(track, t, rotation):
    times, values, mode = track
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    i = max(k for k in range(len(times)) if times[k] <= t)
    if mode == "STEP" or i + 1 >= len(times):
        return values[i]
    f = (t - times[i]) / (times[i + 1] - times[i])
    a, b = values[i], values[i + 1]
    return qslerp(a, b, f) if rotation else tuple(x + (y - x) * f for x, y in zip(a, b))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("glb")
    parser.add_argument("out", nargs="?")
    parser.add_argument("--animation", help="the animation's name (the first if not given)")
    parser.add_argument("--fps", type=float, help="frames a second (the keys' own rate if they are even, else 30)")
    parser.add_argument("--list", action="store_true", help="list the animations and stop")
    args = parser.parse_args()
    gltf, binary = read_glb(args.glb)
    animations = gltf.get("animations", [])
    if args.list or not args.out:
        for a in animations:
            print(a.get("name", "(no name)"), len(a["channels"]), "channels")
        return
    if not animations:
        sys.exit("no animations in the file")
    animation = next((a for a in animations if a.get("name") == args.animation), None) if args.animation else \
        animations[0]
    if animation is None:
        sys.exit(f"no animation named {args.animation}")
    skin = gltf["skins"][0]
    joints = skin["joints"]
    bones, how = map_joints(gltf, skin)
    if 0 not in bones:
        sys.exit("no joint matches the library's root (Hips, or the skin's first joint)")
    bind = bind_pose(gltf, binary, skin)
    # Each library bone's joint, and the bind orientation of its parent's joint (or the armature's, for the root).
    joint_of = {b: k for k, b in enumerate(bones) if b is not None}
    root_joint = joint_of[0]
    armature_rot, armature_pos, armature_scale = ancestors_transform(gltf, joints[root_joint])

    tracks = {}
    for c in animation["channels"]:
        node = c["target"].get("node")
        if node in joints and c["target"]["path"] in ("rotation", "translation", "scale"):
            tracks[(joints.index(node), c["target"]["path"])] = sampler_track(gltf, binary, animation, c["sampler"])
    times = sorted({t for tr in tracks.values() for t in tr[0]})
    start, end = times[0], times[-1]
    steps = [b - a for a, b in zip(times, times[1:])]
    even = steps and max(steps) - min(steps) < 1e-3
    fps = args.fps or (1.0 / steps[0] if even else 30.0)
    frames = max(2, int(round((end - start) * fps)) + 1)
    fps = (frames - 1) / (end - start) if end > start else fps

    def local(joint, t, path, default):
        track = tracks.get((joint, path))
        return sample(track, t, path == "rotation") if track else default

    rest = [node_trs(gltf["nodes"][n]) for n in joints]
    pivot = bind[root_joint][1]
    out = bytearray(MAGIC + struct.pack("<IIIf3f", VERSION, BONES, frames, fps, *pivot))
    unmapped = sum(b is None for b in bones)
    for f in range(frames):
        t = start + f / fps
        # The root: where its pivot is in the rig's world, unscaled, less the pivot at rest.
        r0 = local(root_joint, t, "rotation", rest[root_joint][1])
        p0 = local(root_joint, t, "translation", rest[root_joint][0])
        s0 = sum(local(root_joint, t, "scale", rest[root_joint][2])) / 3.0
        world = qrot(armature_rot, tuple(c * armature_scale for c in p0))
        world = tuple(armature_pos[k] + world[k] for k in range(3))
        scale = armature_scale * s0
        move = tuple(world[k] / scale - pivot[k] for k in range(3))
        out += struct.pack("<3f", *move)
        for b in range(BONES):
            j = joint_of.get(b)
            if j is None:
                out += struct.pack("<4f", 0.0, 0.0, 0.0, 1.0)
                continue
            if b == 0:
                parent_bind = armature_rot
            elif PARENTS[b] in joint_of:
                parent_bind = bind[joint_of[PARENTS[b]]][0]
            else:
                parent_bind = (0.0, 0.0, 0.0, 1.0)
            rotation = local(j, t, "rotation", rest[j][1])
            delta = qnorm(qmul(qmul(parent_bind, rotation), qconj(bind[j][0])))
            out += struct.pack("<4f", *delta)
    open(args.out, "wb").write(out)
    print(f"{animation.get('name', '')}: {frames} frames at {fps:.3f} a second, {end - start:.3f} s; joints matched "
          f"{how}, {unmapped} not matched; root pivot ({pivot[0]:+.3f} {pivot[1]:+.3f} {pivot[2]:+.3f}); "
          f"{len(out)} bytes")


if __name__ == "__main__":
    main()
