"""Leg clean-up for the library's own animations, run inside Blender (5.1) on an action baked from the IK rig of
milestone 11 (FORMATS.md, section 4.8). Our own code; Blender's Python API only.

  exec(open("tools/blender_legs.py").read(), ns)   # in Blender's Python (the Blender MCP, say), then:
  ns["hinge_fix"]()                 # every frame: legs re-solved as hand-keyed legs move (no twist)
  ns["toe_pivot_fix"](frames)       # those frames: feet rolling about the toe joint, toes in place
  ns["smooth_takeoff"]()            # a jump's hips over push-off and landing (cubic Hermite into the flight)
  ns["twist"]()                     # the most a leg bone turns about its own axis, degrees
  ns["export"](path)                # the avatar and its actions as a .glb for tools/anim_convert.py

Why: an IK helper chain leaves the thighs twisted about their own length (some 30 degrees) with every joint in
place; hinge_fix solves the thigh about x and z and the knee about x so the ankle lands exactly where it was.
The armature is "Avatar", its mesh "Avatar mesh" (our rig, samples/rig_export).
"""
import math

import bpy
from mathutils import Euler, Matrix, Vector


def hinge_fix():
    """Re-solve each leg as the hand-keyed FK legs move: thigh about x and z (no twist), knee about x only, putting
    the ankle exactly where it is; the feet and toes keep their world rotations. Keys every frame of the scene."""
    s = bpy.context.scene; vl = bpy.context.view_layer
    a = bpy.data.objects["Avatar"]; pb = a.pose.bones; d = a.data
    if bpy.context.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    frames = list(range(s.frame_start, s.frame_end + 1))
    B = d.bones
    rel = {sd: (B["Hips"].matrix_local.inverted() @ B[sd + "UpperLeg"].matrix_local,
                B[sd + "UpperLeg"].matrix_local.inverted() @ B[sd + "LowerLeg"].matrix_local,
                B[sd + "LowerLeg"].matrix_local.inverted() @ B[sd + "Foot"].matrix_local) for sd in ("Left", "Right")}
    tgt = {}
    for f in frames:
        s.frame_set(f)
        Mh = pb["Hips"].matrix.copy()
        for sd in ("Left", "Right"):
            tgt[(f, sd)] = (Mh, pb[sd + "Foot"].head.copy(), pb[sd + "Foot"].matrix.copy(), pb[sd + "Toes"].matrix.copy(),
                            tuple(pb[sd + "UpperLeg"].rotation_euler), tuple(pb[sd + "LowerLeg"].rotation_euler))

    def ankle(Mh, R, x, z, k):
        Rt, Rs, Rf = R
        Mt = Mh @ Rt @ Euler((x, 0, z), 'XYZ').to_matrix().to_4x4()
        Ms = Mt @ Rs @ Euler((k, 0, 0), 'XYZ').to_matrix().to_4x4()
        return (Ms @ Rf).translation

    worst = 0; prev = {}; sol = {}
    for f in frames:
        for sd in ("Left", "Right"):
            Mh, T, Fm, Tm, eu, el = tgt[(f, sd)]
            p = list(prev.get(sd, (eu[0], eu[2], el[0])))
            for _ in range(30):
                c = ankle(Mh, rel[sd], *p); err = T - c
                if err.length < 1e-7:
                    break
                J = Matrix.Identity(3); h = 1e-5
                for j in range(3):
                    q = list(p); q[j] += h; dc = (ankle(Mh, rel[sd], *q) - c) / h
                    for i in range(3):
                        J[i][j] = dc[i]
                dp = J.inverted() @ err
                for j in range(3):
                    p[j] += dp[j]
            worst = max(worst, (T - ankle(Mh, rel[sd], *p)).length)
            prev[sd] = tuple(p); sol[(f, sd)] = tuple(p)
    prevE = {}
    for f in frames:
        s.frame_set(f)
        for sd in ("Left", "Right"):
            x, z, k = sol[(f, sd)]; Mh, T, Fm, Tm, eu, el = tgt[(f, sd)]
            pb[sd + "UpperLeg"].rotation_euler = (x, 0, z); pb[sd + "LowerLeg"].rotation_euler = (k, 0, 0); vl.update()
            pb[sd + "Foot"].matrix = Fm; vl.update(); pb[sd + "Toes"].matrix = Tm; vl.update()
            for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes"):
                b = pb[bn]
                if bn in prevE:
                    b.rotation_euler = b.matrix_basis.to_euler(b.rotation_mode, prevE[bn])
                prevE[bn] = b.rotation_euler.copy(); b.keyframe_insert("rotation_euler", frame=f)
    wp = 0; wr = 0
    for f in frames:
        s.frame_set(f)
        for sd in ("Left", "Right"):
            Mh, T, Fm, Tm, eu, el = tgt[(f, sd)]
            wp = max(wp, (pb[sd + "Foot"].head - T).length)
            wr = max(wr, math.degrees(pb[sd + "Foot"].matrix.to_quaternion().rotation_difference(Fm.to_quaternion()).angle),
                     math.degrees(pb[sd + "Toes"].matrix.to_quaternion().rotation_difference(Tm.to_quaternion()).angle))
    return {"solve_err_mm": round(worst * 1000, 4), "ankle_moved_mm": round(wp * 1000, 4), "foot_toes_rot_deg": round(wr, 4)}


def twist():
    s = bpy.context.scene; a = bpy.data.objects["Avatar"]; pb = a.pose.bones; m = 0
    for f in range(s.frame_start, s.frame_end + 1):
        s.frame_set(f)
        for bn in ("LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot"):
            q = pb[bn].matrix_basis.to_quaternion(); m = max(m, abs(math.degrees(2 * math.atan2(q.y, q.w))))
    return round(m, 2)


def export(path):
    s = bpy.context.scene
    for o in s.objects:
        o.select_set(o.name in ("Avatar", "Avatar mesh"))
    bpy.context.view_layer.objects.active = bpy.data.objects["Avatar"]
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_animations=True,
                              export_animation_mode='ACTIONS', export_frame_range=True, export_force_sampling=True,
                              export_frame_step=1)


def toe_pivot_fix(ground):
    """On the frames in ground, keep each foot's world rotation but place it so that its toe joint is where it is at
    rest (the foot rolling about the toe joint, the toes flat), then re-solve the leg as hinge_fix does; the toes keep
    their world rotation. Returns the solve's worst miss."""
    s = bpy.context.scene; vl = bpy.context.view_layer
    a = bpy.data.objects["Avatar"]; pb = a.pose.bones; d = a.data
    B = d.bones
    rel = {sd: (B["Hips"].matrix_local.inverted() @ B[sd + "UpperLeg"].matrix_local,
                B[sd + "UpperLeg"].matrix_local.inverted() @ B[sd + "LowerLeg"].matrix_local,
                B[sd + "LowerLeg"].matrix_local.inverted() @ B[sd + "Foot"].matrix_local) for sd in ("Left", "Right")}

    def ankle(Mh, R, x, z, k):
        Rt, Rs, Rf = R
        Mt = Mh @ Rt @ Euler((x, 0, z), 'XYZ').to_matrix().to_4x4()
        Ms = Mt @ Rs @ Euler((k, 0, 0), 'XYZ').to_matrix().to_4x4()
        return (Ms @ Rf).translation
    worst = 0; moved = 0
    for f in ground:
        s.frame_set(f)
        for sd in ("Left", "Right"):
            Mh = pb["Hips"].matrix.copy(); Fm = pb[sd + "Foot"].matrix.copy(); Tm = pb[sd + "Toes"].matrix.copy()
            toe_rest = B[sd + "Toes"].head_local; ank_rest = B[sd + "Foot"].head_local
            R = Fm.to_3x3() @ B[sd + "Foot"].matrix_local.to_3x3().inverted()
            T = toe_rest - R @ (toe_rest - ank_rest)
            moved = max(moved, (T - pb[sd + "Foot"].head).length)
            p = [pb[sd + "UpperLeg"].rotation_euler[0], pb[sd + "UpperLeg"].rotation_euler[2], pb[sd + "LowerLeg"].rotation_euler[0]]
            for _ in range(40):
                c = ankle(Mh, rel[sd], *p); err = T - c
                if err.length < 1e-7:
                    break
                J = Matrix.Identity(3); h = 1e-5
                for j in range(3):
                    q = list(p); q[j] += h; dc = (ankle(Mh, rel[sd], *q) - c) / h
                    for i in range(3):
                        J[i][j] = dc[i]
                dp = J.inverted() @ err
                for j in range(3):
                    p[j] += dp[j]
            worst = max(worst, (T - ankle(Mh, rel[sd], *p)).length)
            prevs = {bn: pb[bn].rotation_euler.copy() for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes")}
            pb[sd + "UpperLeg"].rotation_euler = (p[0], 0, p[1]); pb[sd + "LowerLeg"].rotation_euler = (p[2], 0, 0); vl.update()
            pb[sd + "Foot"].matrix = Fm; vl.update(); pb[sd + "Toes"].matrix = Tm; vl.update()
            for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes"):
                b = pb[bn]; b.rotation_euler = b.matrix_basis.to_euler(b.rotation_mode, prevs[bn])
                b.keyframe_insert("rotation_euler", frame=f)
    return {"toe_solve_miss_mm": round(worst * 1000, 4), "ankle_moved_mm_max": round(moved * 1000, 2)}


def smooth_takeoff(h_crouch=-0.17, f_crouch=10, h_land=-0.13, f_land=31, H0=0.05, f_off=15, f_on=27, fps=30.0, g=9.81):
    """Hips height (Hips location y) over the push-off and the landing as cubic Hermite curves: from rest in the
    crouch to the flight's takeoff speed at f_off, and from its landing speed at f_on to rest in the landing's
    crouch; the flight between is ballistic. Keys every frame from f_crouch to f_land."""
    a = bpy.data.objects["Avatar"]; pb = a.pose.bones
    T = (f_on - f_off) / fps
    v = g * T / 2 / fps  # metres a frame at takeoff
    def herm(p0, v0, p1, v1, n, k):
        t = k / n
        return ((2*t**3 - 3*t**2 + 1) * p0 + (t**3 - 2*t**2 + t) * n * v0 + (-2*t**3 + 3*t**2) * p1 + (t**3 - t**2) * n * v1)
    h = {}
    for k in range(0, f_off - f_crouch + 1):
        h[f_crouch + k] = herm(h_crouch, 0.0, H0, v, f_off - f_crouch, k)
    for f in range(f_off, f_on + 1):
        t = (f - f_off) / fps
        h[f] = H0 + g * T / 2 * t - g * t * t / 2
    for k in range(0, f_land - f_on + 1):
        h[f_on + k] = herm(H0, -v, h_land, 0.0, f_land - f_on, k)
    for f, y in h.items():
        pb["Hips"].location[1] = y
        pb["Hips"].keyframe_insert("location", index=1, frame=f)
    act = a.animation_data.action
    for l in act.layers:
        for st in l.strips:
            for cb in st.channelbags:
                for fc in cb.fcurves:
                    if fc.data_path == 'pose.bones["Hips"].location' and fc.array_index == 1:
                        for kp in fc.keyframe_points:
                            if f_crouch <= round(kp.co[0]) <= f_land:
                                kp.interpolation = 'LINEAR'
                        fc.update()
    return {f: round(y * 100, 1) for f, y in sorted(h.items())}


def _leg_rel(d, sd):
    B = d.bones
    return (B["Hips"].matrix_local.inverted() @ B[sd + "UpperLeg"].matrix_local,
            B[sd + "UpperLeg"].matrix_local.inverted() @ B[sd + "LowerLeg"].matrix_local,
            B[sd + "LowerLeg"].matrix_local.inverted() @ B[sd + "Foot"].matrix_local)


def place_foot(sd, F, T=None, frame=None):
    """At the current frame: solve the leg as a hinge (thigh about x and z, knee about x) so the ankle is at F's
    translation, give the foot F's rotation and the toes T's (or keep theirs); key the four bones if frame is given.
    The armature at the origin, unrotated. Returns how far the ankle is from F (metres; > 0 if out of reach)."""
    vl = bpy.context.view_layer
    a = bpy.data.objects["Avatar"]; pb = a.pose.bones; d = a.data
    rel = _leg_rel(d, sd)
    Mh = pb["Hips"].matrix.copy()
    if T is None:
        T = pb[sd + "Toes"].matrix.copy()

    def ankle(x, z, k):
        Rt, Rs, Rf = rel
        Mt = Mh @ Rt @ Euler((x, 0, z), 'XYZ').to_matrix().to_4x4()
        Ms = Mt @ Rs @ Euler((k, 0, 0), 'XYZ').to_matrix().to_4x4()
        return (Ms @ Rf).translation
    target = F.translation
    p = [pb[sd + "UpperLeg"].rotation_euler[0], pb[sd + "UpperLeg"].rotation_euler[2], pb[sd + "LowerLeg"].rotation_euler[0]]
    for _ in range(40):
        c = ankle(*p); err = target - c
        if err.length < 1e-7:
            break
        J = Matrix.Identity(3); h = 1e-5
        for j in range(3):
            q = list(p); q[j] += h; dc = (ankle(*q) - c) / h
            for i in range(3):
                J[i][j] = dc[i]
        try:
            dp = J.inverted() @ err
        except ValueError:
            break
        step = max(abs(x) for x in dp)
        if step > 0.3:
            dp = dp * (0.3 / step)
        for j in range(3):
            p[j] += dp[j]
    prevs = {bn: pb[bn].rotation_euler.copy() for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes")}
    pb[sd + "UpperLeg"].rotation_euler = (p[0], 0, p[1]); pb[sd + "LowerLeg"].rotation_euler = (p[2], 0, 0); vl.update()
    pb[sd + "Foot"].matrix = F; vl.update(); pb[sd + "Toes"].matrix = T; vl.update()
    for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes"):
        b = pb[bn]; b.rotation_euler = b.matrix_basis.to_euler(b.rotation_mode, prevs[bn])
        if frame is not None:
            b.keyframe_insert("rotation_euler", frame=frame)
    return (pb[sd + "Foot"].head - target).length


def foot_vertices(sd, weight=0.3):
    ob = bpy.data.objects["Avatar mesh"]
    vg = {g.index: g.name for g in ob.vertex_groups}
    return [v.index for v in ob.data.vertices
            if any(vg.get(g.group) in (sd + "Foot", sd + "Toes") and g.weight > weight for g in v.groups)]


def rest_floor():
    """The lowest foot vertex at rest: where the feet stand."""
    a = bpy.data.objects["Avatar"]; ob = bpy.data.objects["Avatar mesh"]; vl = bpy.context.view_layer
    a.data.pose_position = 'REST'; vl.update()
    dg = bpy.context.evaluated_depsgraph_get(); e = ob.evaluated_get(dg); me = e.to_mesh()
    z = min((ob.matrix_world @ me.vertices[i].co).z for sd in ("Left", "Right") for i in foot_vertices(sd))
    e.to_mesh_clear(); a.data.pose_position = 'POSE'; vl.update()
    return z


def _verts(idx):
    ob = bpy.data.objects["Avatar mesh"]
    dg = bpy.context.evaluated_depsgraph_get(); e = ob.evaluated_get(dg); me = e.to_mesh()
    P = {i: (ob.matrix_world @ me.vertices[i].co).copy() for i in idx}
    e.to_mesh_clear(); return P


def fit_stance(sd, stance, swing, speed, floor):
    """A foot of an in-place locomotion cycle on baked keys: on the stance frames (in order) the foot keeps its
    rotation and is moved so that its lowest vertex is on the floor and the vertex that touched the frame before has
    moved back (+y) by speed (metres a frame), sideways not at all; on the swing frames (in order, from after the
    stance to before the next) the change is blended from the stance's last frame to its first. The legs are solved
    by place_foot, so they don't twist. Returns the worst miss (m) and how the touching vertex moved (mm a frame)."""
    s = bpy.context.scene
    a = bpy.data.objects["Avatar"]; pb = a.pose.bones
    idx = foot_vertices(sd)
    orig = {}
    for f in stance + swing:
        s.frame_set(f); orig[f] = (pb[sd + "Foot"].matrix.copy(), pb[sd + "Toes"].matrix.copy())
    new = {}; anchor = None; miss = 0
    for i, f in enumerate(stance):
        s.frame_set(f)
        F, T = orig[f]
        for _ in range(3):
            place_foot(sd, F, T); P = _verts(idx)
            low = min(P, key=lambda j: P[j].z)
            d = Vector((0, 0, floor - P[low].z))
            if anchor is not None:
                j, at = anchor
                d.x = at.x - P[j].x
                d.y = at.y + speed - P[j].y
            if d.length < 1e-5:
                break
            D = Matrix.Translation(d); F = D @ F; T = D @ T
        miss = max(miss, place_foot(sd, F, T, frame=f))
        P = _verts(idx); low = min(P, key=lambda j: P[j].z); anchor = (low, P[low].copy())
        new[f] = (F, T)

    def blend(A, B, w):
        t_ = A.to_translation().lerp(B.to_translation(), w); q = A.to_quaternion().slerp(B.to_quaternion(), w)
        return Matrix.Translation(t_) @ q.to_matrix().to_4x4()
    cF_end = new[stance[-1]][0] @ orig[stance[-1]][0].inverted(); cF_start = new[stance[0]][0] @ orig[stance[0]][0].inverted()
    cT_end = new[stance[-1]][1] @ orig[stance[-1]][1].inverted(); cT_start = new[stance[0]][1] @ orig[stance[0]][1].inverted()
    n = len(swing) + 1
    for i, f in enumerate(swing):
        w = (i + 1) / n; w = w * w * (3 - 2 * w)
        s.frame_set(f)
        F = blend(cF_end, cF_start, w) @ orig[f][0]; T = blend(cT_end, cT_start, w) @ orig[f][1]
        miss = max(miss, place_foot(sd, F, T, frame=f))
    # how the touching vertex moved, as measured afterwards
    moves = []; prev = None
    for f in stance:
        s.frame_set(f); P = _verts(idx)
        if prev is not None:
            j, at = prev; moves.append(round((P[j].y - at.y) * 1000, 2))
        low = min(P, key=lambda j: P[j].z); prev = (low, P[low].copy())
    return {"miss_mm": round(miss * 1000, 3), "touch_moves_mm": moves}


def copy_frame(src, dst, bones):
    """Key bones at dst with their pose at src (to close a loop)."""
    s = bpy.context.scene; pb = bpy.data.objects["Avatar"].pose.bones
    s.frame_set(src)
    vals = {b: pb[b].rotation_euler.copy() for b in bones}
    for b, v in vals.items():
        pb[b].rotation_euler = v; pb[b].keyframe_insert("rotation_euler", frame=dst)


def align_knees(frames=None):
    """On each frame: re-solve each leg with the thigh turning in the hip too (x, y, z) and the knee about x, so that
    the ankle stays where it is and the foot isn't twisted on the shin (the knee faces where the foot points, as a
    real leg's does: the hip turns, the ankle doesn't). The feet and toes keep their world rotations."""
    s = bpy.context.scene; vl = bpy.context.view_layer
    a = bpy.data.objects["Avatar"]; pb = a.pose.bones; d = a.data
    frames = frames or list(range(s.frame_start, s.frame_end + 1))
    worst = 0; worst_tw = 0
    for f in frames:
        s.frame_set(f)
        for sd in ("Left", "Right"):
            rel = _leg_rel(d, sd)
            Mh = pb["Hips"].matrix.copy(); F = pb[sd + "Foot"].matrix.copy(); T = pb[sd + "Toes"].matrix.copy()
            Rf_rest = d.bones[sd + "Foot"].matrix_local

            def chain(x, y, z, k):
                Rt, Rs, Rf = rel
                Mt = Mh @ Rt @ Euler((x, y, z), 'XYZ').to_matrix().to_4x4()
                Ms = Mt @ Rs @ Euler((k, 0, 0), 'XYZ').to_matrix().to_4x4()
                return Ms @ Rf

            def resid(p):
                M = chain(*p)
                e = F.translation - M.translation
                q = (M.to_3x3().inverted() @ F.to_3x3()).to_quaternion()  # the foot's own turn on the shin
                tw = 2 * math.atan2(q.y, q.w)
                return Vector((e.x, e.y, e.z, tw * 0.05))
            eu = pb[sd + "UpperLeg"].rotation_euler; p = [eu[0], eu[1], eu[2], pb[sd + "LowerLeg"].rotation_euler[0]]
            for _ in range(50):
                r = resid(p)
                if r.length < 1e-8:
                    break
                J = Matrix(((0.0,) * 4,) * 4); h = 1e-5
                for j in range(4):
                    q = list(p); q[j] += h; dr = (resid(q) - r) / h
                    for i in range(4):
                        J[i][j] = dr[i]
                dp = J.inverted() @ (-r)
                step = max(abs(x) for x in dp)
                if step > 0.3:
                    dp = dp * (0.3 / step)
                for j in range(4):
                    p[j] += dp[j]
            r = resid(p); worst = max(worst, Vector(r[:3]).length); worst_tw = max(worst_tw, abs(r[3] / 0.05))
            prevs = {bn: pb[bn].rotation_euler.copy() for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes")}
            pb[sd + "UpperLeg"].rotation_euler = (p[0], p[1], p[2]); pb[sd + "LowerLeg"].rotation_euler = (p[3], 0, 0); vl.update()
            pb[sd + "Foot"].matrix = F; vl.update(); pb[sd + "Toes"].matrix = T; vl.update()
            for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes"):
                b = pb[bn]; b.rotation_euler = b.matrix_basis.to_euler(b.rotation_mode, prevs[bn])
                b.keyframe_insert("rotation_euler", frame=f)
    return {"ankle_miss_mm": round(worst * 1000, 4), "foot_twist_left_deg": round(math.degrees(worst_tw), 3)}


def ankle_twist():
    """The most a foot turns on its shin (about the shin's axis), degrees, over the scene's frames."""
    s = bpy.context.scene; a = bpy.data.objects["Avatar"]; pb = a.pose.bones; m = 0
    for f in range(s.frame_start, s.frame_end + 1):
        s.frame_set(f)
        for sd in ("Left", "Right"):
            q = pb[sd + "Foot"].matrix_basis.to_quaternion()  # the foot's turn from rest on its shin, about its length
            m = max(m, abs(math.degrees(2 * math.atan2(q.y, q.w))))
    return round(m, 2)


def _solve_leg4(sd, F, T, frame=None):
    """Solve one leg for the foot matrix F (armature space): thigh about x, y and z, knee about x, the foot not
    twisted on the shin; feet and toes get F's and T's rotations. Keys the four bones at frame if given. Returns the
    ankle's miss in metres."""
    vl = bpy.context.view_layer
    a = bpy.data.objects["Avatar"]; pb = a.pose.bones; d = a.data
    rel = _leg_rel(d, sd)
    Mh = pb["Hips"].matrix.copy()

    def chain(x, y, z, k):
        Rt, Rs, Rf = rel
        Mt = Mh @ Rt @ Euler((x, y, z), 'XYZ').to_matrix().to_4x4()
        Ms = Mt @ Rs @ Euler((k, 0, 0), 'XYZ').to_matrix().to_4x4()
        return Ms @ Rf

    def resid(p):
        M = chain(*p)
        e = F.translation - M.translation
        q = (M.to_3x3().inverted() @ F.to_3x3()).to_quaternion()
        return Vector((e.x, e.y, e.z, 2 * math.atan2(q.y, q.w) * 0.05))
    eu = pb[sd + "UpperLeg"].rotation_euler; p = [eu[0], eu[1], eu[2], pb[sd + "LowerLeg"].rotation_euler[0]]
    for _ in range(50):
        r = resid(p)
        if r.length < 1e-8:
            break
        J = Matrix(((0.0,) * 4,) * 4); h = 1e-5
        for j in range(4):
            q = list(p); q[j] += h; dr = (resid(q) - r) / h
            for i in range(4):
                J[i][j] = dr[i]
        try:
            dp = J.inverted() @ (-r)
        except ValueError:
            break
        step = max(abs(x) for x in dp)
        if step > 0.3:
            dp = dp * (0.3 / step)
        for j in range(4):
            p[j] += dp[j]
    miss = Vector(resid(p)[:3]).length
    prevs = {bn: pb[bn].rotation_euler.copy() for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes")}
    pb[sd + "UpperLeg"].rotation_euler = (p[0], p[1], p[2]); pb[sd + "LowerLeg"].rotation_euler = (p[3], 0, 0); vl.update()
    pb[sd + "Foot"].matrix = F; vl.update(); pb[sd + "Toes"].matrix = T; vl.update()
    for bn in (sd + "UpperLeg", sd + "LowerLeg", sd + "Foot", sd + "Toes"):
        b = pb[bn]; b.rotation_euler = b.matrix_basis.to_euler(b.rotation_mode, prevs[bn])
        if frame is not None:
            b.keyframe_insert("rotation_euler", frame=frame)
    return miss


def snapshot_feet():
    """Every frame's foot and toes matrices (armature space), to pin the feet with after the body has changed."""
    s = bpy.context.scene; pb = bpy.data.objects["Avatar"].pose.bones; snap = {}
    for f in range(s.frame_start, s.frame_end + 1):
        s.frame_set(f)
        for sd in ("Left", "Right"):
            snap[(f, sd)] = (pb[sd + "Foot"].matrix.copy(), pb[sd + "Toes"].matrix.copy())
    return snap


def pin_feet(snap):
    """Put every foot back where snapshot_feet found it, the legs solved as _solve_leg4 does (knees over the feet).
    Returns the worst miss in mm (more than 0 where a leg can't reach any more)."""
    s = bpy.context.scene; worst = 0
    for f in range(s.frame_start, s.frame_end + 1):
        s.frame_set(f)
        for sd in ("Left", "Right"):
            F, T = snap[(f, sd)]
            worst = max(worst, _solve_leg4(sd, F, T, frame=f))
    return round(worst * 1000, 3)
