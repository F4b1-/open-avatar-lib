#ifndef XSDK_AVATAR_INTERNAL_H
#define XSDK_AVATAR_INTERNAL_H

// What the library's files share inside: byte order, a bounds-checked reader over a buffer, bit fields and bit
// streams, the allocation functions, and the asset layer's internal interfaces (LZX, STRB blocks, STFS).

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xsdk/avatar.h"

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

// ---- Quantised values (SPECS.md, sections 3.3, 4.4, 4.5) ----
//
// Mesh positions, skeleton blocks and animation channels store numbers as min + q * step * k: an unsigned integer q
// of some bits, a float minimum and step, and a per-axis scale k (x, y, z), the same everywhere. The step is the same
// float in every one seen, and opens every mesh, so its bits mark where a mesh starts.
enum {
	AVATAR_QUANTISATION_STEP_BITS = 0x36ED4402, // 7.071068e-6 as an IEEE 754 single
};
// k = (2, 2 / sqrt(1.5), sqrt(3)): fitted so that items meet the body (section 3.3); with k = (2, 2, 2) the head is
// stretched and sits behind the neck.
static const float avatar_axis_scale[3] = { 2.0f, 1.6329932f, 1.7320508f };

// A quantised group as the skeleton and animation blocks store it, 146 bits of a bit stream: the step (f32), the
// minimum per axis (f32 x 3), then each axis's width in bits (6 bits x 3); the values follow elsewhere, each axis
// `width` bits (0 for an axis that doesn't vary).
enum {
	AVATAR_QUANTISED_STEP_AT = 0,
	AVATAR_QUANTISED_MIN_AT = 32,
	AVATAR_QUANTISED_WIDTH_AT = 128,
	AVATAR_QUANTISED_WIDTH_BITS = 6,
	AVATAR_QUANTISED_BITS = 146,
	AVATAR_QUANTISED_MAX_WIDTH = 32, // 6 bits could say 63; more than a word isn't a value these blocks hold
};

typedef struct {
	float step;
	float min[3];
	uint32_t width[3];
} AvatarQuantised;

// Reads a group at bit `at`; XSDK_AVATAR_ERROR_FORMAT if a width is more than a word.
static inline int avatar_read_quantised(const uint8_t *data, uint64_t at, AvatarQuantised *q)
{
	q->step = avatar_float(avatar_stream_bits(data, at + AVATAR_QUANTISED_STEP_AT, 32));
	for (uint32_t a = 0; a < 3; a++) {
		q->min[a] = avatar_float(avatar_stream_bits(data, at + AVATAR_QUANTISED_MIN_AT + 32 * a, 32));
		q->width[a] = avatar_stream_bits(
			data, at + AVATAR_QUANTISED_WIDTH_AT + AVATAR_QUANTISED_WIDTH_BITS * a, AVATAR_QUANTISED_WIDTH_BITS);
		if (q->width[a] > AVATAR_QUANTISED_MAX_WIDTH)
			return XSDK_AVATAR_ERROR_FORMAT;
	}
	return 0;
}

// How many bits one value of the group takes: its three widths.
static inline uint32_t avatar_quantised_bits(const AvatarQuantised *q)
{
	return q->width[0] + q->width[1] + q->width[2];
}

// The group's next value, from the bit stream at *at (moved past it): min + q * step * k per axis.
static inline void avatar_dequantise(const uint8_t *data, uint64_t *at, const AvatarQuantised *q, float out[3])
{
	for (uint32_t a = 0; a < 3; a++) {
		out[a] = q->min[a] + (float)avatar_stream_bits(data, *at, q->width[a]) * q->step * avatar_axis_scale[a];
		*at += q->width[a];
	}
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

// ---- LZX (avatar_lzx.c) ----

// Expands one LZX chunk (SPECS.md, section 3.2) into exactly out_bytes; 0 or an XSDK_AVATAR_ERROR_*.
int avatar_lzx_expand(const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t out_bytes);

// ---- STRB files (avatar_strb.c) ----

// STRB block ids (SPECS.md, section 3.1).
enum {
	AVATAR_BLOCK_ANIMATION = 1,
	AVATAR_BLOCK_TEXTURE = 2,
	AVATAR_BLOCK_MODEL = 3,
	AVATAR_BLOCK_SHAPE = 4,
	AVATAR_BLOCK_SKELETON = 5,
	AVATAR_BLOCK_COLORS = 7,
};

// Finds the first block of an STRB file with this id: *data points into file, *data_bytes its length.
// XSDK_AVATAR_ERROR_NOT_FOUND if the file has none, _FORMAT if it isn't an STRB file.
int avatar_find_block(const uint8_t *file, uint32_t size, uint32_t id, const uint8_t **data, uint32_t *data_bytes);
// Expands a model or texture block's LZX chunks (section 3.2) into *out, malloc'd for the caller to free.
int avatar_expand_block(const uint8_t *block, uint32_t block_bytes, uint8_t **out, uint32_t *out_bytes);

// ---- Packed vertices (avatar_model.c), as meshes and shape overrides store them (SPECS.md, sections 3.3, 3.7) ----
//
// A float header (f32 step, f32 minimum per axis), then a bit stream: each position axis's width (6 bits x 3), then
// for each of four elements a u32 base and a u32 width. A vertex is then its quantised position (min + q * step * k)
// and each element as (base + a `width`-bit value) mod 2^32.
enum {
	AVATAR_ELEMENT_NORMAL,  // 11:11:10 signed, x in the low bits
	AVATAR_ELEMENT_WEIGHTS, // four bytes, the first in the low byte, summing to 255
	AVATAR_ELEMENT_BONES,   // four bytes: bone indices, as the weights
	AVATAR_ELEMENT_COLOR,   // ARGB
	AVATAR_ELEMENTS,
};

typedef struct {
	float step;
	float min[3];
	uint32_t position_width[3];
	uint32_t base[AVATAR_ELEMENTS];
	uint32_t width[AVATAR_ELEMENTS];
	uint32_t bits;            // a vertex's bits: its position and elements (texture coordinates come after, in a mesh)
	uint64_t first_vertex;    // the bit where the first vertex starts
} AvatarVertexFormat;

// Reads the format whose float header is at byte `at` of data. XSDK_AVATAR_ERROR_FORMAT if it runs past `bytes` or an
// element is wider than a word.
int avatar_read_vertex_format(const uint8_t *data, uint32_t bytes, uint32_t at, AvatarVertexFormat *format);
// One vertex from the bit stream at *at (moved past it): its position, unit normal, colour, and weights and bones
// (NULL to skip them).
void avatar_decode_vertex(const uint8_t *data, uint64_t *at, const AvatarVertexFormat *format, float position[3],
	float normal[3], uint32_t *color, uint8_t weights[4], uint8_t bones[4]);

// ---- Textures (avatar_texture.c) ----

enum { AVATAR_TEXTURE_HEADER_BYTES = 33 };

// Reads a 33-byte texture header (SPECS.md, section 3.4) into texture (no pixels yet); *empty gets whether it is an
// empty slot, whose pixels the Head's face features fill at run time.
int avatar_parse_texture_header(const uint8_t *header, XsdkAvatarTexture *texture, bool *empty);
// Copies the texture's pixels (frame_count * frame_bytes) from data[at], within bytes, into texture->data.
int avatar_copy_texture_pixels(XsdkAvatarTexture *texture, const uint8_t *data, uint32_t bytes, uint32_t at);

// ---- STFS packages (avatar_stfs.c) ----

// An STFS package being read.
typedef struct {
	XsdkAvatarRead read;
	void *context;
	uint32_t base;  // the first data block's offset: the header, rounded up to a block
	uint32_t shift; // 1 if each hash table has two copies (each takes two blocks), else 0
	uint32_t step[2]; // how far apart level-0 and level-1 hash tables lie, in blocks
	uint32_t allocated; // blocks in use
	uint32_t top_level; // the highest hash table level: 0, 1 or 2
	uint64_t top_table; // its offset
	uint32_t table_blocks; // the file table's length in blocks
	uint32_t table_block;  // its first block
} AvatarStfs;

// A file in a package, as its file table entry gives it.
typedef struct {
	bool consecutive; // stored in consecutive blocks (else a chain the hash tables link)
	uint32_t start;   // its first block
	uint32_t blocks;
	uint32_t bytes;
} AvatarStfsFile;

// Reads the package's header through read. XSDK_AVATAR_ERROR_READ if read fails, _FORMAT if it isn't STFS.
int avatar_stfs_open(AvatarStfs *stfs, XsdkAvatarRead read, void *context);
// The offset in the package of data block `block`, past the hash tables before it.
uint64_t avatar_stfs_block_offset(const AvatarStfs *stfs, uint32_t block);
// The offset of the hash table at `level` (0-2) that covers data block `block` (its first copy).
uint64_t avatar_stfs_table_offset(const AvatarStfs *stfs, uint32_t level, uint32_t block);
// Finds a file in the package's root folder, by name, ignoring case.
int avatar_stfs_find(const AvatarStfs *stfs, const char *name, AvatarStfsFile *file);
// Reads part of a file stored in consecutive blocks.
int avatar_stfs_read_range(
	const AvatarStfs *stfs, const AvatarStfsFile *file, uint32_t offset, void *buffer, uint32_t length);

#endif
