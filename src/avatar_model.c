// Model blocks (FORMATS.md, sections 3.3, 3.5 and 3.6): STRB block 3, compressed (3.2), holding an asset's meshes,
// its texture table and its materials; and the colour table (block 7). Also the packed vertex format meshes and
// shape overrides (avatar_shape.c) share. Little-endian, bit streams LSB first. Builds for the PC.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

// The expanded model's header (section 3.3): u32 words; these are the ones read.
enum {
	MODEL_INDEX_BYTES_AT = 16, // word 4: all meshes' indices, as 16-bit
	MODEL_MESH_COUNT_AT = 20,  // word 5
	MODEL_TEXTURE_COUNT_AT = 24, // word 6
	FIRST_PART_AT = 0x30,      // the first part's header, after the model header's 12 words (section 3.6)
	MAX_MESHES = 64,           // a sanity limit: the pack's models have at most a handful
	MAX_TEXTURES = 32,
};

// ---- Packed vertices ----

// The float header before a vertex format's bit stream: f32 step, f32 minimum per axis.
enum {
	FORMAT_STEP_AT = 0,
	FORMAT_MIN_AT = 4,
	FORMAT_FLOATS_BYTES = 16,
	POSITION_WIDTH_BITS = 6,
	ELEMENT_FIELD_BITS = 32,             // an element's base, then its width
	FORMAT_STREAM_BITS = 3 * POSITION_WIDTH_BITS + AVATAR_ELEMENTS * 2 * ELEMENT_FIELD_BITS, // 274
	FORMAT_STREAM_BYTES = (FORMAT_STREAM_BITS + 7) / 8,
};

int avatar_read_vertex_format(const uint8_t *data, uint32_t bytes, uint32_t at, AvatarVertexFormat *f)
{
	if ((uint64_t)at + FORMAT_FLOATS_BYTES + FORMAT_STREAM_BYTES > bytes)
		return XSDK_AVATAR_ERROR_FORMAT;
	f->step = avatar_float(avatar_le32(data + at + FORMAT_STEP_AT));
	for (uint32_t a = 0; a < 3; a++)
		f->min[a] = avatar_float(avatar_le32(data + at + FORMAT_MIN_AT + 4 * a));
	uint64_t s = (uint64_t)(at + FORMAT_FLOATS_BYTES) * 8;
	f->bits = 0;
	for (uint32_t a = 0; a < 3; a++, s += POSITION_WIDTH_BITS) {
		f->position_width[a] = avatar_stream_bits(data, s, POSITION_WIDTH_BITS);
		f->bits += f->position_width[a];
	}
	for (uint32_t e = 0; e < AVATAR_ELEMENTS; e++, s += 2 * ELEMENT_FIELD_BITS) {
		f->base[e] = avatar_stream_bits(data, s, ELEMENT_FIELD_BITS);
		f->width[e] = avatar_stream_bits(data, s + ELEMENT_FIELD_BITS, ELEMENT_FIELD_BITS);
		if (f->width[e] > 32)
			return XSDK_AVATAR_ERROR_FORMAT;
		f->bits += f->width[e];
	}
	f->first_vertex = s;
	return XSDK_AVATAR_OK;
}

// A normal packed 11:11:10 (x, y, z, x in the low bits), each axis signed and scaled to -1..1, then normalised.
static void unpack_normal(uint32_t v, float *out)
{
	enum { X_BITS = 11, Y_BITS = 11, Z_BITS = 10 };
	out[0] = (float)avatar_signed_bits(v, 0, X_BITS) / (float)((1 << (X_BITS - 1)) - 1);
	out[1] = (float)avatar_signed_bits(v, X_BITS, Y_BITS) / (float)((1 << (Y_BITS - 1)) - 1);
	out[2] = (float)avatar_signed_bits(v, X_BITS + Y_BITS, Z_BITS) / (float)((1 << (Z_BITS - 1)) - 1);
	const float length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (length > 0) {
		out[0] /= length;
		out[1] /= length;
		out[2] /= length;
	}
}

void avatar_decode_vertex(const uint8_t *data, uint64_t *at, const AvatarVertexFormat *f, float position[3],
	float normal[3], uint32_t *color, uint8_t weights[4], uint8_t bones[4])
{
	for (uint32_t a = 0; a < 3; a++) {
		position[a] = f->min[a] + (float)avatar_stream_bits(data, *at, f->position_width[a]) * f->step *
			avatar_axis_scale[a];
		*at += f->position_width[a];
	}
	uint32_t element[AVATAR_ELEMENTS];
	for (uint32_t e = 0; e < AVATAR_ELEMENTS; e++) {
		element[e] = f->base[e] + avatar_stream_bits(data, *at, f->width[e]); // mod 2^32
		*at += f->width[e];
	}
	unpack_normal(element[AVATAR_ELEMENT_NORMAL], normal);
	*color = element[AVATAR_ELEMENT_COLOR];
	if (weights)
		for (uint32_t k = 0; k < 4; k++) {
			weights[k] = (uint8_t)avatar_bits(element[AVATAR_ELEMENT_WEIGHTS], 8 * k, 8);
			bones[k] = (uint8_t)avatar_bits(element[AVATAR_ELEMENT_BONES], 8 * k, 8);
		}
}

// ---- Meshes (section 3.3) ----
//
// A mesh: u32 vertex count, then a vertex format; the vertices, each its position and elements then its texture
// coordinates as 16-bit halves; padded to a byte, an index header (u32 count, u16 0, u16 width) and the indices,
// `width` bits each; padded to a byte. Each mesh opens with the quantisation step, so meshes are found by its bytes.
enum {
	MESH_FORMAT_AT = 4,      // after the u32 vertex count
	MAX_VERTICES = 65535,    // indices are 16-bit
	MAX_UV_HALVES = 12,      // the Head's two halves have 6 sets of 2
	HALF_BITS = 16,
	INDEX_HEADER_BYTES = 8,
	INDEX_ZERO_AT = 4,
	INDEX_WIDTH_AT = 6,
};

// What a mesh's layout turned out to be: the texture coordinate halves per vertex and where the indices are.
typedef struct {
	AvatarVertexFormat format;
	uint32_t vertex_count;
	uint32_t halves;
	uint32_t vertex_bits; // the format's, plus the halves
	uint32_t index_width;
	uint32_t index_count;
	uint64_t first_index; // a bit
} MeshLayout;

static void free_mesh(XsdkAvatarMesh *mesh)
{
	free(mesh->positions);
	free(mesh->normals);
	free(mesh->colors);
	free(mesh->bones);
	free(mesh->weights);
	free(mesh->uvs);
	free(mesh->indices);
	memset(mesh, 0, sizeof *mesh);
}

// The first byte at or after `from` where a mesh's step value starts.
static int find_step(const uint8_t *data, uint32_t bytes, uint32_t from, uint32_t *at)
{
	for (uint32_t p = from; p + 4 <= bytes; p++) {
		if (avatar_le32(data + p) == AVATAR_QUANTISATION_STEP_BITS) {
			*at = p;
			return XSDK_AVATAR_OK;
		}
	}
	return XSDK_AVATAR_ERROR_FORMAT;
}

// The index width a mesh of vertex_count vertices uses: enough bits to number them, at least 1.
static uint32_t index_bits(uint32_t vertex_count)
{
	uint32_t width = 1;
	while ((1u << width) < vertex_count)
		width++;
	return width;
}

// The texture coordinate count isn't stored: it is the one (in halves, 0 to 12) after which a valid index header
// follows. Exactly one must fit.
static int find_halves(const uint8_t *data, uint32_t bytes, MeshLayout *m)
{
	bool found = false;
	for (uint32_t h = 0; h <= MAX_UV_HALVES; h++) {
		const uint32_t vertex_bits = m->format.bits + HALF_BITS * h;
		const uint64_t header = (m->format.first_vertex + (uint64_t)vertex_bits * m->vertex_count + 7) / 8;
		if (header + INDEX_HEADER_BYTES > bytes)
			break;
		const uint32_t count = avatar_le32(data + header);
		const uint32_t zero = avatar_le16(data + header + INDEX_ZERO_AT);
		const uint32_t width = avatar_le16(data + header + INDEX_WIDTH_AT);
		const uint64_t first_index = (header + INDEX_HEADER_BYTES) * 8;
		if (zero == 0 && width == m->index_width && count % 3 == 0 && count > 0 &&
			first_index + (uint64_t)count * width <= (uint64_t)bytes * 8) {
			if (found)
				return XSDK_AVATAR_ERROR_FORMAT; // ambiguous
			found = true;
			m->halves = h;
			m->vertex_bits = vertex_bits;
			m->index_count = count;
			m->first_index = first_index;
		}
	}
	return found && m->halves % 2 == 0 ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_FORMAT;
}

static int read_mesh_layout(const uint8_t *data, uint32_t bytes, uint32_t q, MeshLayout *m)
{
	if (q + MESH_FORMAT_AT > bytes)
		return XSDK_AVATAR_ERROR_FORMAT;
	m->vertex_count = avatar_le32(data + q);
	int result = avatar_read_vertex_format(data, bytes, q + MESH_FORMAT_AT, &m->format);
	if (result == XSDK_AVATAR_OK && (m->vertex_count == 0 || m->vertex_count > MAX_VERTICES))
		result = XSDK_AVATAR_ERROR_FORMAT;
	if (result != XSDK_AVATAR_OK)
		return result;
	m->index_width = index_bits(m->vertex_count);
	return find_halves(data, bytes, m);
}

// A mesh that stores no per-vertex weights or bones still names them in the bases: the Head, hair, hats, glasses
// and earrings weight 255 on bone 19 (Head), rings on a ring finger (section 3.3).
static bool is_skinned(const AvatarVertexFormat *f)
{
	return f->width[AVATAR_ELEMENT_WEIGHTS] > 0 || f->width[AVATAR_ELEMENT_BONES] > 0 ||
		f->base[AVATAR_ELEMENT_WEIGHTS] != 0;
}

static int allocate_mesh(XsdkAvatarMesh *mesh, const MeshLayout *m, bool skinned)
{
	const uint32_t n = m->vertex_count;
	mesh->vertex_count = n;
	mesh->uv_sets = m->halves / 2;
	mesh->index_count = m->index_count;
	mesh->positions = avatar_malloc(sizeof(float) * 3 * n, "a mesh");
	mesh->normals = avatar_malloc(sizeof(float) * 3 * n, "a mesh");
	mesh->colors = avatar_malloc(sizeof(uint32_t) * n, "a mesh");
	mesh->uvs = avatar_malloc(sizeof(float) * (2 * mesh->uv_sets * n + 1), "a mesh");
	mesh->indices = avatar_malloc(sizeof(uint16_t) * m->index_count, "a mesh");
	if (skinned) {
		mesh->bones = avatar_malloc(4 * n, "a mesh");
		mesh->weights = avatar_malloc(4 * n, "a mesh");
	}
	const bool all = mesh->positions && mesh->normals && mesh->colors && mesh->uvs && mesh->indices &&
		(!skinned || (mesh->bones && mesh->weights));
	return all ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_MEMORY;
}

static void decode_vertices(const uint8_t *data, const MeshLayout *m, XsdkAvatarMesh *mesh)
{
	for (uint32_t i = 0; i < m->vertex_count; i++) {
		uint64_t p = m->format.first_vertex + (uint64_t)m->vertex_bits * i;
		avatar_decode_vertex(data, &p, &m->format, mesh->positions + 3 * i, mesh->normals + 3 * i, &mesh->colors[i],
			mesh->bones ? mesh->weights + 4 * i : NULL, mesh->bones ? mesh->bones + 4 * i : NULL);
		for (uint32_t h = 0; h < m->halves; h++, p += HALF_BITS)
			mesh->uvs[m->halves * i + h] = avatar_half_to_float(avatar_stream_bits(data, p, HALF_BITS));
	}
}

static int decode_indices(const uint8_t *data, const MeshLayout *m, XsdkAvatarMesh *mesh)
{
	for (uint32_t k = 0; k < m->index_count; k++) {
		const uint32_t index = avatar_stream_bits(data, m->first_index + (uint64_t)m->index_width * k, m->index_width);
		if (index >= m->vertex_count)
			return XSDK_AVATAR_ERROR_FORMAT;
		mesh->indices[k] = (uint16_t)index;
	}
	return XSDK_AVATAR_OK;
}

// Decodes the mesh whose vertex count is at byte q; *end gets the byte after it.
static int decode_mesh(const uint8_t *data, uint32_t bytes, uint32_t q, XsdkAvatarMesh *mesh, uint32_t *end)
{
	MeshLayout m;
	int result = read_mesh_layout(data, bytes, q, &m);
	if (result == XSDK_AVATAR_OK)
		result = allocate_mesh(mesh, &m, is_skinned(&m.format));
	if (result == XSDK_AVATAR_OK) {
		decode_vertices(data, &m, mesh);
		result = decode_indices(data, &m, mesh);
	}
	if (result != XSDK_AVATAR_OK) {
		free_mesh(mesh);
		return result;
	}
	*end = (uint32_t)((m.first_index + (uint64_t)m.index_width * m.index_count + 7) / 8);
	return XSDK_AVATAR_OK;
}

// ---- The texture table (section 3.5) ----
//
// After the last mesh: per texture u32 its offset in the model's texture memory, u32 its allocated size, a texture
// header (section 3.4), then its pixels unless it is an empty slot.
enum { TEXTURE_ENTRY_PREFIX_BYTES = 8 };

static int decode_textures(XsdkAvatarModel *model, const uint8_t *data, uint32_t bytes, uint32_t at, uint32_t count)
{
	if (count > MAX_TEXTURES)
		return XSDK_AVATAR_ERROR_FORMAT;
	model->textures = avatar_calloc(count ? count : 1, sizeof(XsdkAvatarTexture), "a model's textures");
	if (!model->textures)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t t = 0; t < count; t++) {
		if (at > bytes || bytes - at < TEXTURE_ENTRY_PREFIX_BYTES + AVATAR_TEXTURE_HEADER_BYTES)
			return XSDK_AVATAR_ERROR_FORMAT;
		XsdkAvatarTexture *texture = &model->textures[t];
		bool empty;
		int result = avatar_parse_texture_header(data + at + TEXTURE_ENTRY_PREFIX_BYTES, texture, &empty);
		if (result != XSDK_AVATAR_OK)
			return result;
		model->texture_count = t + 1;
		at += TEXTURE_ENTRY_PREFIX_BYTES + AVATAR_TEXTURE_HEADER_BYTES;
		if (!empty) {
			result = avatar_copy_texture_pixels(texture, data, bytes, at);
			if (result != XSDK_AVATAR_OK)
				return result;
			at += texture->frame_count * texture->frame_bytes;
		}
	}
	return XSDK_AVATAR_OK;
}

// ---- Materials (section 3.6) ----
//
// A model is a series of parts, each a 33-byte header (8 words and a byte; the first word the technique), its
// material records, then its meshes. A record is 24 bytes: u32 type, u32 parameter id, 16 bytes of value.
enum {
	PART_HEADER_BYTES = 33,
	PART_TECHNIQUE_AT = 0,
	RECORD_BYTES = 24,
	RECORD_TYPE_AT = 0,
	RECORD_ID_AT = 4,
	RECORD_VALUE_AT = 8,
	RECORD_TEXTURE = 1, // u16 texture index, u16 UV set, u32 address mode, 8 zero bytes
	RECORD_FLOAT4 = 3,
	BINDING_TEXTURE_AT = RECORD_VALUE_AT,
	BINDING_UV_SET_AT = RECORD_VALUE_AT + 2,
	BINDING_ADDRESS_AT = RECORD_VALUE_AT + 4,
};

// One part's material: its header at byte `header`, its records up to byte `end`, where its first mesh starts.
static int decode_material(XsdkAvatarModel *model, XsdkAvatarMaterial *material, const uint8_t *data, uint32_t header,
	uint32_t end)
{
	const uint32_t start = header + PART_HEADER_BYTES;
	if (end < start || (end - start) % RECORD_BYTES)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t count = (end - start) / RECORD_BYTES;
	material->technique = avatar_le32(data + header + PART_TECHNIQUE_AT);
	material->parameters = avatar_calloc(count ? count : 1, sizeof(XsdkAvatarParameter), "a material");
	if (!material->parameters)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *r = data + start + RECORD_BYTES * i;
		XsdkAvatarParameter *p = &material->parameters[i];
		const uint32_t type = avatar_le32(r + RECORD_TYPE_AT);
		p->id = avatar_le32(r + RECORD_ID_AT);
		if (type == RECORD_TEXTURE) {
			p->is_texture = true;
			p->texture = (uint16_t)avatar_le16(r + BINDING_TEXTURE_AT);
			p->uv_set = (uint16_t)avatar_le16(r + BINDING_UV_SET_AT);
			p->address = avatar_le32(r + BINDING_ADDRESS_AT);
			if (p->texture >= model->texture_count)
				return XSDK_AVATAR_ERROR_FORMAT;
		} else if (type == RECORD_FLOAT4) {
			for (uint32_t k = 0; k < 4; k++)
				p->value[k] = avatar_float(avatar_le32(r + RECORD_VALUE_AT + 4 * k));
		} else {
			return XSDK_AVATAR_ERROR_UNSUPPORTED;
		}
	}
	material->parameter_count = count;
	return XSDK_AVATAR_OK;
}

// ---- The colour table (block 7, section 3.1) ----
//
// u32 count, then per entry three colours of B, G, R, A and a u32 not yet understood. Only that layout is read; a
// block of another size (3 tables in the pack) is left out.
enum {
	COLOR_BYTES = 8,
	COLOR_ENTRY_BYTES = 3 * COLOR_BYTES,
	MAX_COLOR_ENTRIES = 64,
};

static int decode_colors(XsdkAvatarModel *model, const uint8_t *strb, uint32_t size_bytes)
{
	const uint8_t *b;
	uint32_t bytes;
	if (avatar_find_block(strb, size_bytes, AVATAR_BLOCK_COLORS, &b, &bytes) != XSDK_AVATAR_OK || bytes < 4)
		return XSDK_AVATAR_OK;
	const uint32_t count = avatar_le32(b);
	if (count == 0 || count > MAX_COLOR_ENTRIES || 4 + COLOR_ENTRY_BYTES * count != bytes)
		return XSDK_AVATAR_OK;
	model->color_entries = avatar_malloc(sizeof *model->color_entries * count, "a colour table");
	if (!model->color_entries)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t e = 0; e < count; e++)
		for (uint32_t c = 0; c < 3; c++) // B, G, R, A read little-endian is ARGB
			model->color_entries[e][c] = avatar_le32(b + 4 + COLOR_ENTRY_BYTES * e + COLOR_BYTES * c);
	model->color_entry_count = count;
	return XSDK_AVATAR_OK;
}

// ---- The model ----

// Where each part's header starts and its material ends (where its first mesh begins).
typedef struct {
	uint32_t count;
	uint32_t headers[MAX_MESHES];
	uint32_t ends[MAX_MESHES];
} Parts;

// Finds and decodes the meshes in order, noting the parts: the first part's header follows the model header; a
// later part's fills a gap before its first mesh. *end gets the byte after the last mesh.
static int decode_meshes(XsdkAvatarModel *model, const uint8_t *data, uint32_t bytes, uint32_t mesh_count, Parts *parts,
	uint32_t *end, uint32_t *total_indices)
{
	uint32_t from = 0;
	for (uint32_t m = 0; m < mesh_count; m++) {
		uint32_t step_at;
		int result = find_step(data, bytes, from + MESH_FORMAT_AT, &step_at);
		if (result == XSDK_AVATAR_OK && step_at < MESH_FORMAT_AT)
			result = XSDK_AVATAR_ERROR_FORMAT;
		if (result != XSDK_AVATAR_OK)
			return result;
		const uint32_t mesh_at = step_at - MESH_FORMAT_AT;
		if (m == 0 || mesh_at > from) {
			parts->headers[parts->count] = m == 0 ? FIRST_PART_AT : from;
			parts->ends[parts->count++] = mesh_at;
		}
		model->meshes[m].material = parts->count - 1;
		result = decode_mesh(data, bytes, mesh_at, &model->meshes[m], &from);
		if (result != XSDK_AVATAR_OK)
			return result;
		model->mesh_count = m + 1;
		*total_indices += model->meshes[m].index_count;
	}
	*end = from;
	return XSDK_AVATAR_OK;
}

// The texture table, the parts' materials and the colour table, after the meshes.
static int decode_after_meshes(XsdkAvatarModel *model, const uint8_t *data, uint32_t bytes, const Parts *parts,
	uint32_t meshes_end, const uint8_t *strb, uint32_t size_bytes)
{
	if (bytes < MODEL_TEXTURE_COUNT_AT + 4)
		return XSDK_AVATAR_ERROR_FORMAT;
	int result = decode_textures(model, data, bytes, meshes_end, avatar_le32(data + MODEL_TEXTURE_COUNT_AT));
	if (result != XSDK_AVATAR_OK)
		return result;
	model->materials = avatar_calloc(parts->count, sizeof(XsdkAvatarMaterial), "a model's materials");
	if (!model->materials)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t p = 0; p < parts->count; p++) {
		result = decode_material(model, &model->materials[p], data, parts->headers[p], parts->ends[p]);
		model->material_count = p + 1;
		if (result != XSDK_AVATAR_OK)
			return result;
	}
	return decode_colors(model, strb, size_bytes);
}

int xsdk_avatar_model_decode(XsdkAvatarModel *model, const void *strb, uint32_t size_bytes)
{
	memset(model, 0, sizeof *model);
	const uint8_t *block;
	uint32_t block_bytes;
	int result = avatar_find_block(strb, size_bytes, AVATAR_BLOCK_MODEL, &block, &block_bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint8_t *data;
	uint32_t bytes;
	result = avatar_expand_block(block, block_bytes, &data, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint32_t mesh_count = 0, index_bytes = 0, total_indices = 0, meshes_end = 0;
	Parts parts = { 0 };
	if (bytes < MODEL_MESH_COUNT_AT + 4)
		result = XSDK_AVATAR_ERROR_FORMAT;
	if (result == XSDK_AVATAR_OK) {
		mesh_count = avatar_le32(data + MODEL_MESH_COUNT_AT);
		index_bytes = avatar_le32(data + MODEL_INDEX_BYTES_AT);
		if (mesh_count > MAX_MESHES)
			result = XSDK_AVATAR_ERROR_FORMAT;
	}
	if (result == XSDK_AVATAR_OK) {
		model->meshes = avatar_calloc(mesh_count ? mesh_count : 1, sizeof(XsdkAvatarMesh), "a model's meshes");
		if (!model->meshes)
			result = XSDK_AVATAR_ERROR_MEMORY;
	}
	if (result == XSDK_AVATAR_OK)
		result = decode_meshes(model, data, bytes, mesh_count, &parts, &meshes_end, &total_indices);
	// A model with no meshes (Bald) has no material to read.
	if (result == XSDK_AVATAR_OK && mesh_count > 0)
		result = decode_after_meshes(model, data, bytes, &parts, meshes_end, strb, size_bytes);
	if (result == XSDK_AVATAR_OK && total_indices * 2 != index_bytes)
		result = XSDK_AVATAR_ERROR_FORMAT;
	free(data);
	if (result != XSDK_AVATAR_OK)
		xsdk_avatar_model_free(model);
	return result;
}

void xsdk_avatar_model_free(XsdkAvatarModel *model)
{
	for (uint32_t m = 0; m < model->mesh_count; m++)
		free_mesh(&model->meshes[m]);
	free(model->meshes);
	if (model->textures)
		for (uint32_t t = 0; t < model->texture_count; t++)
			free(model->textures[t].data);
	free(model->textures);
	if (model->materials)
		for (uint32_t p = 0; p < model->material_count; p++)
			free(model->materials[p].parameters);
	free(model->materials);
	free(model->color_entries);
	memset(model, 0, sizeof *model);
}

const XsdkAvatarParameter *xsdk_avatar_material_parameter(const XsdkAvatarMaterial *material, uint32_t id)
{
	for (uint32_t i = 0; i < material->parameter_count; i++)
		if (material->parameters[i].id == id)
			return &material->parameters[i];
	return NULL;
}
