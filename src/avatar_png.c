// PNG pictures for the rig export's baked textures (avatar_bake.c): RGBA, 8 bits a channel, each row filtered by
// the difference from the texel before it, compressed with deflate (RFC 1951): matches found through hash chains,
// coded with the fixed Huffman codes, in one block. Avatar textures are mostly flat colours, which this shrinks to a
// small part of their size. No GPU: runs on the PC too.

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "avatar_runtime.h"

enum {
	// Deflate (RFC 1951)
	WINDOW = 32768,
	HASH_BITS = 15,
	MIN_MATCH = 3,
	MAX_MATCH = 258,
	MAX_CHAIN = 32, // candidates tried per position
	END_OF_BLOCK = 256,
	FIRST_LENGTH_SYMBOL = 257,
	LENGTH_CODES = 29,
	DISTANCE_CODES = 30,
	DISTANCE_CODE_BITS = 5, // the fixed distance code: the code itself, in 5 bits
	BLOCK_FINAL = 1,        // the header's first bit: the last block
	BLOCK_FIXED_CODES = 1,  // its next 2 bits: compressed with the fixed codes
	OUT_FIRST_CAPACITY = 65536,

	// zlib (RFC 1950): the header's two bytes, deflate with a 32 KB window and no preset dictionary (0x78 0x01 is a
	// multiple of 31, as the header must be), and the Adler-32 at the end.
	ZLIB_METHOD = 0x78,
	ZLIB_FLAGS = 0x01,
	ADLER_MOD = 65521,

	// PNG
	BYTES_PER_TEXEL = 4,
	BIT_DEPTH = 8,
	COLOR_TYPE_RGBA = 6,
	FILTER_SUB = 1, // each byte less the one a texel before
	CRC_POLYNOMIAL = 0xEDB88320u,
};

typedef struct {
	uint8_t *data;
	size_t size, capacity;
	uint32_t bits;  // pending bits, the first in the lowest
	uint32_t count; // how many
	bool failed;    // out of memory: every put after it does nothing
} Out;

static void put_byte(Out *o, uint8_t b)
{
	if (o->failed)
		return;
	if (o->size == o->capacity) {
		const size_t capacity = o->capacity ? o->capacity * 2 : OUT_FIRST_CAPACITY;
		uint8_t *data = avatar_realloc(o->data, capacity, "a PNG");
		if (!data) {
			o->failed = true;
			return;
		}
		o->data = data;
		o->capacity = capacity;
	}
	o->data[o->size++] = b;
}

static void put_be32(Out *o, uint32_t v)
{
	for (int k = 3; k >= 0; k--)
		put_byte(o, (uint8_t)(v >> (8 * k)));
}

// Bits as deflate packs them: the first bit in the lowest of a byte.
static void put_bits(Out *o, uint32_t value, uint32_t n)
{
	o->bits |= value << o->count;
	o->count += n;
	while (o->count >= 8) {
		put_byte(o, (uint8_t)o->bits);
		o->bits >>= 8;
		o->count -= 8;
	}
}

// A Huffman code goes in from its highest bit.
static void put_code(Out *o, uint32_t code, uint32_t n)
{
	uint32_t reversed = 0;
	for (uint32_t i = 0; i < n; i++)
		reversed |= (code >> i & 1) << (n - 1 - i);
	put_bits(o, reversed, n);
}

// The fixed literal/length code (RFC 1951, 3.2.6): four ranges of symbols, each coded from its first code on.
static void put_symbol(Out *o, uint32_t symbol)
{
	static const struct {
		uint16_t last;  // the range's last symbol
		uint16_t first; // its first symbol
		uint16_t code;  // the first symbol's code
		uint8_t bits;
	} ranges[] = {
		{ 143, 0, 0x30, 8 },
		{ 255, 144, 0x190, 9 },
		{ 279, 256, 0x00, 7 },
		{ 287, 280, 0xC0, 8 },
	};
	uint32_t i = 0;
	while (symbol > ranges[i].last)
		i++;
	put_code(o, ranges[i].code + symbol - ranges[i].first, ranges[i].bits);
}

// Each length and distance code's first value and extra bits (RFC 1951, 3.2.5).
static const uint16_t length_base[LENGTH_CODES] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
	59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t length_extra[LENGTH_CODES] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
	4, 5, 5, 5, 5, 0 };
static const uint16_t distance_base[DISTANCE_CODES] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257,
	385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t distance_extra[DISTANCE_CODES] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9,
	9, 10, 10, 11, 11, 12, 12, 13, 13 };

// A match: the length's symbol and extra bits, then the distance's code and extra bits.
static void put_match(Out *o, uint32_t length, uint32_t distance)
{
	uint32_t l = LENGTH_CODES - 1;
	while (length_base[l] > length)
		l--;
	put_symbol(o, FIRST_LENGTH_SYMBOL + l);
	put_bits(o, length - length_base[l], length_extra[l]);
	uint32_t d = DISTANCE_CODES - 1;
	while (distance_base[d] > distance)
		d--;
	put_code(o, d, DISTANCE_CODE_BITS);
	put_bits(o, distance - distance_base[d], distance_extra[d]);
}

// Hash chains over the input: head[hash] the last position whose next 3 bytes have that hash, prev[position % WINDOW]
// the one before it with the same hash (-1 for none).
typedef struct {
	int32_t *head;
	int32_t *prev;
} Chains;

// A hash of the 3 bytes at p (Knuth's multiplicative hash, its top HASH_BITS bits).
static uint32_t hash3(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u >> (32 - HASH_BITS);
}

static void add_to_chains(Chains *c, const uint8_t *in, size_t n, size_t at)
{
	if (at + MIN_MATCH > n)
		return;
	const uint32_t h = hash3(in + at);
	c->prev[at % WINDOW] = c->head[h];
	c->head[h] = (int32_t)at;
}

// The longest earlier match for the bytes at `at` within the window, trying up to MAX_CHAIN candidates; its length
// (0 if none) and *distance.
static uint32_t longest_match(const Chains *c, const uint8_t *in, size_t n, size_t at, uint32_t *distance)
{
	uint32_t best = 0;
	if (at + MIN_MATCH > n)
		return 0;
	int32_t candidate = c->head[hash3(in + at)];
	const size_t limit = n - at < MAX_MATCH ? n - at : MAX_MATCH;
	for (uint32_t tries = 0; candidate >= 0 && at - (size_t)candidate <= WINDOW && tries < MAX_CHAIN; tries++) {
		uint32_t length = 0;
		while (length < limit && in[(size_t)candidate + length] == in[at + length])
			length++;
		if (length > best) {
			best = length;
			*distance = (uint32_t)(at - (size_t)candidate);
			if (length == limit)
				break;
		}
		const int32_t before = c->prev[(size_t)candidate % WINDOW];
		if (before >= candidate) // a stale entry from a position a window ago
			break;
		candidate = before;
	}
	return best;
}

// The raw bytes as one fixed-code deflate block; false if out of memory.
static bool deflate_fixed(Out *o, const uint8_t *in, size_t n)
{
	bool ok = false;
	Chains c = { avatar_malloc(sizeof(int32_t) << HASH_BITS, "a PNG"), avatar_malloc(sizeof(int32_t) * WINDOW, "a PNG") };
	if (!c.head || !c.prev)
		goto done;
	for (size_t i = 0; i < (size_t)1 << HASH_BITS; i++)
		c.head[i] = -1;
	put_bits(o, BLOCK_FINAL, 1);
	put_bits(o, BLOCK_FIXED_CODES, 2);
	for (size_t at = 0; at < n;) {
		uint32_t distance = 0;
		const uint32_t length = longest_match(&c, in, n, at, &distance);
		size_t step = 1;
		if (length >= MIN_MATCH) {
			put_match(o, length, distance);
			step = length;
		} else {
			put_symbol(o, in[at]);
		}
		for (size_t k = 0; k < step; k++, at++) // every position passed goes into the chains
			add_to_chains(&c, in, n, at);
	}
	put_symbol(o, END_OF_BLOCK);
	if (o->count)
		put_bits(o, 0, 8 - o->count);
	ok = !o->failed;
done:
	free(c.head);
	free(c.prev);
	return ok;
}

static uint32_t crc_table[256];

// The CRC-32 a PNG chunk ends with (ISO 3309, as zlib's).
static uint32_t crc32_of(const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? CRC_POLYNOMIAL ^ (c >> 1) : c >> 1;
			crc_table[i] = c;
		}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

// The Adler-32 checksum a zlib stream ends with: the sum of the bytes (plus 1) in the low half, the sum of those sums
// in the high half, both mod 65521.
static uint32_t adler32_of(const uint8_t *p, size_t n)
{
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < n; i++) {
		a = (a + p[i]) % ADLER_MOD;
		b = (b + a) % ADLER_MOD;
	}
	return b << 16 | a;
}

// A chunk: its length, its type and data, and their CRC.
static void put_chunk(Out *o, const char *type, const uint8_t *data, size_t n)
{
	put_be32(o, (uint32_t)n);
	const size_t start = o->size;
	for (int k = 0; k < 4; k++)
		put_byte(o, (uint8_t)type[k]);
	for (size_t i = 0; i < n; i++)
		put_byte(o, data[i]);
	if (!o->failed)
		put_be32(o, crc32_of(o->data + start, o->size - start));
}

// The picture as PNG stores it before compression: each row its filter byte, then the row filtered (FILTER_SUB).
// NULL if out of memory.
static uint8_t *filter_rows(const uint8_t *rgba, uint32_t width, uint32_t height, size_t *size)
{
	const size_t row = (size_t)width * BYTES_PER_TEXEL;
	*size = (row + 1) * height;
	uint8_t *raw = avatar_malloc(*size, "a PNG");
	if (!raw)
		return NULL;
	for (uint32_t y = 0; y < height; y++) {
		uint8_t *r = raw + y * (row + 1);
		const uint8_t *s = rgba + y * row;
		r[0] = FILTER_SUB;
		for (size_t i = 0; i < row; i++)
			r[1 + i] = (uint8_t)(s[i] - (i >= BYTES_PER_TEXEL ? s[i - BYTES_PER_TEXEL] : 0));
	}
	return raw;
}

// The filtered rows as a zlib stream (PNG's IDAT data) in *z; false if out of memory.
static bool compress_zlib(Out *z, const uint8_t *raw, size_t raw_size)
{
	put_byte(z, ZLIB_METHOD);
	put_byte(z, ZLIB_FLAGS);
	if (!deflate_fixed(z, raw, raw_size))
		return false;
	put_be32(z, adler32_of(raw, raw_size));
	return !z->failed;
}

int avatar_png_encode(const uint8_t *rgba, uint32_t width, uint32_t height, uint8_t **png, uint32_t *size_bytes)
{
	*png = NULL;
	*size_bytes = 0;
	int result = XSDK_AVATAR_ERROR_MEMORY;
	Out z = { 0 }, o = { 0 };
	size_t raw_size = 0;
	uint8_t *raw = filter_rows(rgba, width, height, &raw_size);
	if (!raw || !compress_zlib(&z, raw, raw_size))
		goto done;
	free(raw);
	raw = NULL;

	static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	for (int k = 0; k < 8; k++)
		put_byte(&o, signature[k]);
	// The header: width and height (big-endian u32), bit depth, colour type, then compression, filter and interlace
	// methods, each 0 (deflate, the five filters, none).
	uint8_t ihdr[13] = { [8] = BIT_DEPTH, [9] = COLOR_TYPE_RGBA };
	avatar_store_be32(ihdr, width);
	avatar_store_be32(ihdr + 4, height);
	put_chunk(&o, "IHDR", ihdr, sizeof ihdr);
	put_chunk(&o, "IDAT", z.data, z.size);
	put_chunk(&o, "IEND", NULL, 0);
	if (o.failed)
		goto done;
	*png = o.data;
	*size_bytes = (uint32_t)o.size;
	o.data = NULL;
	result = XSDK_AVATAR_OK;
done:
	free(raw);
	free(z.data);
	free(o.data);
	return result;
}
