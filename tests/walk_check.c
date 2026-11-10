// PC test of the library's own animations (milestone 11; src/avatar_own_animations.c, embedded from animations/),
// on both bodies of the console's stock pack, by the skinned feet:
//   walk_check <FFFE07DF00000002>              all three below
//   walk_check <FFFE07DF00000002> walk         the walk (see check_clip): it loops, a foot down neither slides nor
//                                              sinks, at the speed xsdk_avatar_animation_ground_speed measures, the
//                                              feet keep their width, the knees and ankles don't twist
//   walk_check <FFFE07DF00000002> run          the run, the same way
//   walk_check <FFFE07DF00000002> jump         the jump (see check_jump)
//   walk_check <FFFE07DF00000002> clip <walk.xaan> [<female walk.xaan>]   a walk of your own, converted by
//                                              tools/anim_convert.py: one clip for both bodies, or one each
// Exits 1 if a check fails.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_clips.h"
#include "xsdk/avatar.h"

static int read_file(void *context, uint64_t offset, void *buffer, uint32_t length)
{
	FILE *file = context;
	if (fseek(file, (long)offset, SEEK_SET) != 0)
		return 1;
	return fread(buffer, 1, length, file) == length ? 0 : 1;
}

static void *read_record(const XsdkAvatarPack *pack, const char *name, uint32_t *size)
{
	uint32_t record;
	if (xsdk_avatar_pack_find(pack, name, &record) != XSDK_AVATAR_OK)
		return NULL;
	*size = xsdk_avatar_pack_asset_size(pack, record);
	void *strb = malloc(*size);
	if (strb && xsdk_avatar_pack_read_asset(pack, record, strb) != XSDK_AVATAR_OK) {
		free(strb);
		return NULL;
	}
	return strb;
}

// Where a bone is in a pose: its skin matrix applied to its rest position.
static void posed(const float m[12], const float *rest, float out[3])
{
	for (uint32_t a = 0; a < 3; a++)
		out[a] = m[4 * a] * rest[0] + m[4 * a + 1] * rest[1] + m[4 * a + 2] * rest[2] + m[4 * a + 3];
}

static void *read_whole(const char *path, uint32_t *size)
{
	FILE *file = fopen(path, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	*size = (uint32_t)ftell(file);
	fseek(file, 0, SEEK_SET);
	void *bytes = malloc(*size);
	if (bytes && fread(bytes, 1, *size, file) != *size) {
		free(bytes);
		bytes = NULL;
	}
	fclose(file);
	return bytes;
}

// The authored walk (milestone 11), converted from Blender (tools/anim_convert.py), checked on each body with the clip
// moved onto its skeleton, by the skinned skin of the feet (the vertices an ankle or a toe moves by a third or more):
// it loops; the lowest point of each foot never goes into the floor (where the foot stands at rest) by more than
// 3 mm; while a foot is down (its lowest point within 1 mm of the floor) the point that touches doesn't slide: from
// one frame to the next it moves back at an even speed, the walking speed, which is measured here and must be the
// same for both feet; a foot in the air clears the floor by 3 cm at its highest; the feet keep their width.
static void foot_points(const XsdkAvatarModel *body, const float (*m)[12], uint32_t ankle, uint32_t toe, float *out,
	uint32_t *count)
{
	uint32_t n = 0;
	for (uint32_t k = 0; k < body->mesh_count; k++) {
		const XsdkAvatarMesh *mesh = &body->meshes[k];
		for (uint32_t v = 0; v < mesh->vertex_count; v++) {
			uint32_t foot = 0;
			for (uint32_t j = 0; j < 4; j++)
				if (mesh->bones[4 * v + j] == ankle || mesh->bones[4 * v + j] == toe)
					foot += mesh->weights[4 * v + j];
			if (foot < 85)
				continue;
			float o[3] = { 0, 0, 0 };
			for (uint32_t j = 0; j < 4; j++) {
				float p[3];
				posed(m[mesh->bones[4 * v + j]], mesh->positions + 3 * v, p);
				for (uint32_t a = 0; a < 3; a++)
					o[a] += (float)mesh->weights[4 * v + j] / 255.0f * p[a];
			}
			if (out)
				memcpy(out + 3 * n, o, sizeof o);
			n++;
		}
	}
	*count = n;
}

// How much a leg turns about its own length in an animation, in degrees: the twist of a swing-twist split of a
// bone's pose rotation (the change from rest in its rest frame, which on our skeleton is the meshes' axes) about y,
// the legs' length at rest. Not about the line between the joints: this rig's rest leg isn't straight (the shin's
// line leans 6 degrees), and a foot pointed 55 degrees would show 6 degrees of twist about it that isn't there.
// hip: the thigh turning in the hip; a real hip turns some (with the pelvis, in a run). hinge: the shin turning on
// the thigh and the foot on the shin; a knee is a hinge and an ankle hardly turns, so this stays near 0. A leg
// solved by IK and baked can twist with every joint in place: the thigh one way, the foot back (the first baked
// walks: 31 degrees).
static void leg_twist(
	const XsdkAvatarAnimation *animation, const XsdkAvatarSkeleton *skeleton, float *hip, float *hinge)
{
	(void)skeleton;
	// The first two are the hips.
	static const uint32_t bones[6] = { XSDK_AVATAR_BONE_HIP_LEFT, XSDK_AVATAR_BONE_HIP_RIGHT, XSDK_AVATAR_BONE_KNEE_LEFT,
		XSDK_AVATAR_BONE_KNEE_RIGHT, XSDK_AVATAR_BONE_ANKLE_LEFT, XSDK_AVATAR_BONE_ANKLE_RIGHT };
	*hip = *hinge = 0.0f;
	for (uint32_t b = 0; b < 6; b++) {
		float *most = b < 2 ? hip : hinge;
		for (uint32_t f = 0; f < animation->frame_count; f++) {
			const float *q = animation->poses[(size_t)f * animation->bone_count + bones[b]].rotation;
			float angle = fabsf(2.0f * atan2f(q[1], q[3])) * 57.29578f;
			angle = angle > 180.0f ? 360.0f - angle : angle;
			*most = angle > *most ? angle : *most;
		}
	}
}

// The limits: a hip may turn 15 degrees, a knee or an ankle 5.
static const float hip_twist_most = 15.0f, hinge_twist_most = 5.0f;

static uint32_t lowest_point(const float *p, uint32_t n)
{
	uint32_t low = 0;
	for (uint32_t i = 1; i < n; i++)
		if (p[3 * i + 1] < p[3 * low + 1])
			low = i;
	return low;
}

static int check_clip(
	const XsdkAvatarPack *pack, const char *name, const void *bytes, uint32_t size, uint32_t min_down)
{
	uint32_t strb_size;
	void *strb = read_record(pack, name, &strb_size);
	XsdkAvatarModel body;
	XsdkAvatarSkeleton skeleton;
	if (!strb || xsdk_avatar_model_decode(&body, strb, strb_size) != XSDK_AVATAR_OK ||
		xsdk_avatar_skeleton_build(&skeleton, &body) != XSDK_AVATAR_OK) {
		printf("clip: %s can't be read\n", name);
		return 1;
	}
	free(strb);
	XsdkAvatarAnimation walk;
	if (xsdk_avatar_animation_load(&walk, bytes, size, &skeleton) != XSDK_AVATAR_OK) {
		printf("clip: doesn't load\n");
		return 1;
	}
	static XsdkAvatarBonePose pose[XSDK_AVATAR_BONES];
	static float m[XSDK_AVATAR_BONES][12];
	static const uint32_t ankles[2] = { XSDK_AVATAR_BONE_ANKLE_LEFT, XSDK_AVATAR_BONE_ANKLE_RIGHT };
	static const uint32_t toes[2] = { XSDK_AVATAR_BONE_TOE_LEFT, XSDK_AVATAR_BONE_TOE_RIGHT };
	uint32_t n[2];
	float *points[2], *before[2], floor_at[2], width_rest[2];
	xsdk_avatar_pose_reset(pose, XSDK_AVATAR_BONES);
	xsdk_avatar_skin_matrices(&skeleton, pose, m);
	for (uint32_t s = 0; s < 2; s++) {
		foot_points(&body, (const float (*)[12])m, ankles[s], toes[s], NULL, &n[s]);
		points[s] = malloc(sizeof(float) * 3 * n[s]);
		before[s] = malloc(sizeof(float) * 3 * n[s]);
		foot_points(&body, (const float (*)[12])m, ankles[s], toes[s], points[s], &n[s]);
		floor_at[s] = points[s][3 * lowest_point(points[s], n[s]) + 1];
		float x = 0;
		for (uint32_t i = 0; i < n[s]; i++)
			x += points[s][3 * i] / (float)n[s];
		width_rest[s] = fabsf(x);
	}
	// Each frame of the clip as it is (its own keys), then each foot: how low, and if it was down the frame before,
	// how far the point that touched then has moved along z (the body faces +z).
	float sink = 0.0f, high[2] = { 0, 0 }, moved_sum[2] = { 0, 0 }, moved_min[2] = { 1e9f, 1e9f },
		  moved_max[2] = { -1e9f, -1e9f }, width[2] = { 0, 0 };
	uint32_t moves[2] = { 0, 0 }, touched[2] = { 0, 0 };
	bool down[2] = { false, false };
	const uint32_t frames = walk.frame_count;
	for (uint32_t f = 0; f < frames; f++) {
		xsdk_avatar_animation_sample(&walk, (float)f / walk.frames_per_second, false, pose);
		xsdk_avatar_skin_matrices(&skeleton, pose, m);
		for (uint32_t s = 0; s < 2; s++) {
			float *swap = before[s];
			before[s] = points[s];
			points[s] = swap;
			uint32_t count;
			foot_points(&body, (const float (*)[12])m, ankles[s], toes[s], points[s], &count);
			const uint32_t low = lowest_point(points[s], count);
			const float height = points[s][3 * low + 1] - floor_at[s];
			sink = -height > sink ? -height : sink;
			high[s] = height > high[s] ? height : high[s];
			float x = 0;
			for (uint32_t i = 0; i < count; i++)
				x += points[s][3 * i] / (float)count;
			width[s] += fabsf(x) / (float)frames;
			if (f > 0 && down[s] && fabsf(height) < 0.001f) {
				const float back = before[s][3 * touched[s] + 2] - points[s][3 * touched[s] + 2];
				moved_sum[s] += back;
				moved_min[s] = back < moved_min[s] ? back : moved_min[s];
				moved_max[s] = back > moved_max[s] ? back : moved_max[s];
				moves[s]++;
			}
			down[s] = fabsf(height) < 0.001f;
			touched[s] = low;
		}
	}
	const XsdkAvatarBonePose *first = walk.poses, *last = walk.poses + (size_t)(walk.frame_count - 1) * walk.bone_count;
	bool loops = true;
	for (uint32_t b = 0; b < walk.bone_count; b++)
		for (uint32_t a = 0; a < 4; a++)
			loops = loops && fabsf(first[b].rotation[a] - last[b].rotation[a]) < 1e-4f;
	float speed[2], slide[2];
	for (uint32_t s = 0; s < 2; s++) {
		speed[s] = moves[s] ? moved_sum[s] / (float)moves[s] * walk.frames_per_second : 0.0f;
		slide[s] = moves[s] ? fmaxf(moved_max[s] - moved_sum[s] / (float)moves[s],
								   moved_sum[s] / (float)moves[s] - moved_min[s])
							: 1.0f;
	}
	// The library's measure of the speed, from the joints alone, must agree with the skin's.
	const float ground = xsdk_avatar_animation_ground_speed(&walk, &skeleton);
	printf("clip: %s: xsdk_avatar_animation_ground_speed %.3f m/s\n", name, (double)ground);
	float hip_twist, hinge_twist;
	leg_twist(&walk, &skeleton, &hip_twist, &hinge_twist);
	printf("clip: %s: the hips turn the thighs %.1f degrees at most, knees and ankles twist %.1f\n", name,
		(double)hip_twist, (double)hinge_twist);
	printf("clip: %s: %u frames, %.2f s; down %u and %u frames, the walking speed %.3f and %.3f m/s, the touching point "
		   "off it by %.1f and %.1f mm a frame at most; a foot %.1f mm into the floor at most; highest %.1f and %.1f cm; "
		   "the feet %.3f and %.3f m out (%.3f and %.3f at rest)\n",
		name, (unsigned)frames, (double)xsdk_avatar_animation_length(&walk), (unsigned)moves[0] + 1,
		(unsigned)moves[1] + 1, (double)speed[0], (double)speed[1], (double)(slide[0] * 1000.0f),
		(double)(slide[1] * 1000.0f), (double)(sink * 1000.0f), (double)(high[0] * 100.0f),
		(double)(high[1] * 100.0f), (double)width[0], (double)width[1], (double)width_rest[0], (double)width_rest[1]);
	int failures = 0;
	if (!loops || moves[0] < min_down || moves[1] < min_down || fabsf(speed[0] - speed[1]) > 0.01f ||
		speed[0] < 0.5f || speed[0] > 3.0f || slide[0] > 0.002f || slide[1] > 0.002f || sink > 0.003f || high[0] < 0.03f ||
		high[1] < 0.03f || fabsf(width[0] - width_rest[0]) > 0.015f || fabsf(width[1] - width_rest[1]) > 0.015f ||
		fabsf(ground - 0.5f * (speed[0] + speed[1])) > 0.015f * speed[0] || hip_twist > hip_twist_most ||
		hinge_twist > hinge_twist_most) {
		printf("clip: %s: FAIL\n", name);
		failures++;
	}
	for (uint32_t s = 0; s < 2; s++) {
		free(points[s]);
		free(before[s]);
	}
	xsdk_avatar_animation_free(&walk);
	xsdk_avatar_model_free(&body);
	return failures;
}

// The library's jump (xsdk_avatar_animation_load_jump), on each body, by the skinned skin of the feet as for the walk:
// its first and last frames are the same pose (to fade from and back to an idle); no foot goes into the floor by more
// than 3 mm; a foot down two frames running doesn't slide (the point that touched moves 3 mm at most: it may roll);
// both feet are down at the start and the end, and both in the air together (3 cm up or more) for 0.3 s or more,
// 15 cm up at least at the highest; the legs don't twist (5 degrees at most).
static int check_jump(const XsdkAvatarPack *pack, const char *name, uint32_t gender)
{
	uint32_t strb_size;
	void *strb = read_record(pack, name, &strb_size);
	XsdkAvatarModel body;
	XsdkAvatarSkeleton skeleton;
	if (!strb || xsdk_avatar_model_decode(&body, strb, strb_size) != XSDK_AVATAR_OK ||
		xsdk_avatar_skeleton_build(&skeleton, &body) != XSDK_AVATAR_OK) {
		printf("jump: %s can't be read\n", name);
		return 1;
	}
	free(strb);
	XsdkAvatarAnimation jump;
	if (xsdk_avatar_animation_load_jump(&jump, gender, &skeleton) != XSDK_AVATAR_OK) {
		printf("jump: doesn't load\n");
		return 1;
	}
	static XsdkAvatarBonePose pose[XSDK_AVATAR_BONES];
	static float m[XSDK_AVATAR_BONES][12];
	static const uint32_t ankles[2] = { XSDK_AVATAR_BONE_ANKLE_LEFT, XSDK_AVATAR_BONE_ANKLE_RIGHT };
	static const uint32_t toes[2] = { XSDK_AVATAR_BONE_TOE_LEFT, XSDK_AVATAR_BONE_TOE_RIGHT };
	uint32_t n[2];
	float *points[2], *before[2], floor_at[2];
	xsdk_avatar_pose_reset(pose, XSDK_AVATAR_BONES);
	xsdk_avatar_skin_matrices(&skeleton, pose, m);
	for (uint32_t s = 0; s < 2; s++) {
		foot_points(&body, (const float (*)[12])m, ankles[s], toes[s], NULL, &n[s]);
		points[s] = malloc(sizeof(float) * 3 * n[s]);
		before[s] = malloc(sizeof(float) * 3 * n[s]);
		foot_points(&body, (const float (*)[12])m, ankles[s], toes[s], points[s], &n[s]);
		floor_at[s] = points[s][3 * lowest_point(points[s], n[s]) + 1];
	}
	float sink = 0.0f, slide = 0.0f, highest = 0.0f;
	uint32_t touched[2] = { 0, 0 }, air = 0, air_most = 0;
	bool down[2] = { false, false }, start_down = false, end_down = false;
	const uint32_t frames = jump.frame_count;
	for (uint32_t f = 0; f < frames; f++) {
		xsdk_avatar_animation_sample(&jump, (float)f / jump.frames_per_second, false, pose);
		xsdk_avatar_skin_matrices(&skeleton, pose, m);
		float lowest_of_both = 1e9f;
		for (uint32_t s = 0; s < 2; s++) {
			float *swap = before[s];
			before[s] = points[s];
			points[s] = swap;
			uint32_t count;
			foot_points(&body, (const float (*)[12])m, ankles[s], toes[s], points[s], &count);
			const uint32_t low = lowest_point(points[s], count);
			const float height = points[s][3 * low + 1] - floor_at[s];
			sink = -height > sink ? -height : sink;
			lowest_of_both = height < lowest_of_both ? height : lowest_of_both;
			const bool now = height < 0.001f;
			if (f > 0 && down[s] && now) {
				const float dx = points[s][3 * touched[s]] - before[s][3 * touched[s]],
							dz = points[s][3 * touched[s] + 2] - before[s][3 * touched[s] + 2];
				const float moved = sqrtf(dx * dx + dz * dz);
				slide = moved > slide ? moved : slide;
			}
			down[s] = now;
			touched[s] = low;
		}
		if (f == 0)
			start_down = down[0] && down[1];
		if (f == frames - 1)
			end_down = down[0] && down[1];
		air = lowest_of_both >= 0.03f ? air + 1 : 0;
		air_most = air > air_most ? air : air_most;
		highest = lowest_of_both > highest ? lowest_of_both : highest;
	}
	const XsdkAvatarBonePose *first = jump.poses, *last = jump.poses + (size_t)(frames - 1) * jump.bone_count;
	float differ = 0.0f;
	for (uint32_t b = 0; b < jump.bone_count; b++)
		for (uint32_t a = 0; a < 4; a++)
			differ = fmaxf(differ, fabsf(first[b].rotation[a] - last[b].rotation[a]));
	for (uint32_t a = 0; a < 3; a++)
		differ = fmaxf(differ, fabsf(first[0].translation[a] - last[0].translation[a]));
	float hip_twist, hinge_twist;
	leg_twist(&jump, &skeleton, &hip_twist, &hinge_twist);
	const float air_seconds = (float)air_most / jump.frames_per_second;
	printf("jump: %s: %u frames, %.2f s; first and last frames %s; a foot %.1f mm into the floor at most; a foot down "
		   "moves %.1f mm a frame at most; both feet in the air (3 cm up) %.2f s, up to %.1f cm; the hips turn the "
		   "thighs %.1f degrees at most, knees and ankles twist %.1f\n",
		name, (unsigned)frames, (double)xsdk_avatar_animation_length(&jump), differ < 1e-4f ? "the same" : "differ",
		(double)(sink * 1000.0f), (double)(slide * 1000.0f), (double)air_seconds, (double)(highest * 100.0f),
		(double)hip_twist, (double)hinge_twist);
	int failures = 0;
	if (differ >= 1e-4f || sink > 0.003f || slide > 0.003f || !start_down || !end_down || air_seconds < 0.3f ||
		highest < 0.15f || hip_twist > hip_twist_most || hinge_twist > hinge_twist_most) {
		printf("jump: %s: FAIL\n", name);
		failures++;
	}
	for (uint32_t s = 0; s < 2; s++) {
		free(points[s]);
		free(before[s]);
	}
	xsdk_avatar_animation_free(&jump);
	xsdk_avatar_model_free(&body);
	return failures;
}


static int report(const char *what, int failures)
{
	if (failures)
		return 1;
	printf("%s: all passed\n", what);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2 || (argc > 2 && strcmp(argv[2], "walk") != 0 && strcmp(argv[2], "run") != 0 &&
						strcmp(argv[2], "jump") != 0 && !(strcmp(argv[2], "clip") == 0 && argc > 3))) {
		fprintf(stderr, "usage: walk_check <FFFE07DF00000002> [walk | run | jump | clip <walk.xaan> [<female walk.xaan>]]\n");
		return 2;
	}
	FILE *file = fopen(argv[1], "rb");
	XsdkAvatarPack *pack;
	if (!file || xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK)
		return 2;
	const char *mode = argc > 2 ? argv[2] : "all";
	int failures = 0;
	if (strcmp(mode, "clip") == 0) {
		// One clip for both bodies, or the male's and then the female's.
		uint32_t male_size, female_size;
		void *male = read_whole(argv[3], &male_size);
		void *female = read_whole(argc > 4 ? argv[4] : argv[3], &female_size);
		if (!male || !female)
			return 2;
		failures += check_clip(pack, "MaleBody", male, male_size, 10) +
			check_clip(pack, "FemaleBody", female, female_size, 10);
		free(male);
		free(female);
	}
	// The walk: a foot is down 19 frames of 31; the run: 8 of 21, so 6 moves at least.
	if (strcmp(mode, "walk") == 0 || strcmp(mode, "all") == 0)
		failures += check_clip(pack, "MaleBody", clip_walk_male, clip_walk_male_size, 10) +
			check_clip(pack, "FemaleBody", clip_walk_female, clip_walk_female_size, 10);
	if (strcmp(mode, "run") == 0 || strcmp(mode, "all") == 0)
		failures += check_clip(pack, "MaleBody", clip_run_male, clip_run_male_size, 6) +
			check_clip(pack, "FemaleBody", clip_run_female, clip_run_female_size, 6);
	if (strcmp(mode, "jump") == 0 || strcmp(mode, "all") == 0)
		failures += check_jump(pack, "MaleBody", XSDK_AVATAR_MALE) + check_jump(pack, "FemaleBody", XSDK_AVATAR_FEMALE);
	xsdk_avatar_pack_close(pack);
	fclose(file);
	return report(mode, failures);
}
