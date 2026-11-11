// PC test of putting an avatar together (src/avatar_outfit.c, the avatar layer's loading half): the console's file
// reads (avatar_content.c) replaced by files on the PC, the rest as the console runs it.
//   outfit_check <FFFE07DF00000002> <description> [<folder of item packages> ...]
// Prints each part (its stock record or "package", meshes, what its shape hides and moves) and what the body's draw
// keeps, then the stand-ins and items left out. Exits 1 if an item package the description names and a folder holds
// didn't load as a package, or if a package's shape block went unread: the bug of test run 5 (2026-11-06).
//   outfit_check <FFFE07DF00000002> alloc <description> [<package folder> ...]
// The outfit load and the rig export with their 1st, 2nd, ... allocation failing, as tests/alloc_check does for the
// asset layer: each must return XSDK_AVATAR_ERROR_MEMORY with nothing left over (build/pc/outfit_alloc_check, the
// same test built with AddressSanitizer, runs it).
//   outfit_check <FFFE07DF00000002> stock | hat
// A stock avatar with face features; or a stock hair with and without a hat, swapped for its hat variant under one.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "avatar_runtime.h"

static FILE *pack_file;
static XsdkAvatarPack *pack;
static const char *const *folders;
static int folder_count;

static bool quiet; // the alloc mode's hundreds of runs: no log

void xsdk_log_write(const char *format, ...)
{
	if (quiet)
		return;
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

int avatar_content_read_stock(uint32_t record, void **strb, uint32_t *size_bytes)
{
	*strb = NULL;
	*size_bytes = xsdk_avatar_pack_asset_size(pack, record);
	if (!*size_bytes)
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	void *data = avatar_malloc(*size_bytes, "a stock asset"); // through the hook, as the console's
	if (!data)
		return XSDK_AVATAR_ERROR_MEMORY;
	const int result = xsdk_avatar_pack_read_asset(pack, record, data);
	if (result != XSDK_AVATAR_OK) {
		free(data);
		return result;
	}
	*strb = data;
	return XSDK_AVATAR_OK;
}

int avatar_content_read_item(const uint8_t *id, void **strb, uint32_t *size_bytes)
{
	*strb = NULL;
	char name[33];
	uint32_t title;
	xsdk_avatar_id_package_name(id, name, &title);
	for (int f = 0; f < folder_count; f++) {
		char path[1024];
		snprintf(path, sizeof path, "%s/%s", folders[f], name);
		FILE *file = fopen(path, "rb");
		if (!file)
			continue;
		const int result = xsdk_avatar_package_read_file(read_file, file, "asset_v2.bin", strb, size_bytes);
		fclose(file);
		return result;
	}
	return XSDK_AVATAR_ERROR_NOT_FOUND;
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
	void *strb;
	const int result = avatar_content_read_item(id, &strb, &size);
	free(strb);
	return result == XSDK_AVATAR_OK;
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

// The hair a stock avatar ends up with, by name; "" if none.
static const char *hair_of(XsdkAvatarDescription *d, AvatarOutfit *outfit, const XsdkAvatarStockPart *parts,
	uint32_t count, int *result)
{
	static char label[48];
	label[0] = 0;
	*result = avatar_outfit_describe_stock(d, parts, count);
	if (*result == XSDK_AVATAR_OK)
		*result = avatar_outfit_load(outfit, d);
	for (uint32_t p = 0; p < outfit->part_count; p++)
		if (outfit->parts[p].slots & XSDK_AVATAR_SLOT_HAIR)
			snprintf(label, sizeof label, "%s", outfit->parts[p].label);
	avatar_outfit_free(outfit);
	return label;
}

// Hair under a hat (FORMATS.md, section 3.7; test run 9): with a hat on, the hair is its hat variant, which the pack
// record links to ("Raised Front" -> "Quiff (Hat)"); without one, the hair itself.
static int check_hat(XsdkAvatarDescription *d, AvatarOutfit *outfit)
{
	XsdkAvatarStockPart parts[] = { { "MaleBody", 0xFFC68642 }, { "Head", 0xFFC68642 },
		{ "Raised Front", 0xFF2A1B10 }, { "Male Baseball Cap", 0xFF3A4E7A } };
	int failures = 0, result;
	const char *bare = hair_of(d, outfit, parts, 3, &result);
	printf("hat: without one: %d, %s\n", result, bare);
	failures += result != XSDK_AVATAR_OK || strcmp(bare, "Raised Front") != 0;
	const char *capped = hair_of(d, outfit, parts, 4, &result);
	printf("hat: under a Male Baseball Cap: %d, %s\n", result, capped);
	failures += result != XSDK_AVATAR_OK || strcmp(capped, "Quiff (Hat)") != 0;
	printf(failures ? "hat: FAIL\n" : "hat: all passed\n");
	return failures ? 1 : 0;
}

// The rig export (src/avatar_export.c) as the console runs it: male or female (a bare body and head), stock (the
// avatar of check_stock), hat (a cap over a stock hair), or a description with the folders' item packages. Writes
// the .glb; the file holds the console's meshes, keep it off the repository.
static int export_rig(const char *out, const char *what)
{
	static const XsdkAvatarStockPart male[] = { { "MaleBody", 0xFFC68642 }, { "Head", 0xFFC68642 } };
	static const XsdkAvatarStockPart female[] = { { "FemaleBody", 0xFFC68642 }, { "Head", 0xFFC68642 } };
	static const XsdkAvatarStockPart stock[] = { { "MaleBody", 0xFFC68642 }, { "Head", 0xFFC68642 },
		{ "Buzz Cut", 0xFF2A1B10 }, { "Male Punk Tee", 0xFF303030 }, { "Jeans", 0xFF3A4E7A },
		{ "High Top Sneakers", 0xFF202020 } };
	// Hair under a hat: the cap over "Raised Front", which becomes "Quiff (Hat)".
	static const XsdkAvatarStockPart hat[] = { { "MaleBody", 0xFFC68642 }, { "Head", 0xFFC68642 },
		{ "Raised Front", 0xFF2A1B10 }, { "Male Baseball Cap", 0xFF3A4E7A } };
	void *glb = NULL;
	uint32_t size = 0;
	int result;
	if (strcmp(what, "male") == 0)
		result = xsdk_avatar_export_stock(male, 2, &glb, &size);
	else if (strcmp(what, "female") == 0)
		result = xsdk_avatar_export_stock(female, 2, &glb, &size);
	else if (strcmp(what, "stock") == 0)
		result = xsdk_avatar_export_stock(stock, sizeof stock / sizeof stock[0], &glb, &size);
	else if (strcmp(what, "hat") == 0)
		result = xsdk_avatar_export_stock(hat, sizeof hat / sizeof hat[0], &glb, &size);
	else {
		static uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
		FILE *file = fopen(what, "rb");
		if (!file || fread(bytes, 1, sizeof bytes, file) != sizeof bytes)
			return 2;
		fclose(file);
		result = xsdk_avatar_export_description(bytes, sizeof bytes, &glb, &size);
	}
	FILE *file = result == XSDK_AVATAR_OK ? fopen(out, "wb") : NULL;
	const bool written = file && fwrite(glb, 1, size, file) == size;
	if (file)
		fclose(file);
	free(glb);
	printf("export: %s: %d, %u bytes%s\n", what, result, (unsigned)size, written ? "" : ", not written");
	return written ? 0 : 1;
}

// One load, or one export, of the description; frees what it made.
static int load_once(const XsdkAvatarDescription *d, const uint8_t *bytes, bool export)
{
	if (export) {
		void *glb = NULL;
		uint32_t size = 0;
		const int result = xsdk_avatar_export_description(bytes, XSDK_AVATAR_DESCRIPTION_BYTES, &glb, &size);
		if (result != XSDK_AVATAR_OK && glb) {
			printf("alloc: a failed export left its file: FAIL\n");
			free(glb);
			return 1;
		}
		free(glb);
		return result;
	}
	static AvatarOutfit outfit;
	const int result = avatar_outfit_load(&outfit, d);
	if (result != XSDK_AVATAR_OK && outfit.part_count) {
		printf("alloc: a failed load left parts: FAIL\n");
		return 1;
	}
	avatar_outfit_free(&outfit);
	return result;
}

// The outfit load and the export with allocation 1, 2, 3, ... failing until a run makes none fail.
static int check_alloc(const XsdkAvatarDescription *d, const uint8_t *bytes)
{
	int failures = 0;
	quiet = true;
	for (int export = 0; export < 2; export++)
		for (uint32_t n = 1;; n++) {
			avatar_alloc_fail_at = n;
			const int result = load_once(d, bytes, export);
			const bool failed = avatar_alloc_fail_at == 0;
			avatar_alloc_fail_at = 0;
			if (!failed) {
				printf("alloc: %s: %u allocations, each failure %s\n", export ? "the export" : "the outfit load",
					n - 1, result == XSDK_AVATAR_OK ? "handled" : "handled, but the last run failed: FAIL");
				failures += result != XSDK_AVATAR_OK;
				break;
			}
			if (result != XSDK_AVATAR_ERROR_MEMORY) {
				printf("alloc: %s: allocation %u failed but it returned %d: FAIL\n",
					export ? "the export" : "the outfit load", n, result);
				failures++;
			}
		}
	quiet = false;
	printf(failures ? "alloc: FAIL\n" : "alloc: all passed\n");
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	if (argc < 3 || (strcmp(argv[2], "export") == 0 && argc < 5) || (strcmp(argv[2], "alloc") == 0 && argc < 4)) {
		fprintf(stderr, "usage: outfit_check <FFFE07DF00000002> <description> [<package folder> ...]\n"
						"       outfit_check <FFFE07DF00000002> stock|hat\n"
						"       outfit_check <FFFE07DF00000002> export <out.glb> male|female|stock|hat|<description> "
						"[<package folder> ...]\n"
						"       outfit_check <FFFE07DF00000002> alloc <description> [<package folder> ...]\n");
		return 2;
	}
	pack_file = fopen(argv[1], "rb");
	if (!pack_file || xsdk_avatar_pack_open(&pack, read_file, pack_file) != XSDK_AVATAR_OK)
		return 2;
	if (strcmp(argv[2], "export") == 0) {
		folders = (const char *const *)argv + 5;
		folder_count = argc - 5;
		return export_rig(argv[3], argv[4]);
	}
	const bool alloc = strcmp(argv[2], "alloc") == 0;
	const char *description = alloc ? argv[3] : argv[2];
	folders = (const char *const *)argv + (alloc ? 4 : 3);
	folder_count = argc - (alloc ? 4 : 3);
	XsdkAvatarDescription d;
	static AvatarOutfit outfit;
	if (strcmp(argv[2], "stock") == 0)
		return check_stock(&d, &outfit);
	if (strcmp(argv[2], "hat") == 0)
		return check_hat(&d, &outfit);
	static uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
	FILE *file = fopen(description, "rb");
	if (!file || fread(bytes, 1, sizeof bytes, file) != sizeof bytes)
		return 2;
	fclose(file);
	if (xsdk_avatar_description_parse(&d, bytes, sizeof bytes) != XSDK_AVATAR_OK)
		return 2;
	if (alloc)
		return check_alloc(&d, bytes);
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
		void *strb;
		(void)avatar_content_read_item(d.items[i].id, &strb, &size);
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
