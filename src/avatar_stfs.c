// STFS packages (LIVE, PIRS, CON), read through a callback: the file table and files in consecutive blocks or in
// block chains. Block and hash table addressing as Avatar Thrift Store's core/stfs_package.cpp does it (MIT, the same
// author's). Builds for the PC.
//
// A package is a header, then 4 KB blocks from the first block boundary after it. Data blocks come in groups of 170,
// each group after its hash table (one block, or two copies of it); every 170 groups have a level-1 hash table above
// them, and a large package one level-2 table on top. A hash table holds one 24-byte entry per block it covers.

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

enum {
	BLOCK = 0x1000,
	HASH_ENTRY_BYTES = 0x18,
	BLOCKS_PER_TABLE = BLOCK / HASH_ENTRY_BYTES,             // 170: the blocks one hash table covers
	BLOCKS_PER_L1 = BLOCKS_PER_TABLE * BLOCKS_PER_TABLE,    // 28900: the data blocks under one level-1 table
	FILE_ENTRY_BYTES = 0x40,
	MAX_TABLE_BLOCKS = 64, // the most file-table blocks read: far more files than an avatar item or the pack holds
};

// The package header: the magic, then the header's size (big-endian) at HEADER_SIZE_AT, and the volume descriptor.
enum {
	HEADER_SIZE_AT = 0x340,
	VOLUME_AT = 0x379,
	VOLUME_BYTES = 0x24,
};

// The volume descriptor.
enum {
	VOLUME_FLAGS = 2,                // bit 0 set: one copy of each hash table; bit 1: the top table's second copy is
	VOLUME_FLAG_ONE_COPY = 0x1,      // the current one
	VOLUME_FLAG_TOP_SECOND = 0x2,
	VOLUME_TABLE_BLOCKS_AT = 3,      // u16 little-endian: the file table's length in blocks
	VOLUME_TABLE_BLOCK_AT = 5,       // u24 little-endian: its first block
	VOLUME_ALLOCATED_AT = 0x1C,      // u32 big-endian: the blocks in use
};

// A hash table entry: a 20-byte SHA-1 of the block, a status byte, the next block in the file's chain.
enum {
	HASH_STATUS_AT = 20,
	HASH_STATUS_SECOND_CURRENT = 0x40, // in a level above: the second copy of the table below is the current one
	HASH_NEXT_AT = 21,                 // u24 big-endian
};

// A file table entry: its name (not terminated), then:
enum {
	FILE_FLAGS_AT = 0x28,        // bits 0-5 the name's length, 0x40 blocks consecutive, 0x80 a folder
	FILE_NAME_LENGTH_BITS = 6,
	FILE_CONSECUTIVE = 0x40,
	FILE_FOLDER = 0x80,
	FILE_BLOCKS_AT = 0x29,       // u24 little-endian
	FILE_START_AT = 0x2F,        // u24 little-endian: the first block
	FILE_PARENT_AT = 0x32,       // u16 big-endian: the folder's entry, FILE_ROOT for the root
	FILE_ROOT = 0xFFFF,
	FILE_SIZE_AT = 0x34,         // u32 big-endian, in bytes
};

int avatar_stfs_open(AvatarStfs *stfs, XsdkAvatarRead read, void *context)
{
	memset(stfs, 0, sizeof *stfs);
	stfs->read = read;
	stfs->context = context;
	uint8_t header[VOLUME_AT + VOLUME_BYTES];
	if (read(context, 0, header, sizeof header) != 0)
		return XSDK_AVATAR_ERROR_READ;
	if (memcmp(header, "PIRS", 4) != 0 && memcmp(header, "LIVE", 4) != 0 && memcmp(header, "CON ", 4) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint8_t *volume = header + VOLUME_AT;
	stfs->base = avatar_round_up(avatar_be32(header + HEADER_SIZE_AT), BLOCK);
	// Two copies of each hash table make every table take two blocks: shift doubles a count of tables.
	stfs->shift = (volume[VOLUME_FLAGS] & VOLUME_FLAG_ONE_COPY) ? 0 : 1;
	const uint32_t table_blocks = 1u << stfs->shift;
	// How far apart the level-0 and level-1 tables lie, in blocks: what one of them covers, with its own tables.
	stfs->step[0] = BLOCKS_PER_TABLE + table_blocks;
	stfs->step[1] = BLOCKS_PER_L1 + (BLOCKS_PER_TABLE + 1) * table_blocks;
	stfs->allocated = avatar_be32(volume + VOLUME_ALLOCATED_AT);
	stfs->top_level = stfs->allocated <= BLOCKS_PER_TABLE ? 0 : (stfs->allocated <= BLOCKS_PER_L1 ? 1 : 2);
	stfs->top_table = avatar_stfs_table_offset(stfs, stfs->top_level, 0);
	if (stfs->shift && (volume[VOLUME_FLAGS] & VOLUME_FLAG_TOP_SECOND))
		stfs->top_table += BLOCK;
	stfs->table_blocks = avatar_le16(volume + VOLUME_TABLE_BLOCKS_AT);
	stfs->table_block = avatar_le24(volume + VOLUME_TABLE_BLOCK_AT);
	return XSDK_AVATAR_OK;
}

uint64_t avatar_stfs_block_offset(const AvatarStfs *stfs, uint32_t block)
{
	// The block's number among all blocks: its own, plus the hash tables before it (each one or two blocks).
	uint32_t n = (((block + BLOCKS_PER_TABLE) / BLOCKS_PER_TABLE) << stfs->shift) + block;
	if (block >= BLOCKS_PER_TABLE) {
		n += ((block + BLOCKS_PER_L1) / BLOCKS_PER_L1) << stfs->shift;
		if (block >= BLOCKS_PER_L1)
			n += 1u << stfs->shift;
	}
	return (uint64_t)stfs->base + (uint64_t)n * BLOCK;
}

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

// The block after block in its chain, from its hash entry, reached through the tables above it; where tables have
// two copies, each level's entry says which copy below it is current.
static int next_block(const AvatarStfs *stfs, uint32_t block, uint32_t *next)
{
	if (block >= stfs->allocated)
		return XSDK_AVATAR_ERROR_FORMAT;
	uint64_t table = stfs->top_table;
	uint8_t entry[HASH_ENTRY_BYTES];
	for (uint32_t level = stfs->top_level; level > 0; level--) {
		const uint32_t index = level == 2 ? block / BLOCKS_PER_L1 : (block / BLOCKS_PER_TABLE) % BLOCKS_PER_TABLE;
		if (stfs->read(stfs->context, table + (uint64_t)index * HASH_ENTRY_BYTES, entry, HASH_ENTRY_BYTES) != 0)
			return XSDK_AVATAR_ERROR_READ;
		uint64_t child = avatar_stfs_table_offset(stfs, level - 1, block);
		if (stfs->shift && (entry[HASH_STATUS_AT] & HASH_STATUS_SECOND_CURRENT))
			child += BLOCK;
		table = child;
	}
	const uint64_t at = table + (uint64_t)(block % BLOCKS_PER_TABLE) * HASH_ENTRY_BYTES;
	if (stfs->read(stfs->context, at, entry, HASH_ENTRY_BYTES) != 0)
		return XSDK_AVATAR_ERROR_READ;
	*next = avatar_be24(entry + HASH_NEXT_AT);
	return XSDK_AVATAR_OK;
}

// Whether a file table entry is a file in the root folder named name, ignoring case (the names here are ASCII,
// where setting bit 0x20 makes a letter lower case).
static bool is_root_file(const uint8_t *entry, const char *name, uint32_t name_length)
{
	const uint32_t flags = entry[FILE_FLAGS_AT];
	if (avatar_bits(flags, 0, FILE_NAME_LENGTH_BITS) != name_length || (flags & FILE_FOLDER) ||
		avatar_be16(entry + FILE_PARENT_AT) != FILE_ROOT)
		return false;
	for (uint32_t c = 0; c < name_length; c++)
		if ((entry[c] | 0x20) != ((uint8_t)name[c] | 0x20))
			return false;
	return true;
}

int avatar_stfs_find(const AvatarStfs *stfs, const char *name, AvatarStfsFile *file)
{
	const uint32_t name_length = (uint32_t)strlen(name);
	uint32_t block = stfs->table_block;
	for (uint32_t b = 0; b < stfs->table_blocks && b < MAX_TABLE_BLOCKS; b++) {
		uint8_t table[BLOCK];
		if (stfs->read(stfs->context, avatar_stfs_block_offset(stfs, block), table, sizeof table) != 0)
			return XSDK_AVATAR_ERROR_READ;
		for (uint32_t e = 0; e < BLOCK / FILE_ENTRY_BYTES; e++) {
			const uint8_t *entry = table + e * FILE_ENTRY_BYTES;
			if (!is_root_file(entry, name, name_length))
				continue;
			file->consecutive = (entry[FILE_FLAGS_AT] & FILE_CONSECUTIVE) != 0;
			file->blocks = avatar_le24(entry + FILE_BLOCKS_AT);
			file->start = avatar_le24(entry + FILE_START_AT);
			file->bytes = avatar_be32(entry + FILE_SIZE_AT);
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

// Reads a file stored as a chain of blocks, a block at a time, following its hash entries.
static int read_chain(const AvatarStfs *stfs, const AvatarStfsFile *file, uint8_t *buffer)
{
	uint32_t block = file->start;
	for (uint32_t done = 0; done < file->bytes; done += BLOCK) {
		const uint32_t chunk = file->bytes - done < BLOCK ? file->bytes - done : BLOCK;
		if (stfs->read(stfs->context, avatar_stfs_block_offset(stfs, block), buffer + done, chunk) != 0)
			return XSDK_AVATAR_ERROR_READ;
		if (done + chunk < file->bytes) {
			const int result = next_block(stfs, block, &block);
			if (result != XSDK_AVATAR_OK)
				return result;
		}
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
	uint8_t *buffer = avatar_malloc(file.bytes ? file.bytes : 1, "a file from a package");
	if (!buffer)
		return XSDK_AVATAR_ERROR_MEMORY;
	result = file.consecutive ? avatar_stfs_read_range(&stfs, &file, 0, buffer, file.bytes)
							  : read_chain(&stfs, &file, buffer);
	if (result != XSDK_AVATAR_OK) {
		free(buffer);
		return result;
	}
	*data = buffer;
	*size_bytes = file.bytes;
	return XSDK_AVATAR_OK;
}
