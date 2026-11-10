"""Detail for the library's own animations (milestone 11), over blender_life.py's asymmetries: what keeps a keyed
body from looking stiff. Run inside Blender (5.1) with the animation's .blend open (male or female alike). Our own
code; Blender's Python API only.

  ns = {}; exec(open("tools/blender_legs.py").read(), ns); exec(open("tools/blender_detail.py").read(), ns)
  ns["add_detail"]("walk", ns)      # or "run", "jump"; returns the feet's worst miss in mm

What it lays over the action, as a change added on every frame (so the curves come out keyed every frame):
- hands: the fingers curled as a relaxed hand's are (more from the index to the little finger, the run's nearly a
  loose fist), curling and opening a little behind the wrist; the thumb in;
- arms: each joint behind the one above (the elbow bends most just after the arm's forward swing, the wrist trails
  the forearm, the fingers the wrist); the walk's arm swing a fifth wider; the forearm turns a little;
- shoulders: forward with their arm, back with it; the upper arms 5 degrees further out (the run 14), for the curled fingers;
- torso: the spine gives forward just after each footfall and straightens after, the chest a frame or two behind
  it, the neck and head countering so the eyes stay level; the chest's turn lags the spine's and the neck takes
  some of it back; the hips shift a few millimetres fore and aft each step; the run leans 3 degrees more;
- feet: in the swing the toes hang after toe-off and lift before the landing;
- the jump: the fingers clench in the crouches and spread in the air, the back arches and the toes point in the
  air, the head looks up at takeoff and down for the landing, the shoulders lift with the arms, the elbows give
  and the hands trail on the way up and down, and after the landing the arms swing past rest and settle, the chest
  and head dip late, the back straightens a touch too far and returns. Its first and last frames keep one pose.
The rig's signs (measured at rest, Euler XYZ): a finger curls about z (left -, right +), a thumb closes about y
(left +, right -), a shoulder goes forward about y (left -, right +) and up about z (left +, right -), an elbow and
a wrist bend about y (left -, right +), toes point down about +x, the hips' location 2 is forward.
The feet are pinned as blender_life.py pins them (blender_legs.py: snapshot_feet, pin_feet); the toes' and the
jump's foot changes come after the pin and only where the foot is in the air.
"""
import math

import bpy

R = math.radians
FINGERS = ("Index", "Middle", "Ring", "Little")
JOINTS = ("Proximal", "Intermediate", "Distal")


def _fc(bone, prop, idx):
    a = bpy.data.objects["Avatar"]; b = a.pose.bones[bone]
    if prop == "rotation_euler" and b.rotation_mode == 'QUATERNION':
        b.rotation_mode = 'XYZ'
    act = a.animation_data.action
    cb = [cb for layer in act.layers for st in layer.strips for cb in st.channelbags][0]
    path = f'pose.bones["{bone}"].{prop}'
    fc = cb.fcurves.find(path, index=idx)
    return fc if fc is not None else cb.fcurves.new(path, index=idx)


def _rekey(fc, vals):
    while len(fc.keyframe_points):
        fc.keyframe_points.remove(fc.keyframe_points[0], fast=True)
    fc.keyframe_points.add(len(vals))
    for k, (f, v) in zip(fc.keyframe_points, vals):
        k.co = (f, v); k.interpolation = 'LINEAR'
    fc.update()


def layer(bone, prop, idx, fn):
    """Add fn(frame) (degrees, or cm for a location) to a channel on every frame of the scene."""
    s = bpy.context.scene; fc = _fc(bone, prop, idx)
    u = R if prop.startswith("rotation") else (lambda v: v / 100.0)
    _rekey(fc, [(f, fc.evaluate(f) + u(fn(f))) for f in range(s.frame_start, s.frame_end + 1)])


def amplify(bone, prop, idx, factor):
    """Scale a looping channel about its mean over the loop."""
    s = bpy.context.scene; fc = _fc(bone, prop, idx)
    frames = list(range(s.frame_start, s.frame_end + 1))
    v = [fc.evaluate(f) for f in frames]; mean = sum(v[:-1]) / (len(v) - 1)
    _rekey(fc, [(f, mean + (x - mean) * factor) for f, x in zip(frames, v)])


def wave(period, amp, peak, per_cycle=1, bias=0.0):
    return lambda f: bias + amp * math.cos(2 * math.pi * per_cycle * (f - peak) / period)


def keyed(d, period=None, shift=0):
    """Smooth steps through {frame: value}; with a period, the frame wrapped into it after taking shift off."""
    ks = sorted(d)

    def fn(f):
        if period:
            f = (f - shift) % period
        if f <= ks[0]:
            return d[ks[0]]
        for a, b in zip(ks, ks[1:]):
            if a <= f <= b:
                t = (f - a) / (b - a); t = t * t * (3 - 2 * t)
                return d[a] + (d[b] - d[a]) * t
        return d[ks[-1]]
    return fn


def hand(side, base, fn):
    """A relaxed hand: each finger curled base degrees and more toward the little finger, plus fn(frame)."""
    sign = -1 if side == "Left" else 1
    for i, finger in enumerate(FINGERS):
        for j, joint in enumerate(JOINTS):
            k = (0.8, 1.0, 1.15, 1.3)[i] * (1.0, 1.2, 0.7)[j]
            layer(f"{side}{finger}{joint}", "rotation_euler", 2, lambda f, k=k: sign * k * (base + fn(f)))
    layer(f"{side}ThumbProximal", "rotation_euler", 1, lambda f: -sign * 0.5 * (base + fn(f)))
    layer(f"{side}ThumbIntermediate", "rotation_euler", 1, lambda f: -sign * 0.3 * (base + fn(f)))


def arms_out(degrees):
    """The upper arms a little further from the body, so the curled fingers clear the hips and clothes."""
    layer("LeftUpperArm", "rotation_euler", 2, lambda f: degrees)
    layer("RightUpperArm", "rotation_euler", 2, lambda f: -degrees)


def _torso(P, pitch, yaw, yaw_peak, tilt_peak, shift_cm, shift_peak):
    rot = "rotation_euler"
    layer("Spine", rot, 0, wave(P, pitch, P * 0.10, 2))
    layer("Chest", rot, 0, wave(P, pitch * 0.85, P * 0.15, 2))
    layer("Neck", rot, 0, wave(P, -pitch * 0.65, P * 0.17, 2))
    layer("Head", rot, 0, wave(P, -pitch * 0.85, P * 0.21, 2))
    layer("Chest", rot, 1, wave(P, yaw, yaw_peak))
    layer("Neck", rot, 1, wave(P, -yaw * 0.75, yaw_peak - 1))
    layer("Neck", rot, 2, wave(P, 0.75, tilt_peak))
    layer("Hips", "location", 2, wave(P, shift_cm, shift_peak, 2))


def _walk():
    P, rot = 30, "rotation_euler"
    amplify("LeftUpperArm", rot, 0, 1.2); amplify("RightUpperArm", rot, 0, 1.2)
    layer("LeftLowerArm", rot, 1, wave(P, -6, 18)); layer("RightLowerArm", rot, 1, wave(P, 5, 4))
    layer("LeftLowerArm", rot, 0, wave(P, 4, 15)); layer("RightLowerArm", rot, 0, wave(P, -4, 1))
    layer("LeftHand", rot, 1, wave(P, 9, 9)); layer("RightHand", rot, 1, wave(P, -8, 24))
    hand("Left", 26, wave(P, 5, 27)); hand("Right", 28, wave(P, 4, 12))
    layer("LeftShoulder", rot, 1, wave(P, -3, 14)); layer("RightShoulder", rot, 1, wave(P, 3, 0))
    _torso(P, 1.2, 2.5, 2, 8, 0.4, 12)
    arms_out(5)


def _walk_toes():
    d = {0: 0, 19: 0, 22: 8, 25: 0, 27: -10, 29: -4, 30: 0}
    layer("LeftToes", "rotation_euler", 0, keyed(d, 30)); layer("RightToes", "rotation_euler", 0, keyed(d, 30, 15))


def _run():
    P, rot = 20, "rotation_euler"
    layer("LeftLowerArm", rot, 1, wave(P, -8, 12)); layer("RightLowerArm", rot, 1, wave(P, 7, 3))
    hand("Left", 48, wave(P, 4, 3)); hand("Right", 52, wave(P, 4, 13))
    layer("LeftShoulder", rot, 1, wave(P, -5, 10)); layer("RightShoulder", rot, 1, wave(P, 5, 1))
    layer("Hips", rot, 0, lambda f: 2.0); layer("Spine", rot, 0, lambda f: 1.0)
    _torso(P, 2.2, 4.0, 1.5, 4, 0.7, 7)
    arms_out(14)  # the run's fingertips pass the thighs with the arm back


def _run_toes():
    d = {0: 0, 9: 0, 12: 10, 15: 0, 17: -8, 19: -3, 20: 0}
    layer("LeftToes", "rotation_euler", 0, keyed(d, 20)); layer("RightToes", "rotation_euler", 0, keyed(d, 20, 10))


def _late(fn, end=40):
    """fn a frame late, but on time at the last frame, so the jump ends in the pose it began in."""
    return lambda f: fn(f if f >= end else f - 1)


def _jump():
    rot = "rotation_euler"
    curl = keyed({0: 0, 10: 14, 14: 0, 19: -14, 23: -12, 27: 0, 31: 14, 36: 3, 40: 0})
    hand("Left", 26, curl); hand("Right", 26, lambda f: 0.9 * _late(curl)(f))
    layer("Spine", rot, 0, keyed({0: 0, 15: 0, 18: -3, 22: -3, 26: 0, 34: 0, 37: -2, 40: 0}))
    layer("Chest", rot, 0, keyed({0: 0, 15: 0, 18: -3, 22: -2, 27: 0, 32: 3, 37: 0, 40: 0}))
    layer("Head", rot, 0, keyed({0: 0, 10: 0, 14: -4, 20: 0, 26: 5, 33: 4, 37: 1, 40: 0}))
    swing = keyed({0: 0, 31: 0, 34: 8, 37: 3, 40: 0})
    layer("LeftUpperArm", rot, 0, swing); layer("RightUpperArm", rot, 0, lambda f: 0.8 * _late(swing)(f))
    elbow = keyed({0: 0, 10: 0, 13: -12, 16: 0, 22: 0, 25: -10, 27: -4, 30: -12, 35: -3, 40: 0})
    layer("LeftLowerArm", rot, 1, elbow); layer("RightLowerArm", rot, 1, lambda f: -0.85 * _late(elbow)(f))
    wrist = keyed({0: 0, 10: 0, 13: 15, 17: -10, 20: 0, 25: -12, 28: 0, 30: 10, 35: 0, 40: 0})
    layer("LeftHand", rot, 1, wrist); layer("RightHand", rot, 1, lambda f: -0.9 * _late(wrist)(f))
    lift = keyed({0: 0, 12: 0, 15: 4, 21: 4, 27: 0, 31: -2, 36: 0, 40: 0})
    layer("LeftShoulder", rot, 2, lift); layer("RightShoulder", rot, 2, lambda f: -lift(f))
    arms_out(5)


def _jump_toes():
    d = keyed({0: 0, 15: 0, 18: 12, 22: 12, 25: -5, 27: 0, 40: 0})
    layer("LeftToes", "rotation_euler", 0, d); layer("RightToes", "rotation_euler", 0, d)


def add_detail(which, ns=None):
    """Snapshot the feet, lay the detail over the body, pin the feet back, then the toes. Returns the worst miss."""
    g = ns or globals()
    snap = g["snapshot_feet"]()
    {"walk": _walk, "run": _run, "jump": _jump}[which]()
    miss = g["pin_feet"](snap)
    {"walk": _walk_toes, "run": _run_toes, "jump": _jump_toes}[which]()
    return miss
