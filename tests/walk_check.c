// PC test of the walk made in code (src/avatar_walk.c), on both bodies of the console's stock pack, over "Animation
// Generic Stand 0":
//   walk_check <FFFE07DF00000002>              checks: it loops; in every frame the lower foot stays within 1.5 cm
//                                              of the floor; the feet are apart by about a step, as far out from the
//                                              middle as at rest and either side alike (on the console one leg had
//                                              stood out); a standing foot doesn't slide (1.5 cm at most)
//   walk_check <FFFE07DF00000002> obj <prefix> <time>,...   also writes the male body posed at those times as
//                                              <prefix>_<n>.obj (Wavefront, for a picture; keep it off the repo)
// Exits 1 if a check fails.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsdk/avatar.h"

enum {
	FRAMES_CHECKED = 64,
};

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

static void write_obj(const char *path, const XsdkAvatarModel *body, const float (*m)[12])
{
	FILE *out = fopen(path, "w");
	if (!out)
		return;
	uint32_t base = 1;
	for (uint32_t k = 0; k < body->mesh_count; k++) {
		const XsdkAvatarMesh *mesh = &body->meshes[k];
		for (uint32_t v = 0; v < mesh->vertex_count; v++) {
			float o[3] = { 0, 0, 0 };
			for (uint32_t j = 0; j < 4; j++) {
				float p[3];
				posed(m[mesh->bones[4 * v + j]], mesh->positions + 3 * v, p);
				for (uint32_t a = 0; a < 3; a++)
					o[a] += (float)mesh->weights[4 * v + j] / 255.0f * p[a];
			}
			fprintf(out, "v %f %f %f\n", o[0], o[1], o[2]);
		}
		for (uint32_t t = 0; t < mesh->index_count; t += 3)
			fprintf(out, "f %u %u %u\n", (unsigned)(base + mesh->indices[t]), (unsigned)(base + mesh->indices[t + 1]),
				(unsigned)(base + mesh->indices[t + 2]));
		base += mesh->vertex_count;
	}
	fclose(out);
}

static int check_body(const XsdkAvatarPack *pack, const char *name, const XsdkAvatarBonePose *stand, int argc,
	char **argv)
{
	uint32_t size;
	void *strb = read_record(pack, name, &size);
	XsdkAvatarModel body;
	XsdkAvatarSkeleton skeleton;
	if (!strb || xsdk_avatar_model_decode(&body, strb, size) != XSDK_AVATAR_OK ||
		xsdk_avatar_skeleton_build(&skeleton, &body) != XSDK_AVATAR_OK) {
		printf("walk: %s can't be read\n", name);
		return 1;
	}
	free(strb);
	XsdkAvatarAnimation walk;
	float speed = 0.0f;
	if (xsdk_avatar_animation_walk(&walk, stand, &skeleton, &speed) != XSDK_AVATAR_OK)
		return 1;
	const float length = xsdk_avatar_animation_length(&walk);
	int failures = 0;
	static XsdkAvatarBonePose pose[XSDK_AVATAR_BONES];
	static float m[XSDK_AVATAR_BONES][12];
	// The feet at rest: the floor, and how far apart they stand.
	xsdk_avatar_pose_reset(pose, XSDK_AVATAR_BONES);
	xsdk_avatar_skin_matrices(&skeleton, pose, m);
	float left[3], right[3];
	posed(m[XSDK_AVATAR_BONE_ANKLE_LEFT], skeleton.positions[XSDK_AVATAR_BONE_ANKLE_LEFT], left);
	const float standing = left[1], rest_width = left[0];
	float lowest = 1e9f, highest = -1e9f, widest = 0.0f, middle = 0.0f, width = 0.0f;
	// A standing foot moves back at the walking speed: its z plus speed times the time stays put. Checked while the
	// left foot is flat on the floor (from 0.12 to 0.40 of the cycle; it lands at 0, rolls to its toes from 0.42).
	float slide_low = 1e9f, slide_high = -1e9f;
	for (uint32_t f = 0; f < FRAMES_CHECKED; f++) {
		const float seconds = length * (float)f / FRAMES_CHECKED;
		xsdk_avatar_animation_sample(&walk, seconds, true, pose);
		xsdk_avatar_skin_matrices(&skeleton, pose, m);
		posed(m[XSDK_AVATAR_BONE_ANKLE_LEFT], skeleton.positions[XSDK_AVATAR_BONE_ANKLE_LEFT], left);
		posed(m[XSDK_AVATAR_BONE_ANKLE_RIGHT], skeleton.positions[XSDK_AVATAR_BONE_ANKLE_RIGHT], right);
		const float low = left[1] < right[1] ? left[1] : right[1];
		lowest = low < lowest ? low : lowest;
		highest = low > highest ? low : highest;
		const float apart = fabsf(left[2] - right[2]);
		widest = apart > widest ? apart : widest;
		middle += (left[0] + right[0]) / FRAMES_CHECKED;
		width += (left[0] - right[0]) / 2.0f / FRAMES_CHECKED;
		const float share = seconds / length;
		if (share >= 0.12f && share <= 0.40f) {
			const float still = left[2] + speed * seconds;
			slide_low = still < slide_low ? still : slide_low;
			slide_high = still > slide_high ? still : slide_high;
		}
	}
	const XsdkAvatarBonePose *first = walk.poses, *last = walk.poses + (size_t)(walk.frame_count - 1) * walk.bone_count;
	const bool loops = memcmp(first, last, sizeof(XsdkAvatarBonePose) * walk.bone_count) == 0;
	printf("walk: %s: %u frames, %.2f s, %.2f m/s; the lower ankle %+.3f to %+.3f m from the floor, feet up to %.2f m "
		   "apart; the feet's middle %+.3f m, each %.3f m out (%.3f at rest); a standing foot slides %.3f m\n",
		name, (unsigned)walk.frame_count, (double)length, (double)speed, (double)(lowest - standing),
		(double)(highest - standing), (double)widest, (double)middle, (double)width, (double)rest_width,
		(double)(slide_high - slide_low));
	if (!loops || fabsf(lowest - standing) > 0.015f || fabsf(highest - standing) > 0.015f || widest < 0.3f ||
		speed < 0.5f || speed > 2.0f || fabsf(middle) > 0.01f || fabsf(width - rest_width) > 0.015f ||
		slide_high - slide_low > 0.015f) {
		printf("walk: %s: FAIL\n", name);
		failures++;
	}
	if (argc > 4 && strcmp(argv[2], "obj") == 0 && strcmp(name, "MaleBody") == 0) {
		char *times = argv[4];
		uint32_t n = 0;
		for (char *t = strtok(times, ","); t; t = strtok(NULL, ","), n++) {
			xsdk_avatar_animation_sample(&walk, (float)atof(t), true, pose);
			xsdk_avatar_skin_matrices(&skeleton, pose, m);
			char path[512];
			snprintf(path, sizeof path, "%s_%u.obj", argv[3], (unsigned)n);
			write_obj(path, &body, (const float (*)[12])m);
		}
	}
	xsdk_avatar_animation_free(&walk);
	xsdk_avatar_model_free(&body);
	return failures;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: walk_check <FFFE07DF00000002> [obj <prefix> <time>,...]\n");
		return 2;
	}
	FILE *file = fopen(argv[1], "rb");
	XsdkAvatarPack *pack;
	if (!file || xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK)
		return 2;
	uint32_t size;
	void *strb = read_record(pack, "Animation Generic Stand 0", &size);
	XsdkAvatarAnimation stand_animation;
	if (!strb || xsdk_avatar_animation_decode(&stand_animation, strb, size) != XSDK_AVATAR_OK)
		return 2;
	free(strb);
	static XsdkAvatarBonePose stand[XSDK_AVATAR_BONES];
	xsdk_avatar_animation_sample(&stand_animation, 0.0f, true, stand);
	int failures = check_body(pack, "MaleBody", stand, argc, argv);
	failures += check_body(pack, "FemaleBody", stand, argc, argv);
	xsdk_avatar_animation_free(&stand_animation);
	xsdk_avatar_pack_close(pack);
	fclose(file);
	if (failures)
		return 1;
	printf("walk: all passed\n");
	return 0;
}
