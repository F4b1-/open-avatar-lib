// PC test of the error paths when memory runs out (every allocation through avatar_alloc.c): every load and decode
// of the asset layer, run with its 1st, 2nd, 3rd, ... heap allocation made to fail (avatar_alloc_fail_at), until a
// run makes no allocation fail. Each failed run must return XSDK_AVATAR_ERROR_MEMORY, leave its struct empty, and
// leak and double-free nothing: build.sh pc builds this test with AddressSanitizer, which ends it on either.
//   alloc_check <FFFE07DF00000002> [<item package> ...]
// Prints each operation's allocation count, then "alloc: all passed"; exits 1 on a wrong result.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

static int read_file(void *context, uint64_t offset, void *buffer, uint32_t length)
{
	FILE *file = context;
	if (fseek(file, (long)offset, SEEK_SET) != 0)
		return 1;
	return fread(buffer, 1, length, file) == length ? 0 : 1;
}

static FILE *pack_file;
static XsdkAvatarPack *pack;
static int failures;

// An operation: runs once, frees what it made on success, returns its result.
typedef int (*Operation)(const void *input);

// Runs op with allocation 1, 2, 3, ... failing until it runs without a failure; checks each failed run.
static void check(const char *name, Operation op, const void *input)
{
	for (uint32_t n = 1;; n++) {
		avatar_alloc_fail_at = n;
		const int result = op(input);
		const bool failed = avatar_alloc_fail_at == 0; // the counter reached the failing allocation
		avatar_alloc_fail_at = 0;
		if (!failed) {
			if (result != XSDK_AVATAR_OK) {
				printf("alloc: %s: %d without a failed allocation\n", name, result);
				failures++;
			}
			printf("alloc: %s: %u allocations, each failure handled\n", name, n - 1);
			return;
		}
		if (result != XSDK_AVATAR_ERROR_MEMORY) {
			printf("alloc: %s: allocation %u failed but it returned %d: FAIL\n", name, n, result);
			failures++;
		}
	}
}

// A stock record's STRB file, read without the hook (the test's own allocation).
static void *record(const char *name, uint32_t *size)
{
	uint32_t index;
	if (xsdk_avatar_pack_find(pack, name, &index) != XSDK_AVATAR_OK)
		return NULL;
	*size = xsdk_avatar_pack_asset_size(pack, index);
	void *strb = malloc(*size);
	if (strb && xsdk_avatar_pack_read_asset(pack, index, strb) != XSDK_AVATAR_OK) {
		free(strb);
		return NULL;
	}
	return strb;
}

typedef struct {
	const void *strb;
	uint32_t size;
} Strb;

static bool all_zero(const void *p, size_t n)
{
	const uint8_t *b = p;
	for (size_t i = 0; i < n; i++)
		if (b[i])
			return false;
	return true;
}

// Each failed decode must leave its struct empty: freeing it again is then harmless.
#define EMPTY_ON_ERROR(result, object)                                                                                  \
	do {                                                                                                                \
		if ((result) != XSDK_AVATAR_OK && !all_zero(&(object), sizeof(object))) {                                         \
			printf("alloc: a failed decode left its struct filled: FAIL\n");                                            \
			failures++;                                                                                                 \
		}                                                                                                               \
	} while (0)

static int open_pack(const void *input)
{
	(void)input;
	XsdkAvatarPack *p;
	const int result = xsdk_avatar_pack_open(&p, read_file, pack_file);
	xsdk_avatar_pack_close(p);
	return result;
}

static int decode_model(const void *input)
{
	const Strb *s = input;
	XsdkAvatarModel model;
	const int result = xsdk_avatar_model_decode(&model, s->strb, s->size);
	EMPTY_ON_ERROR(result, model);
	xsdk_avatar_model_free(&model);
	return result;
}

static int decode_texture(const void *input)
{
	const Strb *s = input;
	XsdkAvatarTexture texture;
	const int result = xsdk_avatar_texture_decode(&texture, s->strb, s->size);
	EMPTY_ON_ERROR(result, texture);
	xsdk_avatar_texture_free(&texture);
	return result;
}

static int decode_shape(const void *input)
{
	const Strb *s = input;
	XsdkAvatarShape shape;
	const int result = xsdk_avatar_shape_decode(&shape, s->strb, s->size);
	EMPTY_ON_ERROR(result, shape);
	xsdk_avatar_shape_free(&shape);
	return result;
}

static int decode_animation(const void *input)
{
	const Strb *s = input;
	XsdkAvatarAnimation animation;
	const int result = xsdk_avatar_animation_decode(&animation, s->strb, s->size);
	EMPTY_ON_ERROR(result, animation);
	xsdk_avatar_animation_free(&animation);
	return result;
}

static int build_skeleton(const void *input)
{
	const Strb *s = input;
	XsdkAvatarModel body;
	int result = xsdk_avatar_model_decode(&body, s->strb, s->size);
	XsdkAvatarSkeleton skeleton;
	if (result == XSDK_AVATAR_OK)
		result = xsdk_avatar_skeleton_build(&skeleton, &body);
	xsdk_avatar_model_free(&body);
	return result;
}

// The library's own walk, run and jump, on the male body's skeleton (built beforehand, without the hook).
static XsdkAvatarSkeleton male;

static int load_own(const void *input)
{
	(void)input;
	XsdkAvatarAnimation walk = { 0 }, run = { 0 }, jump = { 0 };
	float speed;
	int result = xsdk_avatar_animation_load_walk(&walk, XSDK_AVATAR_MALE, &male, &speed);
	if (result == XSDK_AVATAR_OK)
		result = xsdk_avatar_animation_load_run(&run, XSDK_AVATAR_MALE, &male, &speed);
	if (result == XSDK_AVATAR_OK)
		result = xsdk_avatar_animation_load_jump(&jump, XSDK_AVATAR_MALE, &male);
	xsdk_avatar_animation_free(&walk);
	xsdk_avatar_animation_free(&run);
	xsdk_avatar_animation_free(&jump);
	return result;
}

// Locomotion (avatar_locomotion.c): its walk, run and jump; a run or jump that runs out of memory fails it too.
static int load_locomotion(const void *input)
{
	(void)input;
	XsdkAvatarLocomotion locomotion;
	const int result = xsdk_avatar_locomotion_init(&locomotion, XSDK_AVATAR_MALE, &male, NULL);
	if (result == XSDK_AVATAR_OK)
		xsdk_avatar_locomotion_free(&locomotion);
	return result;
}

static int read_item(const void *input)
{
	FILE *file = fopen(input, "rb");
	if (!file)
		return XSDK_AVATAR_ERROR_READ;
	void *data;
	uint32_t size;
	int result = xsdk_avatar_package_read_file(read_file, file, "asset_v2.bin", &data, &size);
	fclose(file);
	if (result != XSDK_AVATAR_OK)
		return result;
	XsdkAvatarModel model;
	result = xsdk_avatar_model_decode(&model, data, size);
	xsdk_avatar_model_free(&model);
	free(data);
	return result;
}

static void check_record(const char *what, Operation op, const char *name)
{
	Strb s;
	void *strb = record(name, &s.size);
	if (!strb) {
		printf("alloc: no stock record %s\n", name);
		failures++;
		return;
	}
	s.strb = strb;
	char label[128];
	snprintf(label, sizeof label, "%s \"%s\"", what, name);
	check(label, op, &s);
	free(strb);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: alloc_check <FFFE07DF00000002> [<item package> ...]\n");
		return 2;
	}
	pack_file = fopen(argv[1], "rb");
	if (!pack_file || xsdk_avatar_pack_open(&pack, read_file, pack_file) != XSDK_AVATAR_OK)
		return 2;
	check("open the stock pack", open_pack, NULL);
	check_record("decode the model", decode_model, "MaleBody");
	check_record("decode the model", decode_model, "Head");
	check_record("decode the model", decode_model, "Male Power Tee");
	check_record("decode the model", decode_model, "Bald");
	check_record("decode the texture", decode_texture, "Happy Eyes");
	// The first body template (the shapes clothes hide the body with) the pack has.
	for (uint32_t i = 0; i < xsdk_avatar_pack_count(pack); i++)
		if (xsdk_avatar_pack_mask(pack, i) & XSDK_AVATAR_SLOT_TEMPLATE) {
			check_record("decode the shape", decode_shape, xsdk_avatar_pack_name(pack, i));
			break;
		}
	check_record("decode the animation", decode_animation, "Animation Generic Wave");
	check_record("build the skeleton of", build_skeleton, "FemaleBody");
	uint32_t size;
	void *strb = record("MaleBody", &size);
	XsdkAvatarModel body;
	if (!strb || xsdk_avatar_model_decode(&body, strb, size) != XSDK_AVATAR_OK ||
		xsdk_avatar_skeleton_build(&male, &body) != XSDK_AVATAR_OK)
		return 2;
	xsdk_avatar_model_free(&body);
	free(strb);
	check("load the walk, run and jump", load_own, NULL);
	check("load the locomotion", load_locomotion, NULL);
	for (int i = 2; i < argc; i++) {
		char label[512];
		snprintf(label, sizeof label, "read the item package %s", argv[i]);
		check(label, read_item, argv[i]);
	}
	xsdk_avatar_pack_close(pack);
	fclose(pack_file);
	if (failures)
		return 1;
	printf("alloc: all passed\n");
	return 0;
}
