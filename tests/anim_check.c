// PC test of the animation pipeline (tools/anim_convert.py, xsdk_avatar_animation_load): an animation converted from
// a glTF export that holds a stock one (360sona's "Hooray!" is "Animation Generic Celebration", FORMATS.md, section
// 4.7) must play as the stock one does, decoded from the console's own pack (section 4.5).
//   anim_check <FFFE07DF00000002> <converted.xaan> "<stock animation name>"
// Compares every bone's rotation and the root's move at 200 times through it, on the male body's skeleton. Exits 1
// if a rotation differs by more than 0.5 degrees or the root by more than 1 cm.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: anim_check <FFFE07DF00000002> <converted.xaan> \"<stock animation name>\"\n");
		return 2;
	}
	FILE *file = fopen(argv[1], "rb");
	XsdkAvatarPack *pack;
	if (!file || xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK)
		return 2;
	uint32_t size;
	void *strb = read_record(pack, "MaleBody", &size);
	XsdkAvatarModel body;
	XsdkAvatarSkeleton skeleton;
	if (!strb || xsdk_avatar_model_decode(&body, strb, size) != XSDK_AVATAR_OK ||
		xsdk_avatar_skeleton_build(&skeleton, &body) != XSDK_AVATAR_OK)
		return 2;
	free(strb);
	strb = read_record(pack, argv[3], &size);
	XsdkAvatarAnimation stock, converted;
	if (!strb || xsdk_avatar_animation_decode(&stock, strb, size) != XSDK_AVATAR_OK) {
		printf("anim: no stock animation %s\n", argv[3]);
		return 2;
	}
	free(strb);
	void *bytes = read_whole(argv[2], &size);
	// 360sona put the stock root moves on its rig unchanged, as if its root pivot were ours (FORMATS.md, section 4.8): its
	// stock animations compare as they are, without moving them onto this skeleton's pivot (NULL).
	const int loaded = bytes ? xsdk_avatar_animation_load(&converted, bytes, size, NULL) : -1;
	XsdkAvatarAnimation rebased;
	const int moved = bytes ? xsdk_avatar_animation_load(&rebased, bytes, size, &skeleton) : -1;
	free(bytes);
	if (loaded != XSDK_AVATAR_OK) {
		printf("anim: %s doesn't load (%d)\n", argv[2], loaded);
		return 1;
	}
	const float length = xsdk_avatar_animation_length(&stock);
	printf("anim: stock %u frames at %.3f a second (%.3f s), converted %u at %.3f (%.3f s)\n",
		(unsigned)stock.frame_count, (double)stock.frames_per_second, (double)length, (unsigned)converted.frame_count,
		(double)converted.frames_per_second, (double)xsdk_avatar_animation_length(&converted));
	static XsdkAvatarBonePose a[XSDK_AVATAR_BONES], b[XSDK_AVATAR_BONES];
	float worst_angle = 0.0f, worst_move = 0.0f, worst_rebased = 0.0f;
	uint32_t worst_bone = 0;
	for (uint32_t i = 0; i <= 200; i++) {
		const float t = length * (float)i / 200.0f;
		xsdk_avatar_animation_sample(&stock, t, false, a);
		xsdk_avatar_animation_sample(&converted, t, false, b);
		for (uint32_t bone = 0; bone < XSDK_AVATAR_BONES; bone++) {
			float d = 0.0f;
			for (uint32_t k = 0; k < 4; k++)
				d += a[bone].rotation[k] * b[bone].rotation[k];
			const float angle = 2.0f * acosf(fminf(1.0f, fabsf(d))) * 57.29578f;
			if (angle > worst_angle) {
				worst_angle = angle;
				worst_bone = bone;
			}
		}
		const float *ta = a[XSDK_AVATAR_BONE_ROOT].translation, *tb = b[XSDK_AVATAR_BONE_ROOT].translation;
		const float move = sqrtf((ta[0] - tb[0]) * (ta[0] - tb[0]) + (ta[1] - tb[1]) * (ta[1] - tb[1]) +
			(ta[2] - tb[2]) * (ta[2] - tb[2]));
		worst_move = move > worst_move ? move : worst_move;
		if (moved == XSDK_AVATAR_OK) {
			static XsdkAvatarBonePose c[XSDK_AVATAR_BONES];
			xsdk_avatar_animation_sample(&rebased, t, false, c);
			const float *tc = c[XSDK_AVATAR_BONE_ROOT].translation;
			const float m2 = sqrtf((ta[0] - tc[0]) * (ta[0] - tc[0]) + (ta[1] - tc[1]) * (ta[1] - tc[1]) +
				(ta[2] - tc[2]) * (ta[2] - tc[2]));
			worst_rebased = m2 > worst_rebased ? m2 : worst_rebased;
		}
	}
	printf("anim: rotations differ by %.3f degrees at most (bone %u), the root by %.4f m\n", (double)worst_angle,
		(unsigned)worst_bone, (double)worst_move);
	xsdk_avatar_animation_free(&stock);
	xsdk_avatar_animation_free(&converted);
	xsdk_avatar_model_free(&body);
	xsdk_avatar_pack_close(pack);
	fclose(file);
	if (worst_angle > 0.5f || worst_move > 0.01f) {
		printf("anim: FAIL\n");
		return 1;
	}
	printf("anim: all passed\n");
	return 0;
}
