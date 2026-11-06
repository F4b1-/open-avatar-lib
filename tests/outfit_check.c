// PC test of putting an avatar together (src/avatar_outfit.c, the avatar layer's loading half): the console's file
// reads (avatar_content.c) replaced by files on the PC, the rest as the console runs it.
//   outfit_check <FFFE07DF00000002> <description> [<folder of item packages> ...]
// Prints each part (its stock record or "package", meshes, what its shape hides and moves) and what the body's draw
// keeps, then the stand-ins and items left out. Exits 1 if an item package the description names and a folder holds
// didn't load as a package, or if a package's shape block went unread: the bug of test run 5 (2026-11-06).

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_runtime.h"

static FILE *pack_file;
static XsdkAvatarPack *pack;
static const char *const *folders;
static int folder_count;

void xsdk_log_write(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	printf("  log: ");
	vprintf(format, args);
	printf("\n");
	va_end(args);
}

static int read_file(void *context, uint64_t offset, void *buffer, uint32_t length)
{
	FILE *file = context;
	if (fseek(file, (long)offset, SEEK_SET) != 0)
		return 1;
	return fread(buffer, 1, length, file) == length ? 0 : 1;
}

int avatar_content_pack(const XsdkAvatarPack **out)
{
	*out = pack;
	return pack ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_READ;
}

void *avatar_content_read_stock(uint32_t record, uint32_t *size_bytes)
{
	*size_bytes = xsdk_avatar_pack_asset_size(pack, record);
	void *strb = *size_bytes ? malloc(*size_bytes) : NULL;
	if (strb && xsdk_avatar_pack_read_asset(pack, record, strb) == XSDK_AVATAR_OK)
		return strb;
	free(strb);
	return NULL;
}

void *avatar_content_read_item(const uint8_t *id, uint32_t *size_bytes)
{
	char name[33];
	uint32_t title;
	xsdk_avatar_id_package_name(id, name, &title);
	for (int f = 0; f < folder_count; f++) {
		char path[1024];
		snprintf(path, sizeof path, "%s/%s", folders[f], name);
		FILE *file = fopen(path, "rb");
		if (!file)
			continue;
		void *strb = NULL;
		const int result = xsdk_avatar_package_read_file(read_file, file, "asset_v2.bin", &strb, size_bytes);
		fclose(file);
		return result == XSDK_AVATAR_OK ? strb : NULL;
	}
	return NULL;
}

int avatar_content_read_profile(uint32_t user_index, uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES])
{
	(void)user_index;
	(void)bytes;
	return XSDK_AVATAR_ERROR_UNSUPPORTED;
}

void avatar_content_close(void)
{
}

// The triangles of part q that the outfit's shapes hide, as avatar_render.c counts them.
static uint32_t hidden_triangles(const AvatarOutfit *outfit, const AvatarPart *part, uint32_t *triangles)
{
	*triangles = 0;
	for (uint32_t m = 0; m < part->model.mesh_count; m++)
		*triangles += part->model.meshes[m].index_count / 3;
	if (part->record == UINT32_MAX)
		return 0;
	uint8_t *hidden = calloc(*triangles ? *triangles : 1, 1);
	uint32_t count = 0;
	for (uint32_t p = 0; hidden && p < outfit->part_count; p++) {
		const XsdkAvatarShape *shape = &outfit->parts[p].shape;
		if (shape->target_record != part->record || shape->target_index_bytes != *triangles * 6)
			continue;
		for (uint32_t k = 0; k < shape->hidden_count; k++)
			if (shape->hidden[k] < *triangles && !hidden[shape->hidden[k]]) {
				hidden[shape->hidden[k]] = 1;
				count++;
			}
	}
	free(hidden);
	return count;
}

static bool in_a_folder(const uint8_t *id)
{
	uint32_t size;
	void *strb = avatar_content_read_item(id, &size);
	free(strb);
	return strb != NULL;
}

// An avatar of stock parts with face features (xsdk_avatar_load_stock, as samples/store's NPCs): the features become
// the description's, their colours its colours, and the Head gets its face layers.
static int check_stock(XsdkAvatarDescription *d, AvatarOutfit *outfit)
{
	static const XsdkAvatarStockPart parts[] = { { "MaleBody", 0xFFC68642 }, { "Head", 0xFFC68642 },
		{ "Buzz Cut", 0xFF2A1B10 }, { "Male Punk Tee", 0xFF303030 }, { "Jeans", 0xFF3A4E7A },
		{ "High Top Sneakers", 0xFF202020 }, { "Happy Eyes", 0xFF4A3020 }, { "Studious Eyebrows", 0xFF2A1B10 },
		{ "Grin Mouth", 0xFFB56157 }, { "Jaw Stubble", 0xFF2A1B10 } };
	int failures = 0;
	const int described = avatar_outfit_describe_stock(d, parts, sizeof parts / sizeof parts[0]);
	printf("stock: %d, %u items, %u features, eyes %08X, mouth %08X, facial hair %08X\n", described,
		(unsigned)d->item_count, (unsigned)d->feature_count, (unsigned)d->colors[XSDK_AVATAR_COLOR_EYES],
		(unsigned)d->colors[XSDK_AVATAR_COLOR_MOUTH], (unsigned)d->colors[XSDK_AVATAR_COLOR_FACIAL_HAIR]);
	if (described != XSDK_AVATAR_OK || d->item_count != 6 || d->feature_count != 4 ||
		d->colors[XSDK_AVATAR_COLOR_EYES] != 0xFF4A3020 || d->colors[XSDK_AVATAR_COLOR_FACIAL_HAIR] != 0xFF2A1B10)
		failures++;
	const int loaded = avatar_outfit_load(outfit, d);
	uint32_t layers = 0;
	for (uint32_t p = 0; p < outfit->part_count; p++)
		for (uint32_t s = 0; s < AVATAR_HEAD_LAYERS; s++)
			layers += outfit->parts[p].is_head && outfit->parts[p].layers[s].data;
	printf("stock: %d, %u parts, %u face layers\n", loaded, (unsigned)outfit->part_count, (unsigned)layers);
	if (loaded != XSDK_AVATAR_OK || layers != 4)
		failures++;
	avatar_outfit_free(outfit);
	printf(failures ? "stock: FAIL\n" : "stock: all passed\n");
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: outfit_check <FFFE07DF00000002> <description> [<package folder> ...]\n");
		return 2;
	}
	pack_file = fopen(argv[1], "rb");
	if (!pack_file || xsdk_avatar_pack_open(&pack, read_file, pack_file) != XSDK_AVATAR_OK)
		return 2;
	folders = (const char *const *)argv + 3;
	folder_count = argc - 3;
	XsdkAvatarDescription d;
	static AvatarOutfit outfit;
	if (strcmp(argv[2], "stock") == 0)
		return check_stock(&d, &outfit);
	static uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
	FILE *file = fopen(argv[2], "rb");
	if (!file || fread(bytes, 1, sizeof bytes, file) != sizeof bytes)
		return 2;
	fclose(file);
	if (xsdk_avatar_description_parse(&d, bytes, sizeof bytes) != XSDK_AVATAR_OK)
		return 2;
	const int result = avatar_outfit_load(&outfit, &d);
	printf("outfit: %d, %u parts, gender %u, %u replaced, %u left out, skeleton %u bones\n", result,
		(unsigned)outfit.part_count, (unsigned)outfit.gender, (unsigned)outfit.replaced_count,
		(unsigned)outfit.left_out_count, (unsigned)outfit.skeleton.bone_count);
	int failures = 0;
	for (uint32_t p = 0; p < outfit.part_count; p++) {
		const AvatarPart *part = &outfit.parts[p];
		uint32_t triangles;
		const uint32_t hidden = hidden_triangles(&outfit, part, &triangles);
		char record[16];
		snprintf(record, sizeof record, part->record == UINT32_MAX ? "package" : "%u", (unsigned)part->record);
		printf("%-34s %-8s %u meshes, %u triangles, %u hidden by others; its shape: hides %u, moves %u, of %u\n",
			part->label, record, (unsigned)part->model.mesh_count, (unsigned)triangles, (unsigned)hidden,
			(unsigned)part->shape.hidden_count, (unsigned)part->shape.override_count,
			(unsigned)part->shape.target_record);
	}
	// Every item the folders hold must have loaded as a package, its shape block read if it has one.
	for (uint32_t i = 0; i < d.item_count; i++) {
		uint32_t record;
		if (xsdk_avatar_id_is_stock(d.items[i].id, &record) || !in_a_folder(d.items[i].id) ||
			(d.items[i].slots & XSDK_AVATAR_SLOT_PROP))
			continue;
		char name[33];
		uint32_t title;
		xsdk_avatar_id_package_name(d.items[i].id, name, &title);
		uint32_t size;
		void *strb = avatar_content_read_item(d.items[i].id, &size);
		XsdkAvatarShape shape;
		const bool has_shape = strb && xsdk_avatar_shape_decode(&shape, strb, size) == XSDK_AVATAR_OK;
		free(strb);
		const AvatarPart *part = NULL;
		for (uint32_t p = 0; p < outfit.part_count; p++)
			if (strcmp(outfit.parts[p].label, name) == 0)
				part = &outfit.parts[p];
		if (!part || part->record != UINT32_MAX) {
			printf("FAIL: %s didn't load as a package\n", name);
			failures++;
		} else if (has_shape && part->shape.hidden_count != shape.hidden_count) {
			printf("FAIL: %s: its shape block hides %u triangles, the part %u\n", name, (unsigned)shape.hidden_count,
				(unsigned)part->shape.hidden_count);
			failures++;
		}
		if (has_shape)
			xsdk_avatar_shape_free(&shape);
	}
	avatar_outfit_free(&outfit);
	xsdk_avatar_pack_close(pack);
	fclose(pack_file);
	if (failures) {
		printf("outfit: %d failed\n", failures);
		return 1;
	}
	printf("outfit: all passed\n");
	return 0;
}
