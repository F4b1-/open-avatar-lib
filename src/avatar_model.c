// STRB files, their model blocks (meshes, textures, material) and texture blocks (SPECS.md, sections 3.1-3.6).

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

enum {
	BLOCK_TEXTURE = 2,
	BLOCK_MODEL = 3,
	BLOCK_SHAPE = 4,
	BLOCK_COLORS = 7,
	COLOR_ENTRY_BYTES = 24,
	SHAPE_HEADER_BYTES = 32,
	OVERRIDE_HEADER_BYTES = 48,
	TEXTURE_HEADER_BYTES = 33,
	TEXTURE_ENTRY_BYTES = 8 + TEXTURE_HEADER_BYTES,
	FIRST_PART = 0x30, // the first part's header, after the model header's 12 words
	PART_HEADER_BYTES = 33,
	MATERIAL_RECORD_BYTES = 24,
	MAX_TEXTURES = 32,
	CHUNK_HEADER_BYTES = 12,
	MESH_SIGNATURE = 0x36ED4402, // the quantisation step, 7.071068e-6, opening every mesh
	MAX_UV_HALVES = 12,
	SLACK_BYTES = 8, // zeros after the expanded model, so bit reads never run off its end
};

// Per-axis scale of quantised positions, All-Stars Avatar's fit, checked by rendering (SPECS.md, section 3.3).
static const float position_scale[3] = { 2.0f, 1.6329932f, 1.7320508f };

static uint32_t round_up(uint32_t value, uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static uint32_t read_field(const uint8_t *p, uint32_t bytes, bool little_endian)
{
	uint32_t value = 0;
	for (uint32_t i = 0; i < bytes; i++)
		value |= (uint32_t)p[i] << (8 * (little_endian ? i : bytes - 1 - i));
	return value;
}

// Finds the first block with this id; its data and size (declared in avatar_internal.h).
int avatar_find_block(const uint8_t *file, uint32_t size, uint32_t id, const uint8_t **data, uint32_t *data_bytes)
{
	if (size < 0x1B || memcmp(file, "STRB", 4) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const bool alignment_stored = file[4] != 0;
	const bool little_endian = file[5] != 0;
	const uint32_t id_bytes = file[0x16];
	const uint32_t span_bytes = file[0x17];
	const uint32_t alignment = alignment_stored ? file[0x1A] : 1;
	if (!id_bytes || id_bytes > 4 || !span_bytes || span_bytes > 4 || !alignment || (alignment & (alignment - 1)))
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t header_bytes = round_up(id_bytes + 2 * span_bytes, alignment);
	uint32_t pos = round_up(alignment_stored ? 30 : 0x1A, alignment);
	while (pos + header_bytes <= size) {
		const uint32_t block_id = read_field(file + pos, id_bytes, little_endian);
		const uint32_t length = read_field(file + pos + id_bytes, span_bytes, little_endian);
		const uint32_t start = pos + header_bytes;
		if (length > size - start)
			return XSDK_AVATAR_ERROR_FORMAT;
		if (block_id == id) {
			*data = file + start;
			*data_bytes = length;
			return XSDK_AVATAR_OK;
		}
		pos = start + round_up(length, alignment);
	}
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}

// Expands a model or texture block's LZX chunks into a buffer with SLACK_BYTES of zeros after it.
static int expand(const uint8_t *block, uint32_t block_bytes, uint8_t **out, uint32_t *out_bytes)
{
	uint32_t total = 0;
	for (uint32_t p = 0; p + CHUNK_HEADER_BYTES <= block_bytes;) {
		const uint32_t packed = avatar_le32(block + p);
		const uint32_t offset = avatar_le32(block + p + 4);
		const uint32_t bytes = avatar_le32(block + p + 8);
		if (offset != total || packed > block_bytes - p - CHUNK_HEADER_BYTES || bytes > 0x8000)
			return XSDK_AVATAR_ERROR_FORMAT;
		total += bytes;
		p += CHUNK_HEADER_BYTES + packed;
	}
	uint8_t *buffer = calloc(1, total + SLACK_BYTES);
	if (!buffer)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t p = 0; p + CHUNK_HEADER_BYTES <= block_bytes;) {
		const uint32_t packed = avatar_le32(block + p);
		const uint32_t offset = avatar_le32(block + p + 4);
		const uint32_t bytes = avatar_le32(block + p + 8);
		const int result = avatar_lzx_expand(block + p + CHUNK_HEADER_BYTES, packed, buffer + offset, bytes);
		if (result != XSDK_AVATAR_OK) {
			free(buffer);
			return result;
		}
		p += CHUNK_HEADER_BYTES + packed;
	}
	*out = buffer;
	*out_bytes = total;
	return XSDK_AVATAR_OK;
}

// Up to 32 bits at a bit position, least significant bit first.
static uint32_t bits_at(const uint8_t *data, uint64_t pos, uint32_t width)
{
	if (width == 0)
		return 0;
	const uint8_t *p = data + (pos >> 3);
	uint64_t window = 0;
	for (uint32_t i = 0; i < 5; i++)
		window |= (uint64_t)p[i] << (8 * i);
	window >>= pos & 7;
	return (uint32_t)(window & ((((uint64_t)1) << width) - 1));
}

static float half_to_float(uint32_t h)
{
	const uint32_t sign = (h >> 15) & 1;
	const uint32_t exponent = (h >> 10) & 0x1F;
	const uint32_t mantissa = h & 0x3FF;
	float value;
	if (exponent == 0)
		value = ldexpf((float)mantissa, -24);
	else if (exponent == 31)
		value = mantissa ? NAN : INFINITY;
	else
		value = ldexpf((float)(mantissa | 0x400), (int)exponent - 25);
	return sign ? -value : value;
}

// 11:11:10 signed normalised, x in the low bits (SPECS.md, section 3.3).
static void unpack_normal(uint32_t v, float *out)
{
	const int32_t x = (int32_t)(v & 0x7FF);
	const int32_t y = (int32_t)((v >> 11) & 0x7FF);
	const int32_t z = (int32_t)((v >> 22) & 0x3FF);
	out[0] = (float)(x >= 1024 ? x - 2048 : x) / 1023.0f;
	out[1] = (float)(y >= 1024 ? y - 2048 : y) / 1023.0f;
	out[2] = (float)(z >= 512 ? z - 1024 : z) / 511.0f;
	const float length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (length > 0) {
		out[0] /= length;
		out[1] /= length;
		out[2] /= length;
	}
}

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

static int find_signature(const uint8_t *data, uint32_t bytes, uint32_t from, uint32_t *at)
{
	for (uint32_t p = from; p + 4 <= bytes; p++) {
		if (avatar_le32(data + p) == MESH_SIGNATURE) {
			*at = p;
			return XSDK_AVATAR_OK;
		}
	}
	return XSDK_AVATAR_ERROR_FORMAT;
}

// Decodes the mesh whose vertex count is at byte q; *end gets the byte after it.
static int decode_mesh(const uint8_t *data, uint32_t bytes, uint32_t q, XsdkAvatarMesh *mesh, uint32_t *end)
{
	if (q + 20 + 35 > bytes)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t vertex_count = avatar_le32(data + q);
	const float step = avatar_float(avatar_le32(data + q + 4));
	float min[3];
	for (uint32_t a = 0; a < 3; a++)
		min[a] = avatar_float(avatar_le32(data + q + 8 + 4 * a));
	if (vertex_count == 0 || vertex_count > 65535)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint64_t s = (uint64_t)(q + 20) * 8;
	uint32_t widths[3];
	for (uint32_t a = 0; a < 3; a++)
		widths[a] = bits_at(data, s + 6 * a, 6);
	// Base and bit width for the normal, blend weights, blend indices and colour.
	uint32_t base[4], width[4];
	for (uint32_t e = 0; e < 4; e++) {
		base[e] = bits_at(data, s + 18 + 64 * e, 32);
		width[e] = bits_at(data, s + 18 + 64 * e + 32, 32);
		if (width[e] > 32)
			return XSDK_AVATAR_ERROR_FORMAT;
	}
	const uint64_t vertex_start = s + 18 + 256;
	const uint32_t fixed = widths[0] + widths[1] + widths[2] + width[0] + width[1] + width[2] + width[3];
	uint32_t index_width = 1;
	while ((1u << index_width) < vertex_count)
		index_width++;
	if (vertex_count == 1)
		index_width = 1;

	// The texture coordinate count is the one after which a valid index header follows (SPECS.md, section 3.3).
	int32_t halves = -1;
	uint32_t record_bits = 0, index_header = 0, index_count = 0;
	for (uint32_t h = 0; h <= MAX_UV_HALVES; h++) {
		const uint32_t rec = fixed + 16 * h;
		const uint64_t header = (vertex_start + (uint64_t)rec * vertex_count + 7) / 8;
		if (header + 8 > bytes)
			break;
		const uint32_t count = avatar_le32(data + header);
		const uint32_t zero = avatar_le16(data + header + 4);
		const uint32_t w = avatar_le16(data + header + 6);
		if (zero == 0 && w == index_width && count % 3 == 0 && count > 0 &&
			(header + 8) * 8 + (uint64_t)count * w <= (uint64_t)bytes * 8) {
			if (halves >= 0)
				return XSDK_AVATAR_ERROR_FORMAT; // ambiguous
			halves = (int32_t)h;
			record_bits = rec;
			index_header = (uint32_t)header;
			index_count = count;
		}
	}
	if (halves < 0 || halves % 2)
		return XSDK_AVATAR_ERROR_FORMAT;

	// A mesh that stores no per-vertex weights or bones still names them in the bases: the Head, hair, hats, glasses
	// and earrings weight 255 on bone 19 (Head), rings on a ring finger (SPECS.md, section 3.3).
	const bool skinned = width[1] > 0 || width[2] > 0 || base[1] != 0;
	mesh->vertex_count = vertex_count;
	mesh->uv_sets = (uint32_t)halves / 2;
	mesh->index_count = index_count;
	mesh->positions = malloc(sizeof(float) * 3 * vertex_count);
	mesh->normals = malloc(sizeof(float) * 3 * vertex_count);
	mesh->colors = malloc(sizeof(uint32_t) * vertex_count);
	mesh->uvs = malloc(sizeof(float) * (2 * mesh->uv_sets * vertex_count + 1));
	mesh->indices = malloc(sizeof(uint16_t) * index_count);
	if (skinned) {
		mesh->bones = malloc(4 * vertex_count);
		mesh->weights = malloc(4 * vertex_count);
	}
	if (!mesh->positions || !mesh->normals || !mesh->colors || !mesh->uvs || !mesh->indices ||
		(skinned && (!mesh->bones || !mesh->weights))) {
		free_mesh(mesh);
		return XSDK_AVATAR_ERROR_MEMORY;
	}
	for (uint32_t i = 0; i < vertex_count; i++) {
		uint64_t p = vertex_start + (uint64_t)record_bits * i;
		for (uint32_t a = 0; a < 3; a++) {
			mesh->positions[3 * i + a] = min[a] + (float)bits_at(data, p, widths[a]) * step * position_scale[a];
			p += widths[a];
		}
		uint32_t element[4];
		for (uint32_t e = 0; e < 4; e++) {
			element[e] = base[e] + bits_at(data, p, width[e]);
			p += width[e];
		}
		unpack_normal(element[0], mesh->normals + 3 * i);
		mesh->colors[i] = element[3];
		if (skinned) {
			for (uint32_t k = 0; k < 4; k++) {
				mesh->weights[4 * i + k] = (uint8_t)(element[1] >> (8 * k));
				mesh->bones[4 * i + k] = (uint8_t)(element[2] >> (8 * k));
			}
		}
		for (uint32_t h = 0; h < (uint32_t)halves; h++, p += 16)
			mesh->uvs[(uint32_t)halves * i + h] = half_to_float(bits_at(data, p, 16));
	}
	const uint64_t index_start = (uint64_t)(index_header + 8) * 8;
	for (uint32_t k = 0; k < index_count; k++) {
		const uint32_t index = bits_at(data, index_start + (uint64_t)index_width * k, index_width);
		if (index >= vertex_count) {
			free_mesh(mesh);
			return XSDK_AVATAR_ERROR_FORMAT;
		}
		mesh->indices[k] = (uint16_t)index;
	}
	*end = (uint32_t)((index_start + (uint64_t)index_width * index_count + 7) / 8);
	return XSDK_AVATAR_OK;
}

// The 33-byte texture header (SPECS.md, section 3.4); *empty gets whether it is an empty slot (no pixels follow).
static int parse_texture_header(const uint8_t *h, XsdkAvatarTexture *texture, bool *empty)
{
	const uint32_t format = h[0] & 0x3F;
	const uint32_t endian = h[0] >> 6;
	const uint32_t width = avatar_le32(h + 4);
	const uint32_t height = avatar_le32(h + 8);
	const uint32_t frame_count = avatar_le32(h + 20);
	if ((format != XSDK_AVATAR_TEXTURE_DXT1 && format != XSDK_AVATAR_TEXTURE_DXT5) || endian != 1)
		return XSDK_AVATAR_ERROR_UNSUPPORTED;
	if (!width || !height || width > 1024 || height > 1024 || (width & 3) || (height & 3) || !frame_count ||
		frame_count > 64)
		return XSDK_AVATAR_ERROR_FORMAT;
	texture->format = format;
	texture->width = width;
	texture->height = height;
	texture->frame_count = frame_count;
	texture->frame_bytes = width * height / 16 * (format == XSDK_AVATAR_TEXTURE_DXT1 ? 8 : 16);
	texture->data = NULL;
	*empty = (avatar_le32(h + 24) & 1) != 0;
	return XSDK_AVATAR_OK;
}

// Copies frame_count * frame_bytes pixel bytes at data[at], within bytes.
static int copy_pixels(XsdkAvatarTexture *texture, const uint8_t *data, uint32_t bytes, uint32_t at)
{
	const uint64_t size = (uint64_t)texture->frame_count * texture->frame_bytes;
	if (at > bytes || size > bytes - at)
		return XSDK_AVATAR_ERROR_FORMAT;
	texture->data = malloc((size_t)size);
	if (!texture->data)
		return XSDK_AVATAR_ERROR_MEMORY;
	memcpy(texture->data, data + at, (size_t)size);
	return XSDK_AVATAR_OK;
}

// The colour table (block 7): u32 count, then per entry three colours of B, G, R, A and a u32 not yet understood
// (SPECS.md, section 3.1). Only that layout is read; a block of another size is left out.
static int decode_colors(XsdkAvatarModel *model, const uint8_t *strb, uint32_t size_bytes)
{
	const uint8_t *b;
	uint32_t bytes;
	if (avatar_find_block(strb, size_bytes, BLOCK_COLORS, &b, &bytes) != XSDK_AVATAR_OK || bytes < 4)
		return XSDK_AVATAR_OK;
	const uint32_t count = avatar_le32(b);
	if (count == 0 || count > 64 || 4 + COLOR_ENTRY_BYTES * count != bytes)
		return XSDK_AVATAR_OK;
	model->color_entries = malloc(sizeof *model->color_entries * count);
	if (!model->color_entries)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t e = 0; e < count; e++)
		for (uint32_t c = 0; c < 3; c++) {
			const uint8_t *p = b + 4 + COLOR_ENTRY_BYTES * e + 8 * c;
			model->color_entries[e][c] = (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
		}
	model->color_entry_count = count;
	return XSDK_AVATAR_OK;
}

// The texture table after the last mesh, which ends at byte at (SPECS.md, section 3.5).
static int decode_textures(XsdkAvatarModel *model, const uint8_t *data, uint32_t bytes, uint32_t at, uint32_t count)
{
	if (count > MAX_TEXTURES)
		return XSDK_AVATAR_ERROR_FORMAT;
	model->textures = calloc(count ? count : 1, sizeof(XsdkAvatarTexture));
	if (!model->textures)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t t = 0; t < count; t++) {
		if (at > bytes || bytes - at < TEXTURE_ENTRY_BYTES)
			return XSDK_AVATAR_ERROR_FORMAT;
		XsdkAvatarTexture *texture = &model->textures[t];
		bool empty;
		const int result = parse_texture_header(data + at + 8, texture, &empty);
		if (result != XSDK_AVATAR_OK)
			return result;
		model->texture_count = t + 1;
		at += TEXTURE_ENTRY_BYTES;
		if (!empty) {
			const int copied = copy_pixels(texture, data, bytes, at);
			if (copied != XSDK_AVATAR_OK)
				return copied;
			at += texture->frame_count * texture->frame_bytes;
		}
	}
	return XSDK_AVATAR_OK;
}

// The material records from 0x51 to the first mesh, which starts at byte end (SPECS.md, section 3.6).
// One part's material: its header (8 words and a byte; the first word the technique) at byte header, then records of
// 24 bytes up to byte end, where the part's first mesh starts (SPECS.md, section 3.6).
static int decode_material(XsdkAvatarModel *model, XsdkAvatarMaterial *material, const uint8_t *data, uint32_t header,
	uint32_t end)
{
	const uint32_t start = header + PART_HEADER_BYTES;
	if (end < start || (end - start) % MATERIAL_RECORD_BYTES)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t count = (end - start) / MATERIAL_RECORD_BYTES;
	material->technique = avatar_le32(data + header);
	material->parameters = calloc(count ? count : 1, sizeof(XsdkAvatarParameter));
	if (!material->parameters)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *r = data + start + MATERIAL_RECORD_BYTES * i;
		XsdkAvatarParameter *p = &material->parameters[i];
		const uint32_t type = avatar_le32(r);
		p->id = avatar_le32(r + 4);
		if (type == 1) {
			p->is_texture = true;
			p->texture = (uint16_t)avatar_le16(r + 8);
			p->uv_set = (uint16_t)avatar_le16(r + 10);
			p->address = avatar_le32(r + 12);
			if (p->texture >= model->texture_count)
				return XSDK_AVATAR_ERROR_FORMAT;
		} else if (type == 3) {
			for (uint32_t k = 0; k < 4; k++)
				p->value[k] = avatar_float(avatar_le32(r + 8 + 4 * k));
		} else {
			return XSDK_AVATAR_ERROR_UNSUPPORTED;
		}
	}
	material->parameter_count = count;
	return XSDK_AVATAR_OK;
}

int xsdk_avatar_model_decode(XsdkAvatarModel *model, const void *strb, uint32_t size_bytes)
{
	memset(model, 0, sizeof *model);
	const uint8_t *block;
	uint32_t block_bytes;
	int result = avatar_find_block(strb, size_bytes, BLOCK_MODEL, &block, &block_bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint8_t *data;
	uint32_t bytes;
	result = expand(block, block_bytes, &data, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	if (bytes < 24) {
		free(data);
		return XSDK_AVATAR_ERROR_FORMAT;
	}
	// Header word 5: the mesh count; word 4: the index bytes as 16-bit indices (SPECS.md, section 3.3).
	const uint32_t mesh_count = avatar_le32(data + 20);
	const uint32_t index_bytes = avatar_le32(data + 16);
	if (mesh_count > 64) {
		free(data);
		return XSDK_AVATAR_ERROR_FORMAT;
	}
	model->meshes = calloc(mesh_count ? mesh_count : 1, sizeof(XsdkAvatarMesh));
	if (!model->meshes) {
		free(data);
		return XSDK_AVATAR_ERROR_MEMORY;
	}
	uint32_t from = 0;
	uint32_t total_indices = 0;
	// Where each part's header starts and its first mesh begins (SPECS.md, section 3.6): the first part's header follows
	// the model header; a later part's fills the gap before its first mesh.
	uint32_t part_headers[64], part_ends[64], part_count = 0;
	for (uint32_t m = 0; m < mesh_count; m++) {
		uint32_t at;
		result = find_signature(data, bytes, from + 4, &at);
		if (result == XSDK_AVATAR_OK && at < 4)
			result = XSDK_AVATAR_ERROR_FORMAT;
		if (result == XSDK_AVATAR_OK && (m == 0 || at - 4 > from)) {
			part_headers[part_count] = m == 0 ? FIRST_PART : from;
			part_ends[part_count++] = at - 4;
		}
		if (result == XSDK_AVATAR_OK) {
			model->meshes[m].material = part_count - 1;
			result = decode_mesh(data, bytes, at - 4, &model->meshes[m], &from);
		}
		if (result != XSDK_AVATAR_OK) {
			model->mesh_count = m;
			xsdk_avatar_model_free(model);
			free(data);
			return result;
		}
		model->mesh_count = m + 1;
		total_indices += model->meshes[m].index_count;
	}
	// Header word 6: the texture count (SPECS.md, section 3.5). A model with no meshes has no material to read.
	if (mesh_count > 0) {
		result = bytes >= 28 ? decode_textures(model, data, bytes, from, avatar_le32(data + 24))
							 : XSDK_AVATAR_ERROR_FORMAT;
		model->materials = calloc(part_count, sizeof(XsdkAvatarMaterial));
		if (result == XSDK_AVATAR_OK && !model->materials)
			result = XSDK_AVATAR_ERROR_MEMORY;
		for (uint32_t p = 0; result == XSDK_AVATAR_OK && p < part_count; p++) {
			result = decode_material(model, &model->materials[p], data, part_headers[p], part_ends[p]);
			model->material_count = p + 1;
		}
		if (result == XSDK_AVATAR_OK)
			result = decode_colors(model, strb, size_bytes);
		if (result != XSDK_AVATAR_OK) {
			xsdk_avatar_model_free(model);
			free(data);
			return result;
		}
	}
	free(data);
	if (total_indices * 2 != index_bytes) {
		xsdk_avatar_model_free(model);
		return XSDK_AVATAR_ERROR_FORMAT;
	}
	return XSDK_AVATAR_OK;
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

// The vertex overrides after the hiding list, from byte at of the block (SPECS.md, section 3.7): u32 count, u32 the
// target's vertex bytes, its stock id, u32 0, u32 offset width, then a mesh's quantisation header and bit stream,
// each entry a byte offset into the target's unpacked vertices and then a whole vertex as a mesh's.
static int decode_overrides(XsdkAvatarShape *shape, const uint8_t *data, uint32_t bytes, uint32_t at)
{
	if (at + OVERRIDE_HEADER_BYTES > bytes || avatar_le32(data + at + 24) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t count = avatar_le32(data + at);
	const uint32_t offset_width = avatar_le32(data + at + 28);
	if (count > 0xFFFF || offset_width > 32)
		return XSDK_AVATAR_ERROR_FORMAT;
	const float step = avatar_float(avatar_le32(data + at + 32));
	float min[3];
	for (uint32_t a = 0; a < 3; a++)
		min[a] = avatar_float(avatar_le32(data + at + 36 + 4 * a));
	const uint64_t s = (uint64_t)(at + OVERRIDE_HEADER_BYTES) * 8;
	uint32_t widths[3], base[4], width[4], record = offset_width;
	for (uint32_t a = 0; a < 3; a++) {
		widths[a] = bits_at(data, s + 6 * a, 6);
		record += widths[a];
	}
	for (uint32_t e = 0; e < 4; e++) {
		base[e] = bits_at(data, s + 18 + 64 * e, 32);
		width[e] = bits_at(data, s + 18 + 64 * e + 32, 32);
		if (width[e] > 32)
			return XSDK_AVATAR_ERROR_FORMAT;
		record += width[e];
	}
	const uint64_t first = s + 18 + 256;
	if (first + (uint64_t)record * count > (uint64_t)bytes * 8)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t n = count ? count : 1;
	shape->override_offsets = malloc(sizeof(uint32_t) * n);
	shape->positions = malloc(sizeof(float) * 3 * n);
	shape->normals = malloc(sizeof(float) * 3 * n);
	shape->colors = malloc(sizeof(uint32_t) * n);
	shape->bones = malloc(4 * n);
	shape->weights = malloc(4 * n);
	if (!shape->override_offsets || !shape->positions || !shape->normals || !shape->colors || !shape->bones ||
		!shape->weights)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t i = 0; i < count; i++) {
		uint64_t p = first + (uint64_t)record * i;
		shape->override_offsets[i] = bits_at(data, p, offset_width);
		p += offset_width;
		for (uint32_t a = 0; a < 3; a++) {
			shape->positions[3 * i + a] = min[a] + (float)bits_at(data, p, widths[a]) * step * position_scale[a];
			p += widths[a];
		}
		uint32_t element[4];
		for (uint32_t e = 0; e < 4; e++) {
			element[e] = base[e] + bits_at(data, p, width[e]);
			p += width[e];
		}
		unpack_normal(element[0], shape->normals + 3 * i);
		shape->colors[i] = element[3];
		for (uint32_t k = 0; k < 4; k++) {
			shape->weights[4 * i + k] = (uint8_t)(element[1] >> (8 * k));
			shape->bones[4 * i + k] = (uint8_t)(element[2] >> (8 * k));
		}
	}
	shape->override_count = count;
	return XSDK_AVATAR_OK;
}

int xsdk_avatar_shape_decode(XsdkAvatarShape *shape, const void *strb, uint32_t size_bytes)
{
	static const uint8_t stock_tail[8] = { 0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0 };
	memset(shape, 0, sizeof *shape);
	const uint8_t *b;
	uint32_t bytes;
	int result = avatar_find_block(strb, size_bytes, BLOCK_SHAPE, &b, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	// u32 count, u32 the target's index bytes, the target's stock id (little-endian), u32 0, u32 bit width, then the
	// triangle numbers, LSB first, then the vertex overrides (SPECS.md, section 3.7). Not compressed.
	if (bytes < SHAPE_HEADER_BYTES || memcmp(b + 16, stock_tail, sizeof stock_tail) != 0 || avatar_le32(b + 24) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t count = avatar_le32(b);
	const uint32_t width = avatar_le32(b + 28);
	if (count > 0xFFFF || width > 16 || (count && !width) ||
		SHAPE_HEADER_BYTES + ((uint64_t)count * width + 7) / 8 > bytes)
		return XSDK_AVATAR_ERROR_FORMAT;
	// bits_at reads up to 5 bytes past a position: the block copied with slack.
	uint8_t *data = calloc(1, bytes + SLACK_BYTES);
	shape->hidden = malloc(sizeof(uint16_t) * (count ? count : 1));
	if (!data || !shape->hidden) {
		free(data);
		xsdk_avatar_shape_free(shape);
		return XSDK_AVATAR_ERROR_MEMORY;
	}
	memcpy(data, b, bytes);
	shape->target_record = avatar_le16(b + 12);
	shape->target_index_bytes = avatar_le32(b + 4);
	for (uint32_t k = 0; k < count; k++)
		shape->hidden[k] = (uint16_t)bits_at(data, (uint64_t)SHAPE_HEADER_BYTES * 8 + (uint64_t)k * width, width);
	shape->hidden_count = count;
	result = decode_overrides(shape, data, bytes, SHAPE_HEADER_BYTES + (uint32_t)(((uint64_t)count * width + 7) / 8));
	free(data);
	if (result != XSDK_AVATAR_OK)
		xsdk_avatar_shape_free(shape);
	return result;
}

int xsdk_avatar_shape_apply(const XsdkAvatarShape *shape, XsdkAvatarModel *target)
{
	uint32_t index_bytes = 0, vertex_count = 0;
	for (uint32_t m = 0; m < target->mesh_count; m++) {
		index_bytes += 2 * target->meshes[m].index_count;
		vertex_count += target->meshes[m].vertex_count;
	}
	if (!target->mesh_count || index_bytes != shape->target_index_bytes)
		return XSDK_AVATAR_ERROR_FORMAT;
	// The unpacked vertex: position 12 bytes, then normal, weights, bone indices and colour 4 each, then the texture
	// coordinates as halves (SPECS.md, section 3.7).
	const uint32_t stride = 28 + 4 * target->meshes[0].uv_sets;
	for (uint32_t i = 0; i < shape->override_count; i++) {
		uint32_t v = shape->override_offsets[i] / stride;
		if (shape->override_offsets[i] % stride || v >= vertex_count)
			return XSDK_AVATAR_ERROR_FORMAT;
		XsdkAvatarMesh *mesh = target->meshes;
		while (v >= mesh->vertex_count) {
			v -= mesh->vertex_count;
			mesh++;
		}
		memcpy(mesh->positions + 3 * v, shape->positions + 3 * i, sizeof(float) * 3);
		memcpy(mesh->normals + 3 * v, shape->normals + 3 * i, sizeof(float) * 3);
		mesh->colors[v] = shape->colors[i];
		if (mesh->bones) {
			memcpy(mesh->bones + 4 * v, shape->bones + 4 * i, 4);
			memcpy(mesh->weights + 4 * v, shape->weights + 4 * i, 4);
		}
	}
	return XSDK_AVATAR_OK;
}

void xsdk_avatar_shape_free(XsdkAvatarShape *shape)
{
	free(shape->hidden);
	free(shape->override_offsets);
	free(shape->positions);
	free(shape->normals);
	free(shape->colors);
	free(shape->bones);
	free(shape->weights);
	memset(shape, 0, sizeof *shape);
}

int xsdk_avatar_texture_decode(XsdkAvatarTexture *texture, const void *strb, uint32_t size_bytes)
{
	memset(texture, 0, sizeof *texture);
	const uint8_t *block;
	uint32_t block_bytes;
	int result = avatar_find_block(strb, size_bytes, BLOCK_TEXTURE, &block, &block_bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint8_t *data;
	uint32_t bytes;
	result = expand(block, block_bytes, &data, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	bool empty = true;
	result = bytes >= TEXTURE_HEADER_BYTES ? parse_texture_header(data, texture, &empty) : XSDK_AVATAR_ERROR_FORMAT;
	if (result == XSDK_AVATAR_OK)
		result = empty ? XSDK_AVATAR_ERROR_FORMAT : copy_pixels(texture, data, bytes, TEXTURE_HEADER_BYTES);
	free(data);
	if (result != XSDK_AVATAR_OK)
		xsdk_avatar_texture_free(texture);
	return result;
}

void xsdk_avatar_texture_free(XsdkAvatarTexture *texture)
{
	free(texture->data);
	memset(texture, 0, sizeof *texture);
}
