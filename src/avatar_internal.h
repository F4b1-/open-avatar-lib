#ifndef XSDK_AVATAR_INTERNAL_H
#define XSDK_AVATAR_INTERNAL_H

// What the library's files share inside: byte order, a bounds-checked reader over a buffer, bit fields and bit
// streams, the allocation functions, and the asset layer's internal interfaces (LZX, STRB blocks, STFS).

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---- Byte order: a value stored at p, most significant byte first (be) or last (le) ----

static inline uint32_t avatar_be16(const uint8_t *p)
{
	return (uint32_t)p[0] << 8 | p[1];
}

static inline uint32_t avatar_be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline uint32_t avatar_be24(const uint8_t *p)
{
	return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
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

// Stores value at p, most significant byte first.
static inline void avatar_store_be16(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static inline void avatar_store_be32(uint8_t *p, uint32_t value)
{
	avatar_store_be16(p, value >> 16);
	avatar_store_be16(p + 2, value);
}

// The float whose IEEE 754 single-precision bits these are.
static inline float avatar_float(uint32_t bits)
{
	union {
		uint32_t u;
		float f;
	} v = { .u = bits };
	return v.f;
}

// ---- A reader: a cursor over a buffer that never reads past its end ----
//
// Each read takes the next bytes and moves on. A read past the end returns 0 and marks the reader failed, and every
// read after it does the same, so a decoder reads a whole header and checks `failed` once:
//
//     AvatarReader r = avatar_reader(data, bytes);
//     const uint32_t frames = avatar_read_le32(&r);
//     const float fps = avatar_read_float_le(&r);
//     if (r.failed)
//         return XSDK_AVATAR_ERROR_FORMAT;
typedef struct {
	const uint8_t *data;
	uint32_t size;
	uint32_t at; // the next byte to read
	bool failed;
} AvatarReader;

static inline AvatarReader avatar_reader(const void *data, uint32_t size)
{
	return (AvatarReader){ .data = data, .size = size };
}

// The next n bytes, in place, and moves past them; NULL (and failed) if fewer are left.
static inline const uint8_t *avatar_read_bytes(AvatarReader *r, uint32_t n)
{
	if (r->failed || n > r->size - r->at) {
		r->failed = true;
		return NULL;
	}
	const uint8_t *p = r->data + r->at;
	r->at += n;
	return p;
}

// Moves to byte `at` of the buffer; failed if that is past its end.
static inline void avatar_reader_seek(AvatarReader *r, uint32_t at)
{
	if (at > r->size)
		r->failed = true;
	else
		r->at = at;
}

static inline void avatar_reader_skip(AvatarReader *r, uint32_t n)
{
	(void)avatar_read_bytes(r, n);
}

static inline uint32_t avatar_read_u8(AvatarReader *r)
{
	const uint8_t *p = avatar_read_bytes(r, 1);
	return p ? p[0] : 0;
}

static inline uint32_t avatar_read_le16(AvatarReader *r)
{
	const uint8_t *p = avatar_read_bytes(r, 2);
	return p ? avatar_le16(p) : 0;
}

static inline uint32_t avatar_read_be16(AvatarReader *r)
{
	const uint8_t *p = avatar_read_bytes(r, 2);
	return p ? avatar_be16(p) : 0;
}

static inline uint32_t avatar_read_le32(AvatarReader *r)
{
	const uint8_t *p = avatar_read_bytes(r, 4);
	return p ? avatar_le32(p) : 0;
}

static inline uint32_t avatar_read_be32(AvatarReader *r)
{
	const uint8_t *p = avatar_read_bytes(r, 4);
	return p ? avatar_be32(p) : 0;
}

static inline float avatar_read_float_le(AvatarReader *r)
{
	return avatar_float(avatar_read_le32(r));
}

static inline float avatar_read_float_be(AvatarReader *r)
{
	return avatar_float(avatar_read_be32(r));
}

// ---- Bit fields and bit streams ----

// The field `width` bits wide (1-32) starting at bit `shift` of word.
static inline uint32_t avatar_bits(uint32_t word, uint32_t shift, uint32_t width)
{
	return (uint32_t)(((uint64_t)word >> shift) & ((1ull << width) - 1));
}

// The same field read as a two's complement number: its top bit is the sign.
static inline int32_t avatar_signed_bits(uint32_t word, uint32_t shift, uint32_t width)
{
	const uint32_t field = avatar_bits(word, shift, width);
	return (int32_t)field - (int32_t)((field >> (width - 1)) << width);
}

// `width` bits (0-32) of a bit stream at bit `at`, least significant bit first: bit k of the stream is bit k % 8 of
// byte k / 8. The model, skeleton and animation blocks all store their quantised values this way (SPECS.md, sections
// 3.3, 4.4 and 4.5). Reads only the bytes the field covers; the caller checks they are in the buffer.
static inline uint32_t avatar_stream_bits(const uint8_t *data, uint64_t at, uint32_t width)
{
	if (width == 0)
		return 0;
	const uint8_t *p = data + (at >> 3);
	const uint32_t first = (uint32_t)(at & 7), bytes = (first + width + 7) / 8;
	uint64_t window = 0;
	for (uint32_t i = 0; i < bytes; i++)
		window |= (uint64_t)p[i] << (8 * i);
	return (uint32_t)((window >> first) & ((1ull << width) - 1));
}

// IEEE 754 half precision as a float: a sign bit (15), 5 exponent bits (10-14) biased by 15, 10 mantissa bits (0-9)
// with an implied leading 1 unless the exponent is 0 (subnormal); exponent 31 is infinity or NaN.
static inline float avatar_half_to_float(uint32_t half)
{
	enum { MANTISSA_BITS = 10, EXPONENT_BITS = 5, BIAS = 15, EXPONENT_MAX = 31 };
	const uint32_t sign = avatar_bits(half, MANTISSA_BITS + EXPONENT_BITS, 1);
	const uint32_t exponent = avatar_bits(half, MANTISSA_BITS, EXPONENT_BITS);
	const uint32_t mantissa = avatar_bits(half, 0, MANTISSA_BITS);
	float value;
	if (exponent == 0) // subnormal: mantissa * 2^(1 - BIAS - MANTISSA_BITS)
		value = ldexpf((float)mantissa, 1 - BIAS - MANTISSA_BITS);
	else if (exponent == EXPONENT_MAX)
		value = mantissa ? NAN : INFINITY;
	else // 1.mantissa * 2^(exponent - BIAS), the implied 1 as bit MANTISSA_BITS
		value = ldexpf((float)(mantissa | 1u << MANTISSA_BITS), (int)exponent - BIAS - MANTISSA_BITS);
	return sign ? -value : value;
}

// value rounded up to a multiple of `multiple`, a power of two.
static inline uint32_t avatar_round_up(uint32_t value, uint32_t multiple)
{
	return (value + multiple - 1) & ~(multiple - 1);
}

// ---- Allocation (avatar_alloc.c) ----
//
// Every heap allocation of the library goes through these. On failure each logs what it was for and how much it
// asked ("avatar: out of memory: <what>, <bytes> bytes") and returns NULL; the caller returns
// XSDK_AVATAR_ERROR_MEMORY. tests/alloc_check sets avatar_alloc_fail_at to make the Nth allocation fail.
void *avatar_malloc(size_t bytes, const char *what);
void *avatar_calloc(size_t count, size_t size, const char *what);
void *avatar_realloc(void *pointer, size_t bytes, const char *what);
// 0 (never) normally; N > 0 makes the Nth allocation from now fail, once. Counted per call of the three above.
extern uint32_t avatar_alloc_fail_at;

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
