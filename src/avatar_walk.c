// A walk made in code (avatar.h, xsdk_avatar_animation_walk): the stock animations have none (SPECS.md, section 4.5),
// so until the library ships authored ones (milestone 11) this one is computed. No asset: each foot follows a gait,
// and the legs are solved to reach it, on the hinges the stock animations use (SPECS.md, section 4.4): hips and knees
// about x, the feet about x at the ankle and the toes. Builds for the PC.
//
// The gait of one leg over a cycle, t from 0 to 1 (the right leg half a cycle behind the left):
// - stance, t < STANCE: the foot on the floor moves back under the body at an even speed, which is the walking speed,
//   so that it doesn't slide; it lands on the heel, toe up, rolls flat, and from HEEL_OFF lifts the heel, rolling over
//   the ball of the foot, the toes flat on the floor;
// - swing: the foot comes forward, eased in and out, lifted clear of the floor, its toe coming up for the next step.
// The body sinks as far as the standing leg needs to reach, shifts over the standing foot, its pelvis turning with the
// swinging leg and dipping on its side; the chest and head turn back against it, the arms swing against the legs.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "xsdk/avatar.h"

enum {
	WALK_FRAMES = 32, // one cycle; one more frame repeats the first, so that it loops (xsdk_avatar_animation_length)
	SIDES = 2,        // left, right
};

static const float pi = 3.14159265f;
static const float cycle_seconds = 1.05f;
static const float stance = 0.6f;     // the share of a cycle a foot is on the floor
static const float heel_off = 0.42f;  // when the heel starts to lift
static const float stride = 0.46f;    // how far a foot goes ahead of and behind its hip: times the leg's length
static const float clearance = 0.09f;  // how high the swinging foot lifts, metres, early in its swing
static const float heel_strike = -0.35f; // the foot's pitch landing: toe up
static const float toe_off = 0.70f;      // and leaving: toe down, the heel up
static const float reach = 0.995f;       // of the leg's full length, at most
static const float hip_drop = 0.018f;    // the least the body sinks: the standing knee stays a little bent
static const float arm_swing = 0.45f;    // of the other leg's hip angle
static const float elbow_swing = 0.30f;  // radians more the elbow bends as the arm swings forward
static const float wrist_lag = 1.6f;     // how much the hand trails the arm's swing (of its change over lag_phase)
static const float lag_phase = 0.08f;    // of a cycle
static const float pelvis_turn = 0.30f;  // of the hips' difference
static const float pelvis_dip = 0.05f;   // radians, on the swinging side
static const float chest_turn = 1.6f;    // against the pelvis
static const float lean = 0.05f;         // forward, from the lower back
static const float sway = 0.016f;        // metres, over the standing foot
// Without a skeleton: a leg's lengths and how far a hip is out from the middle.
static const float default_thigh = 0.27f, default_shin = 0.28f, default_foot = 0.10f, default_hip_x = 0.07f;

// Bones the walk poses itself from the rest pose: the root, the legs and their skin helpers (SPECS.md, section 4.4).
static const uint8_t leg_bones[] = { 0, 2, 3, 4, 6, 7, 8, 9, 11, 13, 15, 17, 21, 23 };

// Each bone's mirror image across the avatar's middle (x = 0): left and right swapped, the middle its own.
static uint8_t mirror_of(uint32_t b)
{
	static const uint8_t pairs[][2] = { { 2, 3 }, { 6, 8 }, { 7, 9 }, { 11, 15 }, { 12, 16 }, { 13, 17 }, { 20, 22 },
		{ 21, 23 }, { 25, 28 }, { 26, 29 }, { 27, 30 }, { 31, 34 }, { 32, 35 }, { 33, 36 } };
	for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
		if (pairs[i][0] == b)
			return pairs[i][1];
		if (pairs[i][1] == b)
			return pairs[i][0];
	}
	if (b >= 37 && b <= 43)
		return (uint8_t)(b + 7); // the left hand's fingers and their right counterparts
	if (b >= 44 && b <= 50)
		return (uint8_t)(b - 7);
	if ((b >= 51 && b <= 55) || (b >= 61 && b <= 65))
		return (uint8_t)(b + 5);
	if ((b >= 56 && b <= 60) || (b >= 66 && b <= 70))
		return (uint8_t)(b - 5);
	return (uint8_t)b;
}

// a times b: the rotation b, then a.
static void multiply(float out[4], const float a[4], const float b[4])
{
	const float q[4] = {
		a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
		a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
		a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
		a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
	};
	memcpy(out, q, sizeof q);
}

// The bone's pose turned further about a world axis (its rest frame keeps the world axes).
static void turn(XsdkAvatarBonePose *bone, char axis, float angle)
{
	float q[4];
	xsdk_avatar_quaternion_from_axis(q, axis == 'x', axis == 'y', axis == 'z', angle);
	multiply(bone->rotation, q, bone->rotation);
}

// The standing pose's upper body, made symmetric: base and its mirror image, half each (an idle stand leans on one
// leg); or the rest pose with the arms lowered, as the hand-set pose of milestone 6. The legs and root: the rest pose.
static void upper_body(XsdkAvatarBonePose *pose, const XsdkAvatarBonePose *base)
{
	if (!base) {
		xsdk_avatar_pose_reset(pose, XSDK_AVATAR_BONES);
		turn(&pose[XSDK_AVATAR_BONE_SHOULDER_LEFT], 'z', -1.25f);
		turn(&pose[XSDK_AVATAR_BONE_SHOULDER_RIGHT], 'z', 1.25f);
	} else {
		XsdkAvatarBonePose mirrored[XSDK_AVATAR_BONES];
		for (uint32_t b = 0; b < XSDK_AVATAR_BONES; b++) {
			const XsdkAvatarBonePose *from = &base[mirror_of(b)];
			// A reflection in x: rotations keep their x and turn the other way about y and z.
			mirrored[b] = (XsdkAvatarBonePose){ .translation = { -from->translation[0], from->translation[1],
													from->translation[2] },
				.rotation = { from->rotation[0], -from->rotation[1], -from->rotation[2], from->rotation[3] },
				.scale = { from->scale[0], from->scale[1], from->scale[2] } };
		}
		xsdk_avatar_pose_blend(pose, base, mirrored, 0.5f, XSDK_AVATAR_BONES);
	}
	XsdkAvatarBonePose rest;
	xsdk_avatar_pose_reset(&rest, 1);
	for (size_t i = 0; i < sizeof leg_bones; i++)
		pose[leg_bones[i]] = rest;
}

// A leg's measures, in the plane it swings in (z forward, d down from the hip), from the skeleton's rest pose.
typedef struct {
	float thigh, shin, foot;  // lengths: hip to knee, knee to ankle, ankle to the ball of the foot (forward)
	float thigh_at, shin_at;  // each one's lean forward of straight down at rest, radians
	float ankle_z, ankle_d;   // the ankle at rest, from the hip
	float hip_x;              // the left hip, out from the root (the right one mirrors it)
} Leg;

static void measure(Leg *leg, const XsdkAvatarSkeleton *skeleton)
{
	if (!skeleton || skeleton->bone_count != XSDK_AVATAR_BONES) {
		*leg = (Leg){ .thigh = default_thigh, .shin = default_shin, .foot = default_foot,
			.ankle_d = default_thigh + default_shin, .hip_x = default_hip_x };
		return;
	}
	const float(*at)[3] = skeleton->positions;
	const float *hip = at[XSDK_AVATAR_BONE_HIP_LEFT], *knee = at[XSDK_AVATAR_BONE_KNEE_LEFT];
	const float *ankle = at[XSDK_AVATAR_BONE_ANKLE_LEFT], *toe = at[XSDK_AVATAR_BONE_TOE_LEFT];
	const float thigh_z = knee[2] - hip[2], thigh_d = hip[1] - knee[1];
	const float shin_z = ankle[2] - knee[2], shin_d = knee[1] - ankle[1];
	leg->thigh = sqrtf(thigh_z * thigh_z + thigh_d * thigh_d);
	leg->shin = sqrtf(shin_z * shin_z + shin_d * shin_d);
	leg->thigh_at = atan2f(thigh_z, thigh_d);
	leg->shin_at = atan2f(shin_z, shin_d);
	leg->ankle_z = ankle[2] - hip[2];
	leg->ankle_d = hip[1] - ankle[1];
	leg->foot = toe[2] - ankle[2] > 0.02f ? toe[2] - ankle[2] : default_foot;
	leg->hip_x = hip[0] - at[XSDK_AVATAR_BONE_ROOT][0];
}

// The hip's and knee's angles about x that put the ankle at (z, d) from the hip: two links, the knee bending back.
// A negative hip angle swings the leg forward, a positive knee angle bends it (the hand-set pose of milestone 6).
static void solve(const Leg *leg, float z, float d, float *hip, float *knee)
{
	const float longest = reach * (leg->thigh + leg->shin), shortest = fabsf(leg->thigh - leg->shin) + 0.01f;
	float distance = sqrtf(z * z + d * d);
	distance = distance > longest ? longest : distance < shortest ? shortest : distance;
	float c = (distance * distance - leg->thigh * leg->thigh - leg->shin * leg->shin) / (2.0f * leg->thigh * leg->shin);
	c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
	const float bend = acosf(c);
	const float thigh_angle = atan2f(z, d) + asinf(leg->shin * sinf(bend) / distance);
	const float shin_angle = thigh_angle - bend;
	*hip = leg->thigh_at - thigh_angle;
	*knee = leg->shin_at - *hip - shin_angle;
}

static float smooth(float u)
{
	return u * u * (3.0f - 2.0f * u);
}

// One leg at t: the foot's place ahead of its rest place (z), how far it lifts (the ankle, metres), its pitch (toe
// down positive) and the toes' bend; and how much it swings (0 standing, 1 mid-swing).
typedef struct {
	float z, lift, pitch, toes, swinging;
} Foot;

static Foot foot_at(const Leg *leg, float t)
{
	const float a = stride * (leg->thigh + leg->shin);
	Foot f = { 0 };
	if (t < stance) {
		f.z = a * (1.0f - 2.0f * t / stance);
		if (t < 0.1f) {
			f.pitch = heel_strike * (1.0f - smooth(t / 0.1f));
		} else if (t > heel_off) {
			// Rolling over the ball of the foot: the ankle rises and comes forward as the heel lifts.
			f.pitch = toe_off * smooth((t - heel_off) / (stance - heel_off));
			f.lift = leg->foot * sinf(f.pitch);
			f.z += leg->foot * (1.0f - cosf(f.pitch));
			f.toes = -f.pitch;
		}
		return f;
	}
	const float u = (t - stance) / (1.0f - stance);
	const float rolled = leg->foot * (1.0f - cosf(toe_off));
	f.z = -a + rolled * (1.0f - u) + 2.0f * a * smooth(u);
	// Highest a third of the way through, as a knee folds most early in its swing.
	f.lift = leg->foot * sinf(toe_off) * (1.0f - u) * (1.0f - u) + clearance * sinf(pi * powf(u, 0.7f));
	f.pitch = toe_off + (heel_strike - toe_off) * smooth(u);
	f.toes = -toe_off * (1.0f - u) * (1.0f - u) * (1.0f - u);
	f.swinging = sinf(pi * u);
	return f;
}

// How far the body must sink for a standing foot (or both) to reach its place.
static float sink(const Leg *leg, const Foot *feet, const float *t)
{
	float drop = hip_drop;
	const float longest = reach * (leg->thigh + leg->shin);
	for (uint32_t side = 0; side < SIDES; side++) {
		if (t[side] >= stance)
			continue;
		const float z = leg->ankle_z + feet[side].z;
		const float d = z < longest ? sqrtf(longest * longest - z * z) : 0.0f;
		const float need = leg->ankle_d - feet[side].lift - d;
		drop = need > drop ? need : drop;
	}
	return drop;
}

// The legs at a phase: both feet, how far the body sinks, the pelvis's turn and dip, and the hips' and knees'
// angles that reach the feet from where the turned pelvis carries the hips.
typedef struct {
	Foot feet[SIDES];
	float drop, yaw, dip;
	float hips[SIDES], knees[SIDES];
} Legs;

static Legs legs_at(const Leg *leg, float phase)
{
	Legs l;
	const float t[SIDES] = { phase, fmodf(phase + 0.5f, 1.0f) };
	for (uint32_t side = 0; side < SIDES; side++)
		l.feet[side] = foot_at(leg, t[side]);
	l.drop = sink(leg, l.feet, t);
	for (uint32_t side = 0; side < SIDES; side++)
		solve(leg, leg->ankle_z + l.feet[side].z, leg->ankle_d - l.drop - l.feet[side].lift, &l.hips[side],
			&l.knees[side]);
	l.yaw = pelvis_turn * 0.5f * (l.hips[0] - l.hips[1]);
	l.dip = pelvis_dip * (l.feet[1].swinging - l.feet[0].swinging);
	// The pelvis's turn and dip carry the hips forward or back and up or down: solved again from where they are, so
	// that a standing foot keeps still.
	for (uint32_t side = 0; side < SIDES; side++) {
		const float out = side == 0 ? leg->hip_x : -leg->hip_x;
		const float z = leg->ankle_z + l.feet[side].z + out * sinf(l.yaw);
		const float d = leg->ankle_d - l.drop - l.feet[side].lift + out * sinf(l.dip);
		solve(leg, z, d, &l.hips[side], &l.knees[side]);
	}
	return l;
}

// One frame: the legs solved to their feet, the body sunk, swayed and turned, the arms swung, the hands trailing.
static void pose_frame(XsdkAvatarBonePose *pose, const Leg *leg, float phase)
{
	const Legs now = legs_at(leg, phase), before = legs_at(leg, fmodf(phase + 1.0f - lag_phase, 1.0f));
	const Foot *feet = now.feet;
	const float *hips = now.hips, *knees = now.knees;
	const float drop = now.drop, yaw = now.yaw, dip = now.dip;
	XsdkAvatarBonePose *root = &pose[XSDK_AVATAR_BONE_ROOT];
	root->translation[1] = -drop;
	root->translation[0] = sway * cosf(2.0f * pi * (phase - 0.3f));
	turn(root, 'z', dip);
	turn(root, 'y', yaw);
	const uint8_t hip_bones[SIDES] = { XSDK_AVATAR_BONE_HIP_LEFT, XSDK_AVATAR_BONE_HIP_RIGHT };
	const uint8_t knee_bones[SIDES] = { XSDK_AVATAR_BONE_KNEE_LEFT, XSDK_AVATAR_BONE_KNEE_RIGHT };
	const uint8_t ankle_bones[SIDES] = { XSDK_AVATAR_BONE_ANKLE_LEFT, XSDK_AVATAR_BONE_ANKLE_RIGHT };
	const uint8_t toe_bones[SIDES] = { XSDK_AVATAR_BONE_TOE_LEFT, XSDK_AVATAR_BONE_TOE_RIGHT };
	const uint8_t shoulder_bones[SIDES] = { XSDK_AVATAR_BONE_SHOULDER_LEFT, XSDK_AVATAR_BONE_SHOULDER_RIGHT };
	const uint8_t elbow_bones[SIDES] = { XSDK_AVATAR_BONE_ELBOW_LEFT, XSDK_AVATAR_BONE_ELBOW_RIGHT };
	const uint8_t wrist_bones[SIDES] = { XSDK_AVATAR_BONE_WRIST_LEFT, XSDK_AVATAR_BONE_WRIST_RIGHT };
	for (uint32_t side = 0; side < SIDES; side++) {
		// The legs swing straight ahead whatever the pelvis does: its turn and dip taken back at the hips.
		turn(&pose[hip_bones[side]], 'x', hips[side]);
		turn(&pose[hip_bones[side]], 'y', -yaw);
		turn(&pose[hip_bones[side]], 'z', -dip);
		turn(&pose[knee_bones[side]], 'x', knees[side]);
		turn(&pose[ankle_bones[side]], 'x', feet[side].pitch - hips[side] - knees[side]);
		turn(&pose[toe_bones[side]], 'x', feet[side].toes);
		// Each arm swings with the other leg, its elbow bending more as it comes forward (about y, mirrored).
		const float swing = arm_swing * hips[1 - side];
		turn(&pose[shoulder_bones[side]], 'x', swing);
		const float forward = swing < 0.0f ? -swing / (arm_swing * 0.6f) : 0.0f;
		const float bend = elbow_swing * (forward > 1.0f ? 1.0f : forward);
		turn(&pose[elbow_bones[side]], 'y', side == 0 ? -bend : bend);
		// The hand trails: bent back while the arm swings forward, forward while it swings back (about y, the
		// elbow's way round; positive for the arm's back, as the shoulder's x).
		const float trail = wrist_lag * (arm_swing * before.hips[1 - side] - swing);
		turn(&pose[wrist_bones[side]], 'y', side == 0 ? trail : -trail);
	}
	turn(&pose[XSDK_AVATAR_BONE_BACK_LOWER], 'z', -0.8f * dip);
	turn(&pose[XSDK_AVATAR_BONE_BACK_LOWER], 'x', lean);
	turn(&pose[XSDK_AVATAR_BONE_BACK_UPPER], 'y', -chest_turn * yaw);
	turn(&pose[XSDK_AVATAR_BONE_HEAD], 'y', (chest_turn - 1.0f) * yaw);
}

// Where the ankles are in a pose: their skin matrices applied to their rest positions (y up).
static void ankle_heights(const XsdkAvatarSkeleton *skeleton, const XsdkAvatarBonePose *pose, float y[SIDES])
{
	float m[XSDK_AVATAR_BONES][12];
	xsdk_avatar_skin_matrices(skeleton, pose, m);
	const uint8_t bones[SIDES] = { XSDK_AVATAR_BONE_ANKLE_LEFT, XSDK_AVATAR_BONE_ANKLE_RIGHT };
	for (uint32_t side = 0; side < SIDES; side++) {
		const float *r = m[bones[side]], *p = skeleton->positions[bones[side]];
		y[side] = r[4] * p[0] + r[5] * p[1] + r[6] * p[2] + r[7];
	}
}

// With the skeleton, the body's height measured rather than worked out: each frame moved up or down until the foot
// that stands (the one meant to be lifted least) is as high as it is meant to be over the floor.
static void settle(XsdkAvatarAnimation *walk, const Leg *leg, const XsdkAvatarSkeleton *skeleton)
{
	XsdkAvatarBonePose rest[XSDK_AVATAR_BONES];
	xsdk_avatar_pose_reset(rest, XSDK_AVATAR_BONES);
	float floor[SIDES], y[SIDES];
	ankle_heights(skeleton, rest, floor);
	for (uint32_t f = 0; f < walk->frame_count; f++) {
		const float phase = (float)(f % WALK_FRAMES) / (float)WALK_FRAMES;
		const float t[SIDES] = { phase, fmodf(phase + 0.5f, 1.0f) };
		const Foot feet[SIDES] = { foot_at(leg, t[0]), foot_at(leg, t[1]) };
		const uint32_t standing = feet[0].lift <= feet[1].lift ? 0 : 1;
		XsdkAvatarBonePose *pose = walk->poses + (size_t)f * XSDK_AVATAR_BONES;
		ankle_heights(skeleton, pose, y);
		pose[XSDK_AVATAR_BONE_ROOT].translation[1] += floor[standing] + feet[standing].lift - y[standing];
	}
}

int xsdk_avatar_animation_walk(XsdkAvatarAnimation *walk, const XsdkAvatarBonePose *base,
	const XsdkAvatarSkeleton *skeleton, float *speed)
{
	memset(walk, 0, sizeof *walk);
	Leg leg;
	measure(&leg, skeleton);
	walk->frame_count = WALK_FRAMES + 1;
	walk->frames_per_second = (float)WALK_FRAMES / cycle_seconds;
	walk->bone_count = XSDK_AVATAR_BONES;
	walk->poses = malloc(sizeof(XsdkAvatarBonePose) * XSDK_AVATAR_BONES * walk->frame_count);
	if (!walk->poses)
		return XSDK_AVATAR_ERROR_MEMORY;
	XsdkAvatarBonePose standing[XSDK_AVATAR_BONES];
	upper_body(standing, base);
	for (uint32_t f = 0; f < walk->frame_count; f++) {
		XsdkAvatarBonePose *pose = walk->poses + (size_t)f * XSDK_AVATAR_BONES;
		memcpy(pose, standing, sizeof standing);
		pose_frame(pose, &leg, (float)(f % WALK_FRAMES) / (float)WALK_FRAMES);
	}
	if (skeleton && skeleton->bone_count == XSDK_AVATAR_BONES)
		settle(walk, &leg, skeleton);
	// A standing foot moves back under the body by twice the stride in the stance's share of the cycle.
	if (speed)
		*speed = 2.0f * stride * (leg.thigh + leg.shin) / (stance * cycle_seconds);
	return XSDK_AVATAR_OK;
}
