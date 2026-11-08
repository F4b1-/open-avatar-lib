// PNG pictures for the rig export's baked textures (avatar_bake.c): RGBA, 8 bits a channel, each row filtered by
// the difference from the texel before it, compressed with deflate (RFC 1951): matches found through hash chains,
// coded with the fixed Huffman codes, in one block. Avatar textures are mostly flat colours, which this shrinks to a
// small part of their size. No GPU: runs on the PC too.

#include <stdlib.h>
#include <string.h>

#include "avatar_runtime.h"

enum {
	WINDOW = 32768,
	HASH_BITS = 15,
	MIN_MATCH = 3,
	MAX_MATCH = 258,
	MAX_CHAIN = 32, // candidates tried per position
};

typedef struct {
	uint8_t *data;
	size_t size, capacity;
	uint32_t bits;  // pending bits, the first in the lowest
	uint32_t count; // how many
	bool failed;
} Out;

static void put_byte(Out *o, uint8_t b)
{
	if (o->failed)
		return;
	if (o->size == o->capacity) {
		const size_t capacity = o->capacity ? o->capacity * 2 : 65536;
		uint8_t *data = realloc(o->data, capacity);
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

// The fixed literal/length code (RFC 1951, 3.2.6).
static void put_symbol(Out *o, uint32_t symbol)
{
	if (symbol < 144)
		put_code(o, 0x30 + symbol, 8);
	else if (symbol < 256)
		put_code(o, 0x190 + symbol - 144, 9);
	else if (symbol < 280)
		put_code(o, symbol - 256, 7);
	else
		put_code(o, 0xC0 + symbol - 280, 8);
}

static const uint16_t length_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67,
	83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5,
	5, 5, 0 };
static const uint16_t distance_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
	769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t distance_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
	11, 11, 12, 12, 13, 13 };

static void put_match(Out *o, uint32_t length, uint32_t distance)
{
	uint32_t l = 28;
	while (length_base[l] > length)
		l--;
	put_symbol(o, 257 + l);
	put_bits(o, length - length_base[l], length_extra[l]);
	uint32_t d = 29;
	while (distance_base[d] > distance)
		d--;
	put_code(o, d, 5);
	put_bits(o, distance - distance_base[d], distance_extra[d]);
}

static uint32_t hash3(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u >> (32 - HASH_BITS);
}

// The raw bytes as one fixed-code deflate block.
static bool deflate_fixed(Out *o, const uint8_t *in, size_t n)
{
	int32_t *head = malloc(sizeof(int32_t) << HASH_BITS), *prev = malloc(sizeof(int32_t) * WINDOW);
	if (!head || !prev) {
		free(head);
		free(prev);
		return false;
	}
	for (size_t i = 0; i < (size_t)1 << HASH_BITS; i++)
		head[i] = -1;
	put_bits(o, 1, 1); // the last block
	put_bits(o, 1, 2); // fixed codes
	size_t at = 0;
	while (at < n) {
		uint32_t best = 0, best_distance = 0;
		if (at + MIN_MATCH <= n) {
			const uint32_t h = hash3(in + at);
			int32_t candidate = head[h];
			const size_t limit = n - at < MAX_MATCH ? n - at : MAX_MATCH;
			for (uint32_t tries = 0; candidate >= 0 && at - (size_t)candidate <= WINDOW && tries < MAX_CHAIN; tries++) {
				uint32_t length = 0;
				while (length < limit && in[(size_t)candidate + length] == in[at + length])
					length++;
				if (length > best) {
					best = length;
					best_distance = (uint32_t)(at - (size_t)candidate);
					if (length == limit)
						break;
				}
				const int32_t before = prev[(size_t)candidate % WINDOW];
				if (before >= candidate)
					break;
				candidate = before;
			}
		}
		const size_t step = best >= MIN_MATCH ? best : 1;
		if (best >= MIN_MATCH)
			put_match(o, best, best_distance);
		else
			put_symbol(o, in[at]);
		// Every position passed goes into the chains.
		for (size_t k = 0; k < step; k++, at++)
			if (at + MIN_MATCH <= n) {
				const uint32_t h = hash3(in + at);
				prev[at % WINDOW] = head[h];
				head[h] = (int32_t)at;
			}
	}
	put_symbol(o, 256); // the block's end
	if (o->count)
		put_bits(o, 0, 8 - o->count);
	free(head);
	free(prev);
	return !o->failed;
}

static uint32_t crc_table[256];

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			crc_table[i] = c;
		}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
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

int avatar_png_encode(const uint8_t *rgba, uint32_t width, uint32_t height, uint8_t **png, uint32_t *size_bytes)
{
	*png = NULL;
	*size_bytes = 0;
	// Rows with their filter byte: 1, each byte less the one a texel before.
	const size_t row = (size_t)width * 4, raw_size = (row + 1) * height;
	uint8_t *raw = malloc(raw_size);
	if (!raw)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t y = 0; y < height; y++) {
		uint8_t *r = raw + y * (row + 1);
		const uint8_t *s = rgba + y * row;
		r[0] = 1;
		for (size_t i = 0; i < row; i++)
			r[1 + i] = (uint8_t)(s[i] - (i >= 4 ? s[i - 4] : 0));
	}
	Out z = { 0 };
	put_byte(&z, 0x78); // zlib: deflate, 32 KB window
	put_byte(&z, 0x01);
	bool ok = deflate_fixed(&z, raw, raw_size);
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw_size; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	put_be32(&z, b << 16 | a);
	free(raw);
	Out o = { 0 };
	static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	for (int k = 0; k < 8; k++)
		put_byte(&o, signature[k]);
	const uint8_t ihdr[13] = { (uint8_t)(width >> 24), (uint8_t)(width >> 16), (uint8_t)(width >> 8), (uint8_t)width,
		(uint8_t)(height >> 24), (uint8_t)(height >> 16), (uint8_t)(height >> 8), (uint8_t)height, 8, 6, 0, 0, 0 };
	put_chunk(&o, "IHDR", ihdr, sizeof ihdr);
	ok = ok && !z.failed;
	if (ok)
		put_chunk(&o, "IDAT", z.data, z.size);
	put_chunk(&o, "IEND", NULL, 0);
	free(z.data);
	if (!ok || o.failed) {
		free(o.data);
		return XSDK_AVATAR_ERROR_MEMORY;
	}
	*png = o.data;
	*size_bytes = (uint32_t)o.size;
	return XSDK_AVATAR_OK;
}
