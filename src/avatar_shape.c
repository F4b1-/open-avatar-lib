// Shape overrides (FORMATS.md, section 3.7): STRB block 4, how a garment or a face shape changes another model (the
// body under a garment, the Head for a face shape): the target's triangles to hide, then its vertices to replace.
// Not compressed; little-endian. Builds for the PC.

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

// The block's two parts open with the same kind of header: u32 count, u32 a size of the target's (its index bytes,
// then its vertex bytes), the target's stock id (u32 mask, u16 record, u16 gender, the stock tail), u32 0, u32 the
// width in bits of each entry's number.
enum {
	PART_COUNT_AT = 0,
	PART_TARGET_SIZE_AT = 4,
	PART_TARGET_RECORD_AT = 12, // the id's record (u16)
	PART_STOCK_TAIL_AT = 16,
	PART_ZERO_AT = 24,
	PART_WIDTH_AT = 28,
	PART_HEADER_BYTES = 32,
	MAX_ENTRIES = 0xFFFF,       // the targets' triangle and vertex numbers fit in 16 bits
	MAX_HIDDEN_WIDTH = 16,
	MAX_OFFSET_WIDTH = 32,
};

// The vertex overrides' header is a part header followed by a mesh's float header (f32 step, f32 min x, y, z), then
// a mesh's vertex format's bit stream; each entry is `width` bits of byte offset into the target's unpacked vertices,
// then a vertex as a mesh packs one, without texture coordinates.
enum {
	OVERRIDES_FORMAT_AT = PART_HEADER_BYTES,
	OVERRIDES_HEADER_BYTES = 48,
};

// The target's unpacked vertex (section 3.7): position 12 bytes, normal, weights, bone indices and colour 4 each,
// then 4 per texture coordinate set (two halves).
enum {
	UNPACKED_VERTEX_BYTES = 28,
	UNPACKED_UV_SET_BYTES = 4,
};

static const uint8_t stock_tail[8] = { 0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0 };

static int allocate_overrides(XsdkAvatarShape *shape, uint32_t count)
{
	const uint32_t n = count ? count : 1;
	shape->override_offsets = avatar_malloc(sizeof(uint32_t) * n, "a shape's overrides");
	shape->positions = avatar_malloc(sizeof(float) * 3 * n, "a shape's overrides");
	shape->normals = avatar_malloc(sizeof(float) * 3 * n, "a shape's overrides");
	shape->colors = avatar_malloc(sizeof(uint32_t) * n, "a shape's overrides");
	shape->bones = avatar_malloc(4 * n, "a shape's overrides");
	shape->weights = avatar_malloc(4 * n, "a shape's overrides");
	const bool all = shape->override_offsets && shape->positions && shape->normals && shape->colors && shape->bones &&
		shape->weights;
	return all ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_MEMORY;
}

// The vertex overrides after the hidden triangles, from byte `at` of the block.
static int decode_overrides(XsdkAvatarShape *shape, const uint8_t *data, uint32_t bytes, uint32_t at)
{
	if (at + OVERRIDES_HEADER_BYTES > bytes || avatar_le32(data + at + PART_ZERO_AT) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t count = avatar_le32(data + at + PART_COUNT_AT);
	const uint32_t offset_width = avatar_le32(data + at + PART_WIDTH_AT);
	if (count > MAX_ENTRIES || offset_width > MAX_OFFSET_WIDTH)
		return XSDK_AVATAR_ERROR_FORMAT;
	AvatarVertexFormat format;
	int result = avatar_read_vertex_format(data, bytes, at + OVERRIDES_FORMAT_AT, &format);
	if (result != XSDK_AVATAR_OK)
		return result;
	const uint32_t entry_bits = offset_width + format.bits;
	if (format.first_vertex + (uint64_t)entry_bits * count > (uint64_t)bytes * 8)
		return XSDK_AVATAR_ERROR_FORMAT;
	result = allocate_overrides(shape, count);
	if (result != XSDK_AVATAR_OK)
		return result;
	for (uint32_t i = 0; i < count; i++) {
		uint64_t p = format.first_vertex + (uint64_t)entry_bits * i;
		shape->override_offsets[i] = avatar_stream_bits(data, p, offset_width);
		p += offset_width;
		avatar_decode_vertex(data, &p, &format, shape->positions + 3 * i, shape->normals + 3 * i, &shape->colors[i],
			shape->weights + 4 * i, shape->bones + 4 * i);
	}
	shape->override_count = count;
	return XSDK_AVATAR_OK;
}

int xsdk_avatar_shape_decode(XsdkAvatarShape *shape, const void *strb, uint32_t size_bytes)
{
	memset(shape, 0, sizeof *shape);
	const uint8_t *b;
	uint32_t bytes;
	int result = avatar_find_block(strb, size_bytes, AVATAR_BLOCK_SHAPE, &b, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	// The hidden triangles: a part header, then the triangle numbers, `width` bits each.
	if (bytes < PART_HEADER_BYTES || memcmp(b + PART_STOCK_TAIL_AT, stock_tail, sizeof stock_tail) != 0 ||
		avatar_le32(b + PART_ZERO_AT) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t count = avatar_le32(b + PART_COUNT_AT);
	const uint32_t width = avatar_le32(b + PART_WIDTH_AT);
	const uint32_t list_bytes = (uint32_t)(((uint64_t)count * width + 7) / 8);
	if (count > MAX_ENTRIES || width > MAX_HIDDEN_WIDTH || (count && !width) ||
		PART_HEADER_BYTES + (uint64_t)list_bytes > bytes)
		return XSDK_AVATAR_ERROR_FORMAT;
	shape->hidden = avatar_malloc(sizeof(uint16_t) * (count ? count : 1), "a shape's hidden triangles");
	if (!shape->hidden)
		return XSDK_AVATAR_ERROR_MEMORY;
	shape->target_record = avatar_le16(b + PART_TARGET_RECORD_AT);
	shape->target_index_bytes = avatar_le32(b + PART_TARGET_SIZE_AT);
	for (uint32_t k = 0; k < count; k++)
		shape->hidden[k] = (uint16_t)avatar_stream_bits(b, (uint64_t)PART_HEADER_BYTES * 8 + (uint64_t)k * width, width);
	shape->hidden_count = count;
	result = decode_overrides(shape, b, bytes, PART_HEADER_BYTES + list_bytes);
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
	const uint32_t stride = UNPACKED_VERTEX_BYTES + UNPACKED_UV_SET_BYTES * target->meshes[0].uv_sets;
	for (uint32_t i = 0; i < shape->override_count; i++) {
		// The override's vertex, numbered across the target's meshes in order.
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
