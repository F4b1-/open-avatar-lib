// Textures (FORMATS.md, sections 3.4 and 3.5): a texture header and its pixels, and texture blocks (STRB block 2),
// which hold the face features' textures (eyes, eyebrows, mouths). Builds for the PC.

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

// The 33-byte header (section 3.4), little-endian.
enum {
	FORMAT_AT = 0,       // byte: the Xenos data format (low 6 bits) and endian mode (high 2)
	FORMAT_BITS = 6,
	ENDIAN_BITS = 2,
	ENDIAN_8_IN_16 = 1,  // swap the bytes of each 16-bit word: the only mode seen
	WIDTH_AT = 4,
	HEIGHT_AT = 8,
	FRAME_COUNT_AT = 20,
	SLOT_WORD_AT = 24,   // bit 0 set: an empty slot, no pixels follow
	SLOT_EMPTY = 0x1,
};

enum {
	MAX_SIDE = 1024,      // a sanity limit, well above the textures seen
	MAX_FRAMES = 64,      // eyes and mouths have the most, 14
	DXT_BLOCK_SIDE = 4,   // DXT stores 4x4 texels a block
	DXT1_BLOCK_BYTES = 8,
	DXT5_BLOCK_BYTES = 16,
};

int avatar_parse_texture_header(const uint8_t *h, XsdkAvatarTexture *texture, bool *empty)
{
	const uint32_t format = avatar_bits(h[FORMAT_AT], 0, FORMAT_BITS);
	const uint32_t endian = avatar_bits(h[FORMAT_AT], FORMAT_BITS, ENDIAN_BITS);
	const uint32_t width = avatar_le32(h + WIDTH_AT);
	const uint32_t height = avatar_le32(h + HEIGHT_AT);
	const uint32_t frame_count = avatar_le32(h + FRAME_COUNT_AT);
	if ((format != XSDK_AVATAR_TEXTURE_DXT1 && format != XSDK_AVATAR_TEXTURE_DXT5) || endian != ENDIAN_8_IN_16)
		return XSDK_AVATAR_ERROR_UNSUPPORTED;
	if (!width || !height || width > MAX_SIDE || height > MAX_SIDE || width % DXT_BLOCK_SIDE ||
		height % DXT_BLOCK_SIDE || !frame_count || frame_count > MAX_FRAMES)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t blocks = (width / DXT_BLOCK_SIDE) * (height / DXT_BLOCK_SIDE);
	texture->format = format;
	texture->width = width;
	texture->height = height;
	texture->frame_count = frame_count;
	texture->frame_bytes = blocks * (format == XSDK_AVATAR_TEXTURE_DXT1 ? DXT1_BLOCK_BYTES : DXT5_BLOCK_BYTES);
	texture->data = NULL;
	*empty = (avatar_le32(h + SLOT_WORD_AT) & SLOT_EMPTY) != 0;
	return XSDK_AVATAR_OK;
}

int avatar_copy_texture_pixels(XsdkAvatarTexture *texture, const uint8_t *data, uint32_t bytes, uint32_t at)
{
	const uint64_t size = (uint64_t)texture->frame_count * texture->frame_bytes;
	if (at > bytes || size > bytes - at)
		return XSDK_AVATAR_ERROR_FORMAT;
	texture->data = avatar_malloc((size_t)size, "a texture's pixels");
	if (!texture->data)
		return XSDK_AVATAR_ERROR_MEMORY;
	memcpy(texture->data, data + at, (size_t)size);
	return XSDK_AVATAR_OK;
}

int xsdk_avatar_texture_decode(XsdkAvatarTexture *texture, const void *strb, uint32_t size_bytes)
{
	memset(texture, 0, sizeof *texture);
	const uint8_t *block;
	uint32_t block_bytes;
	int result = avatar_find_block(strb, size_bytes, AVATAR_BLOCK_TEXTURE, &block, &block_bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint8_t *data;
	uint32_t bytes;
	result = avatar_expand_block(block, block_bytes, &data, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	// The expanded block: the header, then the pixels. A face feature's texture is never an empty slot.
	bool empty = true;
	result = bytes >= AVATAR_TEXTURE_HEADER_BYTES ? avatar_parse_texture_header(data, texture, &empty)
												  : XSDK_AVATAR_ERROR_FORMAT;
	if (result == XSDK_AVATAR_OK)
		result = empty ? XSDK_AVATAR_ERROR_FORMAT
					   : avatar_copy_texture_pixels(texture, data, bytes, AVATAR_TEXTURE_HEADER_BYTES);
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
