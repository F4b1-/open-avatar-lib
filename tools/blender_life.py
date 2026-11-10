"""Life for the library's own animations (milestone 11): the asymmetries and small irregularities a real body has,
laid over the mirror-perfect walk, run and jump in Blender (5.1) after their legs are fitted. Our own code;
Blender's Python API only. Run inside Blender with the animation's .blend open (male or female, the same changes):

  ns = {}; exec(open("tools/blender_legs.py").read(), ns); exec(open("tools/blender_life.py").read(), ns)
  ns["add_life"]("walk")      # or "run", "jump"

What changes (angles in degrees, moves in cm; the rig's signs: + about x leans forward and swings an arm back,
+ about z drops the pelvis's right side, tilts the head and bends the chest to the right, lifts the left shoulder
and lowers the right one; + about y turns the head and the spine to the left):
- the arms: the left swings 8-10 % wider, the right 12-15 % less and a frame late; the left elbow bends more;
- the head: a slight habitual tilt and turn, a small nod a few frames after each footfall, not the same on both;
- the shoulders lift with their arm's forward swing, the right sits lower;
- the chest bends a little over the standing leg; walk and run: the pelvis drops on the swinging leg's side and
  sways over the standing foot (more to the left than to the right), the spine taking back half the drop;
- the jump: the right arm a frame late on the upswing and 10 degrees lower overhead, the head turning at the top,
  the chest twisting in the air, the landing settling onto the left foot; its first and last frames unchanged.
The feet are pinned where they were (blender_legs.py: snapshot_feet, pin_feet), so nothing the fitting did moves.
"""
import math

import bpy

R = math.radians


def _bag():
    act = bpy.data.objects["Avatar"].animation_data.action
    return act, [cb for layer in act.layers for st in layer.strips for cb in st.channelbags][0]


def _curve(bone, prop, idx, cyclic):
    _, cb = _bag()
    path = f'pose.bones["{bone}"].{prop}'
    fc = cb.fcurves.find(path, index=idx)
    if fc is None:
        fc = cb.fcurves.new(path, index=idx)
        if cyclic:
            fc.modifiers.new('CYCLES')
    return fc


def _unit(prop):
    return (lambda v: R(v)) if prop.startswith("rotation") else (lambda v: v / 100.0)


def offset(bone, prop, idx, deltas, cyclic):
    """Add deltas ({frame: degrees or cm}) to a channel, keyed at those frames (made if it isn't there)."""
    fc = _curve(bone, prop, idx, cyclic); u = _unit(prop)
    base = {f: fc.evaluate(f) for f in deltas}
    for f, dv in deltas.items():
        fc.keyframe_points.insert(f, base[f] + u(dv))
    _tidy(fc)


def scale(bone, prop, idx, factor):
    """Scale a channel's keys about their mean (a wider or narrower swing)."""
    fc = _curve(bone, prop, idx, False)
    vals = [k.co[1] for k in fc.keyframe_points]
    mean = sum(vals[:-1]) / max(1, len(vals) - 1)
    for k in fc.keyframe_points:
        k.co[1] = mean + (k.co[1] - mean) * factor
    _tidy(fc)


def add_all(bone, prop, idx, dv):
    """Add one amount to every key of a channel (a lasting offset)."""
    fc = _curve(bone, prop, idx, False); u = _unit(prop)
    for k in fc.keyframe_points:
        k.co[1] += u(dv)
    _tidy(fc)


def shift(bone, prop, idx, df, frames=None):
    """Move a channel's keys (those at frames, or all) later by df frames."""
    fc = _curve(bone, prop, idx, False)
    for k in sorted(fc.keyframe_points, key=lambda k: -k.co[0]):
        if frames is None or round(k.co[0]) in frames:
            k.co[0] += df; k.handle_left[0] += df; k.handle_right[0] += df
    _tidy(fc)


def set_keys(bone, prop, idx, values):
    """Set a channel's value at frames ({frame: degrees or cm}), keyed."""
    fc = _curve(bone, prop, idx, False); u = _unit(prop)
    for f, v in values.items():
        fc.keyframe_points.insert(f, u(v))
    _tidy(fc)


def _tidy(fc):
    for k in fc.keyframe_points:
        k.interpolation = 'BEZIER'; k.handle_left_type = k.handle_right_type = 'AUTO_CLAMPED'
    fc.update()


def _walk():
    E, L = 30, True  # the loop's last frame (= 0), cyclic
    scale("LeftUpperArm", "rotation_euler", 0, 1.10); scale("RightUpperArm", "rotation_euler", 0, 0.85)
    shift("RightUpperArm", "rotation_euler", 0, 1)
    add_all("LeftLowerArm", "rotation_euler", 1, -4)
    offset("Head", "rotation_euler", 2, {0: 2.5, 15: 2.0, E: 2.5}, L)       # tilt to the right
    offset("Head", "rotation_euler", 1, {0: 2.0, 15: 2.5, E: 2.0}, L)       # turned a little left
    offset("Head", "rotation_euler", 0, {0: 0.3, 3: 1.5, 8: -0.6, 11: -0.3, 15: 0.3, 18: 1.1, 23: -0.5, 26: -0.2, E: 0.3}, L)
    offset("LeftShoulder", "rotation_euler", 2, {0: -1.0, 8: 0.6, 15: 2.5, 23: 0.8, E: -1.0}, L)
    offset("RightShoulder", "rotation_euler", 2, {0: -0.5, 8: 0.6, 15: 2.0, 23: 1.0, E: -0.5}, L)
    offset("Chest", "rotation_euler", 2, {0: 0, 8: -1.5, 15: 0, 23: 1.0, E: 0}, L)
    offset("Hips", "rotation_euler", 2, {0: 0, 8: 3.0, 15: 0, 23: -2.2, E: 0}, L)
    offset("Spine", "rotation_euler", 2, {0: 0, 8: -1.5, 15: 0, 23: 1.1, E: 0}, L)
    offset("Hips", "location", 0, {0: 0, 8: 1.2, 15: 0, 23: -1.0, E: 0}, L)


def _run():
    E, L = 20, True
    scale("LeftUpperArm", "rotation_euler", 0, 1.08); scale("RightUpperArm", "rotation_euler", 0, 0.88)
    shift("RightUpperArm", "rotation_euler", 0, 1)
    add_all("LeftLowerArm", "rotation_euler", 1, -5)
    offset("Head", "rotation_euler", 2, {0: 2.0, 10: 1.5, E: 2.0}, L)
    offset("Head", "rotation_euler", 1, {0: -2.5, 10: -2.0, E: -2.5}, L)
    offset("Head", "rotation_euler", 0, {0: 0.2, 1: 1.5, 6: -0.8, 11: 1.0, 16: -0.6, E: 0.2}, L)
    offset("LeftShoulder", "rotation_euler", 2, {0: -1.0, 5: 0.8, 10: 3.0, 15: 0.8, E: -1.0}, L)
    offset("RightShoulder", "rotation_euler", 2, {0: -1.5, 5: 0.5, 10: 2.5, 15: 1.0, E: -1.5}, L)
    offset("Chest", "rotation_euler", 2, {0: 0, 4: -2.0, 10: 0, 14: 1.4, E: 0}, L)
    offset("Hips", "rotation_euler", 2, {0: 0, 4: 4.0, 10: 0, 14: -3.0, E: 0}, L)
    offset("Spine", "rotation_euler", 2, {0: 0, 4: -2.0, 10: 0, 14: 1.5, E: 0}, L)
    offset("Hips", "location", 0, {0: 0, 4: 1.0, 10: 0, 14: -0.8, E: 0}, L)


def _jump():
    late = {13, 14, 15, 21}
    shift("RightUpperArm", "rotation_euler", 0, 1, late)
    shift("RightLowerArm", "rotation_euler", 1, 1, late)
    set_keys("RightUpperArm", "rotation_euler", 0, {22: -140})
    offset("LeftLowerArm", "rotation_euler", 1, {21: -4}, False)
    offset("Head", "rotation_euler", 2, {0: 0, 15: 2, 21: 4, 27: 1, 31: -2, 36: -0.5, 40: 0}, False)
    offset("Head", "rotation_euler", 1, {0: 0, 15: 1, 21: 5, 27: 1, 31: -3, 36: -1, 40: 0}, False)
    offset("Spine", "rotation_euler", 1, {0: 0, 15: 0, 18: 3, 21: 5, 25: 2, 28: 0, 40: 0}, False)
    offset("LeftShoulder", "rotation_euler", 2, {0: 0, 14: 1, 21: 2, 27: 0.5, 40: 0}, False)
    offset("RightShoulder", "rotation_euler", 2, {0: 0, 15: -1, 22: -3, 27: -0.5, 40: 0}, False)
    offset("Hips", "location", 0, {0: 0, 27: 0, 28: 0.5, 31: 1.5, 35: 0.6, 40: 0}, False)
    offset("Hips", "rotation_euler", 2, {0: 0, 27: 0, 31: 2.5, 35: 1.0, 40: 0}, False)
    offset("Spine", "rotation_euler", 2, {0: 0, 27: 0, 31: -1.5, 35: -0.5, 40: 0}, False)


def add_life(which, ns=None):
    """Snapshot the feet, lay the variation over the body, pin the feet back. Returns the feet's worst miss (mm)."""
    g = ns or globals()
    snap = g["snapshot_feet"]()
    {"walk": _walk, "run": _run, "jump": _jump}[which]()
    return g["pin_feet"](snap)
