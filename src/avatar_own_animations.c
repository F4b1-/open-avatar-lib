// The library's own animations (avatar.h, xsdk_avatar_animation_load_walk and _load_jump; milestone 11): a clip per
// body, keyed in Blender on our rig and converted by tools/anim_convert.py (FORMATS.md, section 4.8),
// embedded from animations/*.xaan by tools/gen_clips.py. And the walking speed of any walk clip
// (xsdk_avatar_animation_ground_speed), from its feet while they are on the floor. Builds for the PC.

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "avatar_internal.h"
#include "avatar_clips.h"
#include "xsdk/avatar.h"

static int measure_ground_speed(const XsdkAvatarAnimation *walk, const XsdkAvatarSkeleton *skeleton, float *speed);

// Loads the body's clip of a walk or a run and, if asked, measures its speed; a measure that runs out of memory fails
// the load (an animation with a speed of 0 would play at the wrong pace without a word).
static int load_with_speed(XsdkAvatarAnimation *animation, uint32_t gender, const uint8_t *male, uint32_t male_size,
	const uint8_t *female, uint32_t female_size, const XsdkAvatarSkeleton *skeleton, float *speed)
{
	const bool is_female = gender == XSDK_AVATAR_FEMALE;
	int result = xsdk_avatar_animation_load(
		animation, is_female ? female : male, is_female ? female_size : male_size, skeleton);
	if (result == XSDK_AVATAR_OK && speed)
		result = measure_ground_speed(animation, skeleton, speed);
	if (result != XSDK_AVATAR_OK)
		xsdk_avatar_animation_free(animation);
	return result;
}

int xsdk_avatar_animation_load_walk(
	XsdkAvatarAnimation *walk, uint32_t gender, const XsdkAvatarSkeleton *skeleton, float *speed)
{
	return load_with_speed(walk, gender, clip_walk_male, clip_walk_male_size, clip_walk_female, clip_walk_female_size,
		skeleton, speed);
}

int xsdk_avatar_animation_load_run(
	XsdkAvatarAnimation *run, uint32_t gender, const XsdkAvatarSkeleton *skeleton, float *speed)
{
	return load_with_speed(run, gender, clip_run_male, clip_run_male_size, clip_run_female, clip_run_female_size,
		skeleton, speed);
}

int xsdk_avatar_animation_load_jump(XsdkAvatarAnimation *jump, uint32_t gender, const XsdkAvatarSkeleton *skeleton)
{
	const int female = gender == XSDK_AVATAR_FEMALE;
	return xsdk_avatar_animation_load(jump, female ? clip_jump_female : clip_jump_male,
		female ? clip_jump_female_size : clip_jump_male_size, skeleton);
}

// Where a bone's rest position goes in a pose (its skin matrix applied to it).
static void posed(const float m[12], const float rest[3], float out[3])
{
	for (uint32_t a = 0; a < 3; a++)
		out[a] = m[4 * a] * rest[0] + m[4 * a + 1] * rest[1] + m[4 * a + 2] * rest[2] + m[4 * a + 3];
}

// Two points on each sole, fixed to the foot: under the ankle (the heel) and under the toe joint (the ball), on the
// floor at rest (height 0 in the meshes' space). A foot that rolls on the floor without sliding has the point that
// touches moving with the floor; so whenever one of these points is on the floor (within ON_FLOOR) in two frames
// running, how far it moved back between them (the body faces +z) is the walking speed times a frame's time. The
// speed is the median of those moves: a few taken while the touch passes from heel to ball are off, most aren't.
static const float on_floor = 0.005f; // metres

static int by_value(const void *a, const void *b)
{
	const float x = *(const float *)a, y = *(const float *)b;
	return (x > y) - (x < y);
}

// The measure; XSDK_AVATAR_ERROR_MEMORY if it can't allocate its list of moves, else OK and *speed (0 if no foot is
// on the floor two frames running).
static int measure_ground_speed(const XsdkAvatarAnimation *walk, const XsdkAvatarSkeleton *skeleton, float *out)
{
	*out = 0.0f;
	if (!walk || !skeleton || walk->frame_count < 2 || walk->bone_count != XSDK_AVATAR_BONES ||
		skeleton->bone_count != XSDK_AVATAR_BONES)
		return XSDK_AVATAR_OK;
	static const uint32_t ankles[2] = { XSDK_AVATAR_BONE_ANKLE_LEFT, XSDK_AVATAR_BONE_ANKLE_RIGHT };
	static const uint32_t toes[2] = { XSDK_AVATAR_BONE_TOE_LEFT, XSDK_AVATAR_BONE_TOE_RIGHT };
	float sole[2][2][3];
	for (uint32_t s = 0; s < 2; s++)
		for (uint32_t p = 0; p < 2; p++) {
			const float *joint = skeleton->positions[p ? toes[s] : ankles[s]];
			sole[s][p][0] = joint[0];
			sole[s][p][1] = 0.0f;
			sole[s][p][2] = joint[2];
		}
	// Up to one move a frame for each of the four sole points.
	float *moves = avatar_malloc(sizeof(float) * 4 * walk->frame_count, "a walk speed measure");
	if (!moves)
		return XSDK_AVATAR_ERROR_MEMORY;
	uint32_t count = 0;
	float m[XSDK_AVATAR_BONES][12];
	float before[2][2][3];
	for (uint32_t f = 0; f < walk->frame_count; f++) {
		xsdk_avatar_skin_matrices(skeleton, walk->poses + (size_t)f * walk->bone_count, m);
		for (uint32_t s = 0; s < 2; s++)
			for (uint32_t p = 0; p < 2; p++) {
				float at[3];
				posed(m[ankles[s]], sole[s][p], at);
				if (f && fabsf(at[1]) < on_floor && fabsf(before[s][p][1]) < on_floor)
					moves[count++] = before[s][p][2] - at[2];
				for (uint32_t a = 0; a < 3; a++)
					before[s][p][a] = at[a];
			}
	}
	float speed = 0.0f;
	if (count) {
		qsort(moves, count, sizeof *moves, by_value);
		speed = (count % 2 ? moves[count / 2] : 0.5f * (moves[count / 2 - 1] + moves[count / 2])) *
			walk->frames_per_second;
	}
	free(moves);
	*out = speed;
	return XSDK_AVATAR_OK;
}

float xsdk_avatar_animation_ground_speed(const XsdkAvatarAnimation *walk, const XsdkAvatarSkeleton *skeleton)
{
	float speed;
	(void)measure_ground_speed(walk, skeleton, &speed); // 0 if it runs out of memory, as avatar.h says
	return speed;
}
