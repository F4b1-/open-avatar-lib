// PC tool: decodes models with the library and prints what research/check_c.py compares with the Python prototypes.
//   avatar_dump pack <FFFE07DF00000002>     every model in the stock pack
//   avatar_dump asset <asset_v2.bin> ...    item files
//   avatar_dump item <package> ...          item packages (their asset_v2.bin)
//   avatar_dump desc <description> <FFFE07DF00000002>   a saved avatar description, its items named
//   avatar_dump shapes <FFFE07DF00000002>  every record's link and shape block (hiding templates)
//   avatar_dump bones <FFFE07DF00000002>   the avatar's skeleton from both bodies, props' skeletons, a test pose
//   avatar_dump motion <FFFE07DF00000002>  every stock animation: sums over its frames, and one sampled pose
// Each model: "<label> meshes <n>", then per mesh "  <vertices> <triangles> <uv sets> <skinned> <sum x> <sum y> <sum z>".

#include <stdio.h>
#include <stdlib.h>

#include "xsdk/avatar.h"

static int read_file(void *context, uint64_t offset, void *buffer, uint32_t length)
{
	FILE *file = context;
	if (fseek(file, (long)offset, SEEK_SET) != 0)
		return 1;
	return fread(buffer, 1, length, file) == length ? 0 : 1;
}

// FNV-1a, 32 bits: a fingerprint of texture pixels for research/check_c.py.
static uint32_t fnv1a(const uint8_t *p, uint32_t n)
{
	uint32_t h = 0x811C9DC5u;
	for (uint32_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 0x01000193u;
	return h;
}

static void dump_texture(const char *prefix, const XsdkAvatarTexture *t)
{
	printf("%s%#x %u %u %u %08X\n", prefix, (unsigned)t->format, (unsigned)t->width, (unsigned)t->height,
		(unsigned)t->frame_count, t->data ? (unsigned)fnv1a(t->data, t->frame_count * t->frame_bytes) : 0u);
}

// Prints a texture block if the file has one; returns 0, or 1 if it didn't decode.
static int dump_texture_block(const char *label, const void *strb, uint32_t size)
{
	XsdkAvatarTexture texture;
	const int result = xsdk_avatar_texture_decode(&texture, strb, size);
	if (result == XSDK_AVATAR_ERROR_NOT_FOUND)
		return 0;
	if (result != XSDK_AVATAR_OK) {
		printf("%s texture FAILED %d\n", label, result);
		return 1;
	}
	printf("%s texture ", label);
	dump_texture("", &texture);
	xsdk_avatar_texture_free(&texture);
	return 0;
}

// Prints one model; returns 0, or 1 if it didn't decode.
// After the meshes: "  t <format> <width> <height> <frames> <FNV-1a of the pixels, 0 for a slot>" per texture,
// "  p <id> T <texture> <uv set> <address>" or "  p <id> F <x> <y> <z> <w>" per parameter.
static int dump(const char *label, const void *strb, uint32_t size)
{
	XsdkAvatarModel model;
	const int result = xsdk_avatar_model_decode(&model, strb, size);
	if (result == XSDK_AVATAR_ERROR_NOT_FOUND)
		return dump_texture_block(label, strb, size);
	if (result != XSDK_AVATAR_OK) {
		printf("%s FAILED %d\n", label, result);
		return 1;
	}
	printf("%s meshes %u\n", label, (unsigned)model.mesh_count);
	for (uint32_t m = 0; m < model.mesh_count; m++) {
		const XsdkAvatarMesh *mesh = &model.meshes[m];
		double sum[3] = { 0, 0, 0 };
		for (uint32_t i = 0; i < mesh->vertex_count; i++)
			for (uint32_t a = 0; a < 3; a++)
				sum[a] += mesh->positions[3 * i + a];
		printf("  %u %u %u %d %.4f %.4f %.4f\n", (unsigned)mesh->vertex_count, (unsigned)(mesh->index_count / 3),
			(unsigned)mesh->uv_sets, mesh->bones != NULL, sum[0], sum[1], sum[2]);
	}
	for (uint32_t t = 0; t < model.texture_count; t++)
		dump_texture("  t ", &model.textures[t]);
	if (model.color_entry_count)
		printf("  k %u %08X %08X %08X\n", (unsigned)model.color_entry_count, (unsigned)model.color_entries[0][0],
			(unsigned)model.color_entries[0][1], (unsigned)model.color_entries[0][2]);
	for (uint32_t m = 0; m < model.material_count; m++) {
		const XsdkAvatarMaterial *material = &model.materials[m];
		uint32_t meshes = 0;
		for (uint32_t k = 0; k < model.mesh_count; k++)
			meshes += model.meshes[k].material == m;
		printf("  m %u %u %u\n", (unsigned)material->technique, (unsigned)material->parameter_count, (unsigned)meshes);
		for (uint32_t i = 0; i < material->parameter_count; i++) {
			const XsdkAvatarParameter *p = &material->parameters[i];
			if (p->is_texture)
				printf("  p %u T %u %u %u\n", (unsigned)p->id, (unsigned)p->texture, (unsigned)p->uv_set,
					(unsigned)p->address);
			else
				printf("  p %u F %g %g %g %g\n", (unsigned)p->id, (double)p->value[0], (double)p->value[1],
					(double)p->value[2], (double)p->value[3]);
		}
	}
	xsdk_avatar_model_free(&model);
	return 0;
}

// The test pose research/check_c.py also builds: per bone a rotation (as an animation stores it), the root moved, one
// bone scaled.
static void test_pose(XsdkAvatarBonePose *pose, uint32_t count)
{
	xsdk_avatar_pose_reset(pose, count);
	for (uint32_t b = 0; b < count; b++) {
		const float log[3] = { 0.05f * (float)((int)(b % 5) - 2), 0.04f * (float)((int)(b % 3) - 1),
			0.03f * (float)((int)(b % 7) - 3) };
		xsdk_avatar_quaternion_from_log(pose[b].rotation, log);
	}
	pose[0].translation[0] = 0.01f;
	pose[0].translation[1] = 0.02f;
	pose[0].translation[2] = -0.03f;
	if (count > 5) {
		pose[5].scale[0] = 1.1f;
		pose[5].scale[1] = 0.9f;
	}
}

// "<label> <bone> <parent> <x> <y> <z>" per bone, then "<label> posed <bone> <x> <y> <z>": where the test pose puts
// each bone (its skin matrix applied to its rest position).
static void dump_skeleton(const char *label, const XsdkAvatarSkeleton *skeleton)
{
	for (uint32_t b = 0; b < skeleton->bone_count; b++)
		printf("%s %u %d %.5f %.5f %.5f\n", label, (unsigned)b,
			skeleton->parents[b] == XSDK_AVATAR_NO_PARENT ? -1 : (int)skeleton->parents[b],
			(double)skeleton->positions[b][0], (double)skeleton->positions[b][1], (double)skeleton->positions[b][2]);
	XsdkAvatarBonePose pose[XSDK_AVATAR_MAX_BONES];
	float matrices[XSDK_AVATAR_MAX_BONES][12];
	test_pose(pose, skeleton->bone_count);
	xsdk_avatar_skin_matrices(skeleton, pose, matrices);
	for (uint32_t b = 0; b < skeleton->bone_count; b++) {
		const float *p = skeleton->positions[b], *m = matrices[b];
		printf("%s posed %u", label, (unsigned)b);
		for (uint32_t r = 0; r < 3; r++)
			printf(" %.5f", (double)(m[4 * r] * p[0] + m[4 * r + 1] * p[1] + m[4 * r + 2] * p[2] + m[4 * r + 3]));
		printf("\n");
	}
}

// Skeletons: the avatar's from MaleBody and FemaleBody, and every record's own skeleton block (props).
static int dump_bones(XsdkAvatarPack *pack)
{
	int failures = 0;
	for (uint32_t i = 0; i < xsdk_avatar_pack_count(pack); i++) {
		const uint32_t size = xsdk_avatar_pack_asset_size(pack, i);
		void *strb = size ? malloc(size) : NULL;
		if (!strb || xsdk_avatar_pack_read_asset(pack, i, strb) != XSDK_AVATAR_OK) {
			free(strb);
			continue;
		}
		char label[32];
		XsdkAvatarSkeleton skeleton;
		if (i < 2) {
			XsdkAvatarModel body;
			snprintf(label, sizeof label, "body %u", (unsigned)i);
			if (xsdk_avatar_model_decode(&body, strb, size) != XSDK_AVATAR_OK ||
				xsdk_avatar_skeleton_build(&skeleton, &body) != XSDK_AVATAR_OK) {
				printf("%s FAILED\n", label);
				failures++;
			} else {
				dump_skeleton(label, &skeleton);
				xsdk_avatar_model_free(&body);
			}
		}
		const int result = xsdk_avatar_skeleton_decode(&skeleton, strb, size);
		snprintf(label, sizeof label, "prop %u", (unsigned)i);
		if (result == XSDK_AVATAR_OK)
			dump_skeleton(label, &skeleton);
		else if (result != XSDK_AVATAR_ERROR_NOT_FOUND) {
			printf("%s FAILED %d\n", label, result);
			failures++;
		}
		free(strb);
	}
	return failures;
}

// Adds a rotation to a sum with its sign chosen so that w >= 0: q and -q are the same rotation.
static void add_rotation(double sum[4], const float q[4])
{
	const double sign = q[3] < 0.0f ? -1.0 : 1.0;
	for (uint32_t k = 0; k < 4; k++)
		sum[k] += sign * q[k];
}

// Every record's animation block: "<index> anim <frames> <fps> T <sums> R <sums> S <sums>" over all frames and
// bones, then "<index> sample T <sums> R <sums>" of the pose at 0.4 of its length (not looped).
static int dump_animations(XsdkAvatarPack *pack)
{
	int failures = 0;
	static XsdkAvatarBonePose pose[XSDK_AVATAR_BONES];
	for (uint32_t i = 0; i < xsdk_avatar_pack_count(pack); i++) {
		const uint32_t size = xsdk_avatar_pack_asset_size(pack, i);
		void *strb = size ? malloc(size) : NULL;
		if (!strb || xsdk_avatar_pack_read_asset(pack, i, strb) != XSDK_AVATAR_OK) {
			free(strb);
			continue;
		}
		XsdkAvatarAnimation animation;
		const int result = xsdk_avatar_animation_decode(&animation, strb, size);
		free(strb);
		if (result == XSDK_AVATAR_ERROR_NOT_FOUND)
			continue;
		if (result != XSDK_AVATAR_OK) {
			printf("%u anim FAILED %d\n", (unsigned)i, result);
			failures++;
			continue;
		}
		double t[3] = { 0 }, r[4] = { 0 }, s[3] = { 0 };
		for (uint32_t k = 0; k < animation.frame_count * animation.bone_count; k++) {
			const XsdkAvatarBonePose *p = &animation.poses[k];
			for (uint32_t a = 0; a < 3; a++) {
				t[a] += p->translation[a];
				s[a] += p->scale[a];
			}
			add_rotation(r, p->rotation);
		}
		printf("%u anim %u %.4f T %.4f %.4f %.4f R %.4f %.4f %.4f %.4f S %.4f %.4f %.4f\n", (unsigned)i,
			(unsigned)animation.frame_count, (double)animation.frames_per_second, t[0], t[1], t[2], r[0], r[1], r[2],
			r[3], s[0], s[1], s[2]);
		xsdk_avatar_animation_sample(&animation, 0.4f * xsdk_avatar_animation_length(&animation), false, pose);
		double st[3] = { 0 }, sr[4] = { 0 };
		for (uint32_t b = 0; b < animation.bone_count; b++) {
			for (uint32_t a = 0; a < 3; a++)
				st[a] += pose[b].translation[a];
			add_rotation(sr, pose[b].rotation);
		}
		printf("%u sample T %.4f %.4f %.4f R %.4f %.4f %.4f %.4f\n", (unsigned)i, st[0], st[1], st[2], sr[0], sr[1],
			sr[2], sr[3]);
		xsdk_avatar_animation_free(&animation);
	}
	return failures;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: avatar_dump pack <package> | asset <file> ...\n");
		return 2;
	}
	int failures = 0;
	if (argv[1][0] == 'b' || argv[1][0] == 'm') {
		FILE *file = fopen(argv[2], "rb");
		XsdkAvatarPack *pack;
		if (!file || xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK)
			return 1;
		failures = argv[1][0] == 'b' ? dump_bones(pack) : dump_animations(pack);
		xsdk_avatar_pack_close(pack);
		fclose(file);
	} else if (argv[1][0] == 'p') {
		FILE *file = fopen(argv[2], "rb");
		XsdkAvatarPack *pack;
		if (!file || xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK) {
			fprintf(stderr, "can't open %s as an avatar pack package\n", argv[2]);
			return 1;
		}
		for (uint32_t i = 0; i < xsdk_avatar_pack_count(pack); i++) {
			const uint32_t size = xsdk_avatar_pack_asset_size(pack, i);
			if (!size)
				continue;
			void *strb = malloc(size);
			if (!strb || xsdk_avatar_pack_read_asset(pack, i, strb) != XSDK_AVATAR_OK) {
				printf("%u FAILED read\n", (unsigned)i);
				failures++;
				free(strb);
				continue;
			}
			char label[160];
			snprintf(label, sizeof label, "%u %s", (unsigned)i, xsdk_avatar_pack_name(pack, i));
			failures += dump(label, strb, size);
			free(strb);
		}
		xsdk_avatar_pack_close(pack);
		fclose(file);
	} else if (argv[1][0] == 's') {
		// Every record's link and shape block: "<index> link <mask> <record>" and
		// "<index> shape <target> <index bytes> <count> <sum of the triangle numbers> overrides <count> <sum of offsets>
		// <sum of positions>". Each shape is also applied to its decoded target; a failure counts.
		FILE *file = fopen(argv[2], "rb");
		XsdkAvatarPack *pack;
		if (!file || xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK)
			return 1;
		XsdkAvatarModel targets[3] = { 0 };
		for (uint32_t t = 0; t < 3; t++) {
			const uint32_t size = xsdk_avatar_pack_asset_size(pack, t);
			void *strb = malloc(size);
			if (!strb || xsdk_avatar_pack_read_asset(pack, t, strb) != XSDK_AVATAR_OK ||
				xsdk_avatar_model_decode(&targets[t], strb, size) != XSDK_AVATAR_OK)
				return 1;
			free(strb);
		}
		for (uint32_t i = 0; i < xsdk_avatar_pack_count(pack); i++) {
			uint8_t id[XSDK_AVATAR_ID_BYTES];
			uint32_t record;
			if (xsdk_avatar_pack_link(pack, i, id) && xsdk_avatar_id_is_stock(id, &record))
				printf("%u link %#x %u\n", (unsigned)i,
					(unsigned)((uint32_t)id[0] << 24 | (uint32_t)id[1] << 16 | (uint32_t)id[2] << 8 | id[3]),
					(unsigned)record);
			const uint32_t size = xsdk_avatar_pack_asset_size(pack, i);
			void *strb = size ? malloc(size) : NULL;
			if (!strb || xsdk_avatar_pack_read_asset(pack, i, strb) != XSDK_AVATAR_OK) {
				free(strb);
				continue;
			}
			XsdkAvatarShape shape;
			const int result = xsdk_avatar_shape_decode(&shape, strb, size);
			if (result == XSDK_AVATAR_OK) {
				uint32_t sum = 0;
				for (uint32_t k = 0; k < shape.hidden_count; k++)
					sum += shape.hidden[k];
				uint32_t offsets = 0;
				double positions = 0;
				for (uint32_t k = 0; k < shape.override_count; k++) {
					offsets += shape.override_offsets[k];
					positions += shape.positions[3 * k] + shape.positions[3 * k + 1] + shape.positions[3 * k + 2];
				}
				printf("%u shape %u %#x %u %u overrides %u %u %.2f\n", (unsigned)i, (unsigned)shape.target_record,
					(unsigned)shape.target_index_bytes, (unsigned)shape.hidden_count, (unsigned)sum,
					(unsigned)shape.override_count, (unsigned)offsets, positions);
				if (shape.target_record > 2 ||
					xsdk_avatar_shape_apply(&shape, &targets[shape.target_record]) != XSDK_AVATAR_OK) {
					fprintf(stderr, "%u: apply failed\n", (unsigned)i);
					failures++;
				}
				xsdk_avatar_shape_free(&shape);
			} else if (result != XSDK_AVATAR_ERROR_NOT_FOUND) {
				printf("%u shape FAILED %d\n", (unsigned)i, result);
				failures++;
			}
			free(strb);
		}
		for (uint32_t t = 0; t < 3; t++)
			xsdk_avatar_model_free(&targets[t]);
		xsdk_avatar_pack_close(pack);
		fclose(file);
	} else if (argv[1][0] == 'i') {
		// Item packages: asset_v2.bin read through the package's block chain.
		for (int a = 2; a < argc; a++) {
			FILE *file = fopen(argv[a], "rb");
			void *strb = NULL;
			uint32_t size = 0;
			const int result = file ? xsdk_avatar_package_read_file(read_file, file, "asset_v2.bin", &strb, &size)
									: XSDK_AVATAR_ERROR_READ;
			if (file)
				fclose(file);
			if (result != XSDK_AVATAR_OK) {
				printf("%s FAILED package %d\n", argv[a], result);
				failures++;
				continue;
			}
			failures += dump(argv[a], strb, size);
			free(strb);
		}
	} else if (argv[1][0] == 'd') {
		// A saved description and the pack package: what the viewer would load.
		FILE *desc = fopen(argv[2], "rb");
		uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
		XsdkAvatarDescription d;
		FILE *file = argc > 3 ? fopen(argv[3], "rb") : NULL;
		XsdkAvatarPack *pack;
		if (!desc || fread(bytes, 1, sizeof bytes, desc) != sizeof bytes ||
			xsdk_avatar_description_parse(&d, bytes, sizeof bytes) != XSDK_AVATAR_OK || !file ||
			xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK) {
			fprintf(stderr, "usage: avatar_dump desc <description.bin> <pack package>\n");
			return 1;
		}
		printf("height %g weight %g, skin %08X hair %08X\n", (double)d.height, (double)d.weight,
			(unsigned)d.colors[XSDK_AVATAR_COLOR_SKIN], (unsigned)d.colors[XSDK_AVATAR_COLOR_HAIR]);
		for (uint32_t i = 0; i < d.item_count; i++) {
			uint32_t record, title;
			char name[33];
			if (xsdk_avatar_id_is_stock(d.items[i].id, &record)) {
				printf("item %u: slots %#x stock #%u %s, colour %08X\n", (unsigned)i, (unsigned)d.items[i].slots,
					(unsigned)record, xsdk_avatar_pack_name(pack, record), (unsigned)d.items[i].colors[0]);
			} else {
				xsdk_avatar_id_package_name(d.items[i].id, name, &title);
				printf("item %u: slots %#x package %08X\\00009000\\%s\n", (unsigned)i, (unsigned)d.items[i].slots,
					(unsigned)title, name);
			}
		}
		xsdk_avatar_pack_close(pack);
		fclose(file);
		fclose(desc);
	} else {
		for (int a = 2; a < argc; a++) {
			FILE *file = fopen(argv[a], "rb");
			if (!file)
				return 1;
			fseek(file, 0, SEEK_END);
			const long size = ftell(file);
			void *strb = malloc((size_t)size);
			fseek(file, 0, SEEK_SET);
			if (!strb || fread(strb, 1, (size_t)size, file) != (size_t)size)
				return 1;
			fclose(file);
			failures += dump(argv[a], strb, (uint32_t)size);
			free(strb);
		}
	}
	fprintf(stderr, "failures: %d\n", failures);
	return failures ? 1 : 0;
}
