// A rig to animate (avatar.h, SPECS.md section 4.9): an avatar's skeleton and parts as a glTF binary, made on the
// console from its own files, as the avatar layer loads them. No GPU: tests/outfit_check.c runs it on the PC too.
//
// The skeleton is the library's 71 bones in its order and tree, in the T-pose with every rest rotation the identity,
// so a rotation keyed on a bone is the library's pose rotation as it is; the bones carry 360sona's names, so that
// tools/anim_convert.py finds them after a 3D program reorders the joints. Metres, y up, the avatar facing +z, as the
// library and glTF both have it.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_runtime.h"
#include "xsdk/log.h"

enum {
	GL_UNSIGNED_BYTE = 5121,
	GL_UNSIGNED_SHORT = 5123,
	GL_FLOAT = 5126,
	GL_ARRAY_BUFFER = 34962,
	GL_ELEMENT_ARRAY_BUFFER = 34963,
	GLB_MAGIC = 0x46546C67, // "glTF"
	GLB_JSON = 0x4E4F534A,  // "JSON"
	GLB_BIN = 0x004E4942,   // "BIN"
};

// 360sona's names (SPECS.md, section 4.7), as tools/anim_convert.py's HUMANOID table reads them; NULL: "shape_NN".
static const char *const bone_names[XSDK_AVATAR_BONES] = {
	[0] = "Hips",
	[1] = "Spine",
	[2] = "LeftUpperLeg",
	[3] = "RightUpperLeg",
	[5] = "Chest",
	[6] = "LeftLowerLeg",
	[8] = "RightLowerLeg",
	[11] = "LeftFoot",
	[12] = "LeftShoulder",
	[14] = "Neck",
	[15] = "RightFoot",
	[16] = "RightShoulder",
	[19] = "Head",
	[20] = "LeftUpperArm",
	[21] = "LeftToes",
	[22] = "RightUpperArm",
	[23] = "RightToes",
	[25] = "LeftLowerArm",
	[28] = "RightLowerArm",
	[33] = "LeftHand",
	[36] = "RightHand",
	[37] = "LeftIndexProximal",
	[38] = "LeftMiddleProximal",
	[39] = "LeftRingProximal",
	[40] = "LeftLittleProximal",
	[41] = "PropLeft",
	[42] = "SpecialLeft",
	[43] = "LeftThumbProximal",
	[44] = "RightIndexProximal",
	[45] = "RightMiddleProximal",
	[46] = "RightRingProximal",
	[47] = "RightLittleProximal",
	[48] = "PropRight",
	[49] = "SpecialRight",
	[50] = "RightThumbProximal",
	[51] = "LeftIndexIntermediate",
	[52] = "LeftMiddleIntermediate",
	[53] = "LeftRingIntermediate",
	[54] = "LeftLittleIntermediate",
	[55] = "LeftThumbIntermediate",
	[56] = "RightIndexIntermediate",
	[57] = "RightMiddleIntermediate",
	[58] = "RightRingIntermediate",
	[59] = "RightLittleIntermediate",
	[60] = "RightThumbIntermediate",
	[61] = "LeftIndexDistal",
	[62] = "LeftMiddleDistal",
	[63] = "LeftRingDistal",
	[64] = "LeftLittleDistal",
	[65] = "LeftThumbDistal",
	[66] = "RightIndexDistal",
	[67] = "RightMiddleDistal",
	[68] = "RightRingDistal",
	[69] = "RightLittleDistal",
	[70] = "RightThumbDistal",
};

// The colour of a part without colours of its own.
static const float plain_grey[3] = { 0.6f, 0.6f, 0.6f };

// Growing memory: the binary chunk, and the JSON's lists as text.
typedef struct {
	char *data;
	size_t length, capacity;
	uint32_t count; // items in a JSON list
	bool failed;
} Buffer;

static bool reserve(Buffer *b, size_t more)
{
	if (b->failed)
		return false;
	if (b->length + more <= b->capacity)
		return true;
	size_t capacity = b->capacity ? b->capacity : 4096;
	while (capacity < b->length + more)
		capacity *= 2;
	char *data = realloc(b->data, capacity);
	if (!data) {
		b->failed = true;
		return false;
	}
	b->data = data;
	b->capacity = capacity;
	return true;
}

static void append(Buffer *b, const void *data, size_t size)
{
	if (reserve(b, size)) {
		memcpy(b->data + b->length, data, size);
		b->length += size;
	}
}

static void printf_append(Buffer *b, const char *format, va_list args)
{
	va_list again;
	va_copy(again, args);
	const int n = vsnprintf(NULL, 0, format, args);
	if (n >= 0 && reserve(b, (size_t)n + 1)) {
		vsnprintf(b->data + b->length, (size_t)n + 1, format, again);
		b->length += (size_t)n;
	}
	va_end(again);
}

static void text(Buffer *b, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	printf_append(b, format, args);
	va_end(args);
}

// An item of a JSON list, the comma before all but the first.
static void item(Buffer *list, const char *format, ...)
{
	if (list->count++)
		append(list, ",", 1);
	va_list args;
	va_start(args, format);
	printf_append(list, format, args);
	va_end(args);
}

typedef struct {
	Buffer bin, views, accessors, primitives, materials, nodes, images, textures;
} Gltf;

// The two samplers the baked textures take: 0 repeats, 1 clamps (glTF's REPEAT and CLAMP_TO_EDGE), both linear.
static const char samplers[] = "{\"magFilter\":9729,\"minFilter\":9729,\"wrapS\":10497,\"wrapT\":10497},"
							   "{\"magFilter\":9729,\"minFilter\":9729,\"wrapS\":33071,\"wrapT\":33071}";
enum {
	NO_TEXCOORD = UINT32_MAX,
};

// Elements of element_bytes as a buffer view, starting on 4 bytes; returns the view. glTF is little-endian: on the
// console (big-endian) each element's bytes are turned round as they go in.
static uint32_t add_view(Gltf *g, const void *data, size_t size, uint32_t target, uint32_t element_bytes)
{
	const size_t offset = g->bin.length;
	append(&g->bin, data, size);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	if (!g->bin.failed && element_bytes > 1)
		for (size_t at = offset; at + element_bytes <= offset + size; at += element_bytes)
			for (uint32_t k = 0; k < element_bytes / 2; k++) {
				const char swap = g->bin.data[at + k];
				g->bin.data[at + k] = g->bin.data[at + element_bytes - 1 - k];
				g->bin.data[at + element_bytes - 1 - k] = swap;
			}
#else
	(void)element_bytes;
#endif
	static const uint8_t zeros[3];
	append(&g->bin, zeros, (4 - g->bin.length % 4) % 4);
	if (target)
		item(&g->views, "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u,\"target\":%u}", (unsigned)offset,
			(unsigned)size, (unsigned)target);
	else
		item(&g->views, "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", (unsigned)offset, (unsigned)size);
	return g->views.count - 1;
}

// A view and its accessor; extra is more of the accessor (bounds, normalized) or "".
static uint32_t add_accessor(Gltf *g, const void *data, size_t size, uint32_t target, uint32_t component,
	uint32_t count, const char *type, const char *extra)
{
	const uint32_t view =
		add_view(g, data, size, target, component == GL_FLOAT ? 4 : component == GL_UNSIGNED_SHORT ? 2 : 1);
	item(&g->accessors, "{\"bufferView\":%u,\"componentType\":%u,\"count\":%u,\"type\":\"%s\"%s}", (unsigned)view,
		(unsigned)component, (unsigned)count, type, extra);
	return g->accessors.count - 1;
}

// A JSON string's characters: quotes, backslashes and control characters left out (labels are names and hex ids).
static void clean_name(char *out, size_t size, const char *in)
{
	size_t n = 0;
	for (; *in && n + 1 < size; in++)
		if (*in != '"' && *in != '\\' && (unsigned char)*in >= 0x20)
			out[n++] = *in;
	out[n] = 0;
}

// One mesh of a part as a primitive: its vertices, and its triangles other parts don't hide (hidden, from the
// part's first triangle at triangle). Returns whether it kept any.
static bool add_mesh(Gltf *g, const XsdkAvatarMesh *mesh, const uint8_t *hidden, uint32_t triangle, uint32_t material,
	uint32_t uv_set)
{
	const uint32_t n = mesh->vertex_count;
	uint16_t *indices = malloc(mesh->index_count ? mesh->index_count * 2u : 2u);
	uint8_t *joints = malloc(n ? n * 4u : 4u), *weights = malloc(n ? n * 4u : 4u);
	if (!indices || !joints || !weights) {
		free(indices);
		free(joints);
		free(weights);
		g->bin.failed = true;
		return false;
	}
	uint32_t kept = 0;
	for (uint32_t t = 0; t < mesh->index_count / 3; t++)
		if (!hidden[triangle + t])
			for (uint32_t c = 0; c < 3; c++)
				indices[kept++] = mesh->indices[3 * t + c];
	if (!kept || !n) {
		free(indices);
		free(joints);
		free(weights);
		return false;
	}
	// As the renderer takes them: a mesh naming no bone follows the root; a joint without weight given as 0.
	for (uint32_t i = 0; i < 4 * n; i++) {
		const uint32_t bone = mesh->bones && mesh->bones[i] < XSDK_AVATAR_BONES ? mesh->bones[i] : 0;
		weights[i] = mesh->weights ? mesh->weights[i] : (i % 4 ? 0 : 255);
		joints[i] = weights[i] ? (uint8_t)bone : 0;
	}
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
	for (uint32_t v = 0; v < n; v++)
		for (uint32_t a = 0; a < 3; a++) {
			const float p = mesh->positions[3 * v + a];
			lo[a] = p < lo[a] ? p : lo[a];
			hi[a] = p > hi[a] ? p : hi[a];
		}
	char bounds[200];
	snprintf(bounds, sizeof bounds, ",\"min\":[%.6f,%.6f,%.6f],\"max\":[%.6f,%.6f,%.6f]", (double)lo[0],
		(double)lo[1], (double)lo[2], (double)hi[0], (double)hi[1], (double)hi[2]);
	const uint32_t position =
		add_accessor(g, mesh->positions, (size_t)n * 12, GL_ARRAY_BUFFER, GL_FLOAT, n, "VEC3", bounds);
	const uint32_t normal = add_accessor(g, mesh->normals, (size_t)n * 12, GL_ARRAY_BUFFER, GL_FLOAT, n, "VEC3", "");
	const uint32_t joint = add_accessor(g, joints, (size_t)n * 4, GL_ARRAY_BUFFER, GL_UNSIGNED_BYTE, n, "VEC4", "");
	const uint32_t weight = add_accessor(
		g, weights, (size_t)n * 4, GL_ARRAY_BUFFER, GL_UNSIGNED_BYTE, n, "VEC4", ",\"normalized\":true");
	const uint32_t index =
		add_accessor(g, indices, (size_t)kept * 2, GL_ELEMENT_ARRAY_BUFFER, GL_UNSIGNED_SHORT, kept, "SCALAR", "");
	// The texture coordinates the material's baked texture is drawn in, as TEXCOORD_0.
	char texcoord[40] = "";
	if (uv_set != NO_TEXCOORD && mesh->uv_sets) {
		const uint32_t set = uv_set < mesh->uv_sets ? uv_set : 0;
		float *uvs = malloc((size_t)n * 8);
		if (uvs) {
			for (uint32_t v = 0; v < n; v++)
				memcpy(uvs + 2 * v, mesh->uvs + 2 * ((size_t)mesh->uv_sets * v + set), 8);
			snprintf(texcoord, sizeof texcoord, ",\"TEXCOORD_0\":%u",
				(unsigned)add_accessor(g, uvs, (size_t)n * 8, GL_ARRAY_BUFFER, GL_FLOAT, n, "VEC2", ""));
			free(uvs);
		} else
			g->bin.failed = true;
	}
	item(&g->primitives,
		"{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u%s},\"indices\":%u,"
		"\"material\":%u}",
		(unsigned)position, (unsigned)normal, (unsigned)joint, (unsigned)weight, texcoord, (unsigned)index,
		(unsigned)material);
	free(indices);
	free(joints);
	free(weights);
	return true;
}

// The joints, nodes 0-70: each placed from its parent, no rotation; their inverse bind matrices move back from each
// pivot (column-major, the translation in the last column).
static uint32_t add_skeleton(Gltf *g, const XsdkAvatarSkeleton *skeleton)
{
	static float ibm[XSDK_AVATAR_BONES][16];
	for (uint32_t b = 0; b < XSDK_AVATAR_BONES; b++) {
		const uint8_t parent = skeleton->parents[b];
		float t[3];
		for (uint32_t a = 0; a < 3; a++)
			t[a] = skeleton->positions[b][a] - (parent == XSDK_AVATAR_NO_PARENT ? 0.0f : skeleton->positions[parent][a]);
		char name[24];
		if (bone_names[b])
			snprintf(name, sizeof name, "%s", bone_names[b]);
		else
			snprintf(name, sizeof name, "shape_%02u", (unsigned)b);
		Buffer children = { 0 };
		for (uint32_t c = b + 1; c < XSDK_AVATAR_BONES; c++)
			if (skeleton->parents[c] == b)
				item(&children, "%u", (unsigned)c);
		append(&children, "", 1);
		if (children.failed)
			g->nodes.failed = true;
		else if (children.count)
			item(&g->nodes, "{\"name\":\"%s\",\"translation\":[%.6f,%.6f,%.6f],\"children\":[%s]}", name,
				(double)t[0], (double)t[1], (double)t[2], children.data);
		else
			item(&g->nodes, "{\"name\":\"%s\",\"translation\":[%.6f,%.6f,%.6f]}", name, (double)t[0], (double)t[1],
				(double)t[2]);
		free(children.data);
		memset(ibm[b], 0, sizeof ibm[b]);
		ibm[b][0] = ibm[b][5] = ibm[b][10] = ibm[b][15] = 1.0f;
		for (uint32_t a = 0; a < 3; a++)
			ibm[b][12 + a] = -skeleton->positions[b][a];
	}
	return add_accessor(g, ibm, sizeof ibm, 0, GL_FLOAT, XSDK_AVATAR_BONES, "MAT4", "");
}

// The parts as one mesh of primitives, a material for each of their materials with its textures baked (avatar_bake.c).
static void add_parts(Gltf *g, const AvatarOutfit *outfit)
{
	for (uint32_t p = 0; p < outfit->part_count; p++) {
		const AvatarPart *part = &outfit->parts[p];
		const XsdkAvatarModel *model = &part->model;
		uint32_t triangles = 0;
		for (uint32_t m = 0; m < model->mesh_count; m++)
			triangles += model->meshes[m].index_count / 3;
		if (!triangles)
			continue;
		uint8_t *hidden = malloc(triangles);
		if (!hidden) {
			g->bin.failed = true;
			return;
		}
		avatar_outfit_hidden(outfit, part, hidden, triangles);
		char name[64];
		clean_name(name, sizeof name, part->label);
		// A glTF material per material of the part, with its textures baked into one; a mesh naming a material the
		// model doesn't have goes with the first.
		const uint32_t materials = model->material_count ? model->material_count : 1;
		for (uint32_t k = 0; k < materials; k++) {
			AvatarBake bake;
			const bool textured = avatar_bake_material(part, k, hidden, &bake) == XSDK_AVATAR_OK;
			const uint32_t material = g->materials.count;
			bool any = false;
			for (uint32_t m = 0, triangle = 0; m < model->mesh_count; m++) {
				const XsdkAvatarMesh *mesh = &model->meshes[m];
				if (mesh->material == k || (k == 0 && mesh->material >= materials))
					any |= add_mesh(g, mesh, hidden, triangle, material, textured ? bake.uv_set : NO_TEXCOORD);
				triangle += mesh->index_count / 3;
			}
			uint8_t *png = NULL;
			uint32_t png_bytes = 0;
			if (any && textured && avatar_png_encode(bake.rgba, bake.width, bake.height, &png, &png_bytes) ==
					XSDK_AVATAR_OK) {
				const uint32_t view = add_view(g, png, png_bytes, 0, 1);
				item(&g->images, "{\"name\":\"%s %u\",\"bufferView\":%u,\"mimeType\":\"image/png\"}", name,
					(unsigned)k, (unsigned)view);
				item(&g->textures, "{\"sampler\":%u,\"source\":%u}", bake.clamp ? 1u : 0u,
					(unsigned)g->images.count - 1);
				item(&g->materials,
					"{\"name\":\"%s %u\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":%u},"
					"\"metallicFactor\":0,\"roughnessFactor\":0.9}%s}",
					name, (unsigned)k, (unsigned)g->textures.count - 1,
					bake.see_through ? ",\"alphaMode\":\"BLEND\"" : "");
			} else if (any) {
				// No texture to bake (or no room): the part's first colour, as before textures.
				const float *c = part->is_head ? part->skin : part->tinted ? part->colors[0] : plain_grey;
				item(&g->materials,
					"{\"name\":\"%s %u\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.4f,%.4f,%.4f,1],"
					"\"metallicFactor\":0,\"roughnessFactor\":0.9}}",
					name, (unsigned)k, (double)c[0], (double)c[1], (double)c[2]);
			}
			free(png);
			if (textured)
				free(bake.rgba);
		}
		free(hidden);
	}
}

static void free_gltf(Gltf *g)
{
	Buffer *buffers[] = { &g->bin, &g->views, &g->accessors, &g->primitives, &g->materials, &g->nodes, &g->images,
		&g->textures };
	for (size_t i = 0; i < sizeof buffers / sizeof buffers[0]; i++)
		free(buffers[i]->data);
}

int avatar_export_outfit(const AvatarOutfit *outfit, void **glb, uint32_t *size_bytes)
{
	*glb = NULL;
	*size_bytes = 0;
	if (outfit->skeleton.bone_count != XSDK_AVATAR_BONES)
		return XSDK_AVATAR_ERROR_FORMAT; // no body
	Gltf g = { 0 };
	const uint32_t ibm = add_skeleton(&g, &outfit->skeleton);
	add_parts(&g, outfit);
	// Node 71 the skinned mesh, node 72 the armature above it all.
	Buffer joints = { 0 };
	for (uint32_t b = 0; b < XSDK_AVATAR_BONES; b++)
		item(&joints, "%u", (unsigned)b);
	append(&joints, "", 1);
	item(&g.nodes, "{\"name\":\"Avatar mesh\",\"mesh\":0,\"skin\":0}");
	item(&g.nodes, "{\"name\":\"Avatar\",\"children\":[0,%u]}", (unsigned)XSDK_AVATAR_BONES);
	append(&g.views, "", 1);
	append(&g.accessors, "", 1);
	append(&g.primitives, "", 1);
	append(&g.materials, "", 1);
	append(&g.nodes, "", 1);
	// The baked textures, if any: glTF allows no empty lists.
	Buffer pictures = { 0 };
	if (g.images.count) {
		append(&g.images, "", 1);
		append(&g.textures, "", 1);
		text(&pictures, "\"images\":[%s],\"textures\":[%s],\"samplers\":[%s],", g.images.data, g.textures.data,
			samplers);
	}
	append(&pictures, "", 1);
	Buffer json = { 0 };
	const bool built = !g.bin.failed && !g.views.failed && !g.accessors.failed && !g.primitives.failed &&
		!g.materials.failed && !g.nodes.failed && !g.images.failed && !g.textures.failed && !pictures.failed &&
		!joints.failed && g.primitives.count;
	if (built)
		text(&json,
			"{\"asset\":{\"version\":\"2.0\",\"generator\":\"xsdk avatar library\"},"
			"\"scene\":0,\"scenes\":[{\"name\":\"Avatar\",\"nodes\":[%u]}],"
			"\"nodes\":[%s],"
			"\"skins\":[{\"name\":\"Avatar\",\"inverseBindMatrices\":%u,\"skeleton\":0,\"joints\":[%s]}],"
			"\"meshes\":[{\"name\":\"Avatar\",\"primitives\":[%s]}],"
			"\"materials\":[%s],%s"
			"\"buffers\":[{\"byteLength\":%u}],"
			"\"bufferViews\":[%s],"
			"\"accessors\":[%s]}",
			(unsigned)XSDK_AVATAR_BONES + 1, g.nodes.data, (unsigned)ibm, joints.data, g.primitives.data,
			g.materials.data, pictures.data, (unsigned)g.bin.length, g.views.data, g.accessors.data);
	free(joints.data);
	free(pictures.data);
	if (!built || json.failed) {
		// Nothing to show (every part hidden or empty) is a format error, else memory ran out.
		const int result = g.primitives.count ? XSDK_AVATAR_ERROR_MEMORY : XSDK_AVATAR_ERROR_FORMAT;
		free(json.data);
		free_gltf(&g);
		return result;
	}
	// The container (glTF 2.0, little-endian): a header, the JSON padded with spaces, the binary padded with zeros.
	const uint32_t json_padded = (uint32_t)(json.length + 3) & ~3u, bin_length = (uint32_t)g.bin.length;
	const uint32_t total = 12 + 8 + json_padded + 8 + bin_length;
	uint8_t *out = malloc(total);
	if (out) {
		const uint32_t words[5] = { GLB_MAGIC, 2, total, json_padded, GLB_JSON };
		for (uint32_t w = 0; w < 5; w++)
			for (uint32_t k = 0; k < 4; k++)
				out[4 * w + k] = (uint8_t)(words[w] >> (8 * k));
		memcpy(out + 20, json.data, json.length);
		memset(out + 20 + json.length, ' ', json_padded - json.length);
		const uint32_t chunk[2] = { bin_length, GLB_BIN };
		for (uint32_t w = 0; w < 2; w++)
			for (uint32_t k = 0; k < 4; k++)
				out[20 + json_padded + 4 * w + k] = (uint8_t)(chunk[w] >> (8 * k));
		memcpy(out + 28 + json_padded, g.bin.data, bin_length);
	}
	free(json.data);
	free_gltf(&g);
	if (!out)
		return XSDK_AVATAR_ERROR_MEMORY;
	*glb = out;
	*size_bytes = total;
	return XSDK_AVATAR_OK;
}

static int export_described(const XsdkAvatarDescription *d, void **glb, uint32_t *size_bytes)
{
	*glb = NULL;
	*size_bytes = 0;
	AvatarOutfit *outfit = malloc(sizeof *outfit);
	if (!outfit)
		return XSDK_AVATAR_ERROR_MEMORY;
	int result = avatar_outfit_load(outfit, d);
	if (result == XSDK_AVATAR_OK) {
		result = avatar_export_outfit(outfit, glb, size_bytes);
		avatar_outfit_free(outfit);
	}
	free(outfit);
	if (result != XSDK_AVATAR_OK)
		xsdk_log_write("avatar: export failed: %d", result);
	return result;
}

int xsdk_avatar_export_profile(uint32_t user_index, void **glb, uint32_t *size_bytes)
{
	*glb = NULL;
	*size_bytes = 0;
	static uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
	const int result = avatar_content_read_profile(user_index, bytes);
	return result == XSDK_AVATAR_OK ? xsdk_avatar_export_description(bytes, sizeof bytes, glb, size_bytes) : result;
}

int xsdk_avatar_export_description(const void *bytes, uint32_t size_bytes, void **glb, uint32_t *glb_bytes)
{
	*glb = NULL;
	*glb_bytes = 0;
	XsdkAvatarDescription d;
	const int result = xsdk_avatar_description_parse(&d, bytes, size_bytes);
	return result == XSDK_AVATAR_OK ? export_described(&d, glb, glb_bytes) : result;
}

int xsdk_avatar_export_stock(const XsdkAvatarStockPart *parts, uint32_t count, void **glb, uint32_t *size_bytes)
{
	*glb = NULL;
	*size_bytes = 0;
	XsdkAvatarDescription d;
	const int result = avatar_outfit_describe_stock(&d, parts, count);
	return result == XSDK_AVATAR_OK ? export_described(&d, glb, size_bytes) : result;
}
