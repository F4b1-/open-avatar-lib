// STFS packages (LIVE, PIRS, CON), read through a callback: the file table and files in consecutive blocks or in
// block chains. Block and hash table addressing as Avatar Thrift Store's core/stfs_package.cpp does it (MIT, the same
// author's).

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

enum {
	HEADER_SIZE = 0x340, // u32 BE
	VOLUME = 0x379,
	BLOCK = 0x1000,
	BLOCKS_PER_TABLE = 0xAA,
	BLOCKS_PER_L1 = 0x70E4,
	HASH_ENTRY = 0x18,
	FILE_ENTRY = 0x40,
	MAX_TABLE_BLOCKS = 64,
};

int avatar_stfs_open(AvatarStfs *stfs, XsdkAvatarRead read, void *context)
{
	memset(stfs, 0, sizeof *stfs);
	stfs->read = read;
	stfs->context = context;
	uint8_t header[VOLUME + 0x24];
	if (read(context, 0, header, sizeof header) != 0)
		return XSDK_AVATAR_ERROR_READ;
	if (memcmp(header, "PIRS", 4) != 0 && memcmp(header, "LIVE", 4) != 0 && memcmp(header, "CON ", 4) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint8_t *volume = header + VOLUME;
	stfs->base = (avatar_be32(header + HEADER_SIZE) + 0xFFF) & ~0xFFFu;
	stfs->shift = (~volume[2]) & 1;
	stfs->step[0] = stfs->shift ? 0xAC : 0xAB;
	stfs->step[1] = stfs->shift ? 0x723A : 0x718F;
	stfs->allocated = avatar_be32(volume + 0x1C);
	stfs->top_level = stfs->allocated <= BLOCKS_PER_TABLE ? 0 : (stfs->allocated <= BLOCKS_PER_L1 ? 1 : 2);
	stfs->top_table = avatar_stfs_table_offset(stfs, stfs->top_level, 0);
	if (stfs->shift && (volume[2] & 2))
		stfs->top_table += BLOCK;
	stfs->table_blocks = avatar_le16(volume + 3);
	stfs->table_block = avatar_le24(volume + 5);
	return XSDK_AVATAR_OK;
}

uint64_t avatar_stfs_block_offset(const AvatarStfs *stfs, uint32_t block)
{
	uint32_t n = (((block + BLOCKS_PER_TABLE) / BLOCKS_PER_TABLE) << stfs->shift) + block;
	if (block >= BLOCKS_PER_TABLE) {
		n += ((block + BLOCKS_PER_L1) / BLOCKS_PER_L1) << stfs->shift;
		if (block >= BLOCKS_PER_L1)
			n += 1u << stfs->shift;
	}
	return (uint64_t)stfs->base + (uint64_t)n * BLOCK;
}

// Where level's hash table covering block lies.
uint64_t avatar_stfs_table_offset(const AvatarStfs *stfs, uint32_t level, uint32_t block)
{
	uint32_t n = 0;
	if (level == 0) {
		if (block >= BLOCKS_PER_TABLE) {
			n = (block / BLOCKS_PER_TABLE) * stfs->step[0];
			n += ((block / BLOCKS_PER_L1) + 1) << stfs->shift;
			if (block / BLOCKS_PER_L1 != 0)
				n += 1u << stfs->shift;
		}
	} else if (level == 1) {
		n = block < BLOCKS_PER_L1 ? stfs->step[0] : (1u << stfs->shift) + stfs->step[1] * (block / BLOCKS_PER_L1);
	} else {
		n = stfs->step[1];
	}
	return (uint64_t)stfs->base + (uint64_t)n * BLOCK;
}

// The block after block in its chain, from its hash entry; for packages with two copies of each table, the parent
// entry's status bit 0x40 says which copy is current.
static int next_block(const AvatarStfs *stfs, uint32_t block, uint32_t *next)
{
	if (block >= stfs->allocated)
		return XSDK_AVATAR_ERROR_FORMAT;
	uint64_t table = stfs->top_table;
	uint8_t entry[HASH_ENTRY];
	for (uint32_t level = stfs->top_level; level > 0; level--) {
		const uint32_t index = level == 2 ? block / BLOCKS_PER_L1 : (block / BLOCKS_PER_TABLE) % BLOCKS_PER_TABLE;
		if (stfs->read(stfs->context, table + (uint64_t)index * HASH_ENTRY, entry, HASH_ENTRY) != 0)
			return XSDK_AVATAR_ERROR_READ;
		uint64_t child = avatar_stfs_table_offset(stfs, level - 1, block);
		if (stfs->shift && (entry[20] & 0x40))
			child += BLOCK;
		table = child;
	}
	if (stfs->read(stfs->context, table + (uint64_t)(block % BLOCKS_PER_TABLE) * HASH_ENTRY, entry, HASH_ENTRY) != 0)
		return XSDK_AVATAR_ERROR_READ;
	*next = (uint32_t)entry[21] << 16 | (uint32_t)entry[22] << 8 | entry[23];
	return XSDK_AVATAR_OK;
}

int avatar_stfs_find(const AvatarStfs *stfs, const char *name, AvatarStfsFile *file)
{
	const uint32_t name_length = (uint32_t)strlen(name);
	uint32_t block = stfs->table_block;
	for (uint32_t b = 0; b < stfs->table_blocks && b < MAX_TABLE_BLOCKS; b++) {
		uint8_t table[BLOCK];
		if (stfs->read(stfs->context, avatar_stfs_block_offset(stfs, block), table, sizeof table) != 0)
			return XSDK_AVATAR_ERROR_READ;
		for (uint32_t e = 0; e < BLOCK / FILE_ENTRY; e++) {
			const uint8_t *entry = table + e * FILE_ENTRY;
			const uint32_t length = entry[0x28] & 0x3F;
			if (length != name_length || (entry[0x28] & 0x80) || avatar_be16(entry + 0x32) != 0xFFFF)
				continue;
			bool same = true;
			for (uint32_t c = 0; c < length && same; c++) {
				const char a = (char)entry[c], b2 = name[c];
				same = (a | 0x20) == (b2 | 0x20); // the names here are ASCII
			}
			if (!same)
				continue;
			file->consecutive = (entry[0x28] & 0x40) != 0;
			file->blocks = avatar_le24(entry + 0x29);
			file->start = avatar_le24(entry + 0x2F);
			file->bytes = avatar_be32(entry + 0x34);
			return XSDK_AVATAR_OK;
		}
		if (b + 1 < stfs->table_blocks) {
			const int result = next_block(stfs, block, &block);
			if (result != XSDK_AVATAR_OK)
				return result;
		}
	}
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}

int avatar_stfs_read_range(const AvatarStfs *stfs, const AvatarStfsFile *file, uint32_t offset, void *buffer,
	uint32_t length)
{
	if (!file->consecutive)
		return XSDK_AVATAR_ERROR_UNSUPPORTED;
	if (offset > file->bytes || length > file->bytes - offset)
		return XSDK_AVATAR_ERROR_FORMAT;
	uint8_t *out = buffer;
	while (length) {
		const uint32_t block = file->start + offset / BLOCK;
		const uint64_t start = avatar_stfs_block_offset(stfs, block) + offset % BLOCK;
		uint32_t run = BLOCK - offset % BLOCK;
		uint32_t next = block + 1;
		// Blocks between hash tables lie next to each other: read them in one go.
		while (run < length &&
			avatar_stfs_block_offset(stfs, next) == avatar_stfs_block_offset(stfs, next - 1) + BLOCK) {
			run += BLOCK;
			next++;
		}
		if (run > length)
			run = length;
		if (stfs->read(stfs->context, start, out, run) != 0)
			return XSDK_AVATAR_ERROR_READ;
		out += run;
		offset += run;
		length -= run;
	}
	return XSDK_AVATAR_OK;
}

int xsdk_avatar_package_read_file(
	XsdkAvatarRead read, void *context, const char *name, void **data, uint32_t *size_bytes)
{
	*data = NULL;
	*size_bytes = 0;
	AvatarStfs stfs;
	AvatarStfsFile file;
	int result = avatar_stfs_open(&stfs, read, context);
	if (result == XSDK_AVATAR_OK)
		result = avatar_stfs_find(&stfs, name, &file);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint8_t *buffer = malloc(file.bytes ? file.bytes : 1);
	if (!buffer)
		return XSDK_AVATAR_ERROR_MEMORY;
	if (file.consecutive) {
		result = avatar_stfs_read_range(&stfs, &file, 0, buffer, file.bytes);
	} else {
		uint32_t block = file.start;
		for (uint32_t done = 0; result == XSDK_AVATAR_OK && done < file.bytes; done += BLOCK) {
			const uint32_t chunk = file.bytes - done < BLOCK ? file.bytes - done : BLOCK;
			if (read(context, avatar_stfs_block_offset(&stfs, block), buffer + done, chunk) != 0)
				result = XSDK_AVATAR_ERROR_READ;
			else if (done + chunk < file.bytes)
				result = next_block(&stfs, block, &block);
		}
	}
	if (result != XSDK_AVATAR_OK) {
		free(buffer);
		return result;
	}
	*data = buffer;
	*size_bytes = file.bytes;
	return XSDK_AVATAR_OK;
}
