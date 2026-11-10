// STRB files (SPECS.md, section 3.1): the container every avatar asset is, a list of blocks by id; and the LZX chunks
// model and texture blocks are compressed in (section 3.2). Builds for the PC.

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

// The file header (section 3.1).
enum {
	ALIGNMENT_STORED_AT = 0x04, // nonzero: the block alignment is at ALIGNMENT_AT
	LITTLE_ENDIAN_AT = 0x05,    // nonzero: block headers are little-endian (big-endian in every file seen)
	ID_BYTES_AT = 0x16,         // the size of a block header's id field
	SPAN_BYTES_AT = 0x17,       // the size of its length fields
	ALIGNMENT_AT = 0x1A,
	HEADER_BYTES = 0x1B,
	FIRST_BLOCK_AT = 0x1A,           // where blocks start, rounded up to the alignment,
	FIRST_BLOCK_AT_ALIGNED = 0x1E,   // or this when the alignment is stored
	MAX_FIELD_BYTES = 4,
};

// A compressed block's chunk header (section 3.2), little-endian.
enum {
	CHUNK_PACKED_AT = 0,  // u32 compressed bytes, after this header
	CHUNK_OFFSET_AT = 4,  // u32 where its output goes
	CHUNK_BYTES_AT = 8,   // u32 its output's size
	CHUNK_HEADER_BYTES = 12,
	MAX_CHUNK_BYTES = 0x8000, // the LZX window: a chunk expands to at most 32 KB
};

// A block header field of `bytes` bytes (1 to 4) in the file's byte order.
static uint32_t read_field(const uint8_t *p, uint32_t bytes, bool little_endian)
{
	uint32_t value = 0;
	for (uint32_t i = 0; i < bytes; i++)
		value |= (uint32_t)p[i] << (8 * (little_endian ? i : bytes - 1 - i));
	return value;
}

int avatar_find_block(const uint8_t *file, uint32_t size, uint32_t id, const uint8_t **data, uint32_t *data_bytes)
{
	if (size < HEADER_BYTES || memcmp(file, "STRB", 4) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const bool alignment_stored = file[ALIGNMENT_STORED_AT] != 0;
	const bool little_endian = file[LITTLE_ENDIAN_AT] != 0;
	const uint32_t id_bytes = file[ID_BYTES_AT];
	const uint32_t span_bytes = file[SPAN_BYTES_AT];
	const uint32_t alignment = alignment_stored ? file[ALIGNMENT_AT] : 1;
	if (!id_bytes || id_bytes > MAX_FIELD_BYTES || !span_bytes || span_bytes > MAX_FIELD_BYTES || !alignment ||
		(alignment & (alignment - 1)))
		return XSDK_AVATAR_ERROR_FORMAT;
	// A block header: the id, the data's length, the field size; padded to the alignment.
	const uint32_t header_bytes = avatar_round_up(id_bytes + 2 * span_bytes, alignment);
	uint32_t pos = avatar_round_up(alignment_stored ? FIRST_BLOCK_AT_ALIGNED : FIRST_BLOCK_AT, alignment);
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
		pos = start + avatar_round_up(length, alignment);
	}
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}

// The size of a compressed block's output, checking its chunks follow on from each other.
static int expanded_size(const uint8_t *block, uint32_t block_bytes, uint32_t *total)
{
	*total = 0;
	for (uint32_t p = 0; p + CHUNK_HEADER_BYTES <= block_bytes;) {
		const uint32_t packed = avatar_le32(block + p + CHUNK_PACKED_AT);
		const uint32_t offset = avatar_le32(block + p + CHUNK_OFFSET_AT);
		const uint32_t bytes = avatar_le32(block + p + CHUNK_BYTES_AT);
		if (offset != *total || packed > block_bytes - p - CHUNK_HEADER_BYTES || bytes > MAX_CHUNK_BYTES)
			return XSDK_AVATAR_ERROR_FORMAT;
		*total += bytes;
		p += CHUNK_HEADER_BYTES + packed;
	}
	return XSDK_AVATAR_OK;
}

int avatar_expand_block(const uint8_t *block, uint32_t block_bytes, uint8_t **out, uint32_t *out_bytes)
{
	uint32_t total;
	int result = expanded_size(block, block_bytes, &total);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint8_t *buffer = avatar_calloc(1, total ? total : 1, "an expanded block");
	if (!buffer)
		return XSDK_AVATAR_ERROR_MEMORY;
	// Each chunk is an LZX stream of its own (section 3.2).
	for (uint32_t p = 0; p + CHUNK_HEADER_BYTES <= block_bytes;) {
		const uint32_t packed = avatar_le32(block + p + CHUNK_PACKED_AT);
		const uint32_t offset = avatar_le32(block + p + CHUNK_OFFSET_AT);
		const uint32_t bytes = avatar_le32(block + p + CHUNK_BYTES_AT);
		result = avatar_lzx_expand(block + p + CHUNK_HEADER_BYTES, packed, buffer + offset, bytes);
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
