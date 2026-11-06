#ifndef XSDK_AVATAR_INTERNAL_H
#define XSDK_AVATAR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

static inline uint32_t avatar_be16(const uint8_t *p)
{
	return (uint32_t)p[0] << 8 | p[1];
}

static inline uint32_t avatar_be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline uint32_t avatar_le16(const uint8_t *p)
{
	return (uint32_t)p[1] << 8 | p[0];
}

static inline uint32_t avatar_le24(const uint8_t *p)
{
	return (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static inline uint32_t avatar_le32(const uint8_t *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static inline float avatar_float(uint32_t bits)
{
	union {
		uint32_t u;
		float f;
	} v = { .u = bits };
	return v.f;
}

// Expands one LZX chunk (SPECS.md, section 3.2) into exactly out_bytes; 0 or an XSDK_AVATAR_ERROR_*.
int avatar_lzx_expand(const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t out_bytes);

#include "xsdk/avatar.h"

// Finds the first block of an STRB file with this id: its data and size (avatar_model.c).
int avatar_find_block(const uint8_t *file, uint32_t size, uint32_t id, const uint8_t **data, uint32_t *data_bytes);

// An STFS package being read (avatar_stfs.c).
typedef struct {
	XsdkAvatarRead read;
	void *context;
	uint32_t base;  // the first data block's offset
	uint32_t shift; // 1 if each hash table has two copies
	uint32_t step[2];
	uint32_t allocated;
	uint32_t top_level;
	uint64_t top_table;
	uint32_t table_blocks;
	uint32_t table_block;
} AvatarStfs;

typedef struct {
	bool consecutive;
	uint32_t start;
	uint32_t blocks;
	uint32_t bytes;
} AvatarStfsFile;

int avatar_stfs_open(AvatarStfs *stfs, XsdkAvatarRead read, void *context);
uint64_t avatar_stfs_block_offset(const AvatarStfs *stfs, uint32_t block);
uint64_t avatar_stfs_table_offset(const AvatarStfs *stfs, uint32_t level, uint32_t block);
// Finds a file in the package's root folder, by name, ignoring case.
int avatar_stfs_find(const AvatarStfs *stfs, const char *name, AvatarStfsFile *file);
// Reads part of a file stored in consecutive blocks.
int avatar_stfs_read_range(
	const AvatarStfs *stfs, const AvatarStfsFile *file, uint32_t offset, void *buffer, uint32_t length);

#endif
