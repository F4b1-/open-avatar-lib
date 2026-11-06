// LZX as avatar assets use it (SPECS.md, section 3.2): each chunk a stream of its own with a 32 KB window, its own
// header, Huffman lengths and repeated offsets; 16-bit little-endian words read most significant bit first.
// Written from the public description of LZX (libmspack's lzxd.c documents the same algorithm; no code taken).

#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

enum {
	WINDOW_BITS = 15,
	POSITION_SLOTS = 30, // for a 2^15 window
	CHARS = 256,
	MAIN_SYMBOLS = CHARS + 8 * POSITION_SLOTS,
	LENGTH_SYMBOLS = 249,
	ALIGNED_SYMBOLS = 8,
	PRETREE_SYMBOLS = 20,
	MIN_MATCH = 2,
	MAX_CODE_BITS = 16,
	BLOCK_VERBATIM = 1,
	BLOCK_ALIGNED = 2,
	BLOCK_UNCOMPRESSED = 3,
};

typedef struct {
	const uint8_t *in;
	uint32_t in_bytes;
	uint32_t pos; // next byte to load
	uint64_t buffer;
	uint32_t bits; // bits held in buffer
	uint32_t consumed;
} Bits;

typedef struct {
	uint16_t count[MAX_CODE_BITS + 1];
	uint32_t first[MAX_CODE_BITS + 1];
	uint16_t offset[MAX_CODE_BITS + 1];
	uint16_t symbols[MAIN_SYMBOLS];
	uint32_t max_bits;
} Huffman;

typedef struct {
	Bits bits;
	uint8_t main_lengths[MAIN_SYMBOLS];
	uint8_t length_lengths[LENGTH_SYMBOLS];
	Huffman main;
	Huffman length;
	Huffman aligned;
	Huffman pretree;
	bool has_length_tree;
	uint32_t repeats[3];
} Decoder;

static uint8_t extra_bits[POSITION_SLOTS];
static uint32_t position_base[POSITION_SLOTS];

static void init_tables(void)
{
	if (position_base[POSITION_SLOTS - 1])
		return;
	uint32_t extra = 0;
	for (uint32_t i = 0; i < POSITION_SLOTS; i += 2) {
		extra_bits[i] = extra_bits[i + 1] = (uint8_t)extra;
		if (i != 0 && extra < 17)
			extra++;
	}
	for (uint32_t i = 1; i < POSITION_SLOTS; i++)
		position_base[i] = position_base[i - 1] + (1u << extra_bits[i - 1]);
}

static void fill(Bits *b)
{
	while (b->bits <= 32) {
		uint32_t word = 0;
		if (b->pos + 1 < b->in_bytes)
			word = avatar_le16(b->in + b->pos);
		else if (b->pos < b->in_bytes)
			word = b->in[b->pos];
		b->pos += 2;
		b->buffer = b->buffer << 16 | word;
		b->bits += 16;
	}
}

static uint32_t read_bits(Bits *b, uint32_t count)
{
	if (count == 0)
		return 0;
	fill(b);
	const uint32_t value = (uint32_t)(b->buffer >> (b->bits - count)) & ((1u << count) - 1);
	b->bits -= count;
	b->buffer &= ((uint64_t)1 << b->bits) - 1;
	b->consumed += count;
	return value;
}

static uint32_t read_long(Bits *b, uint32_t count)
{
	return count > 16 ? read_bits(b, count - 16) << 16 | read_bits(b, 16) : read_bits(b, count);
}

static bool overrun(const Bits *b)
{
	return b->consumed > (b->in_bytes + 2) * 8;
}

static bool build(Huffman *h, const uint8_t *lengths, uint32_t symbol_count)
{
	memset(h->count, 0, sizeof h->count);
	h->max_bits = 0;
	for (uint32_t s = 0; s < symbol_count; s++) {
		if (lengths[s] > MAX_CODE_BITS)
			return false;
		h->count[lengths[s]]++;
		if (lengths[s] > h->max_bits)
			h->max_bits = lengths[s];
	}
	uint32_t code = 0;
	uint32_t offset = 0;
	for (uint32_t bits = 1; bits <= MAX_CODE_BITS; bits++) {
		h->first[bits] = code;
		h->offset[bits] = (uint16_t)offset;
		code = (code + h->count[bits]) << 1;
		offset += h->count[bits];
	}
	uint16_t next[MAX_CODE_BITS + 1];
	memcpy(next, h->offset, sizeof next);
	for (uint32_t s = 0; s < symbol_count; s++)
		if (lengths[s])
			h->symbols[next[lengths[s]]++] = (uint16_t)s;
	return true;
}

// The next symbol, or -1 for a code no symbol has.
static int decode(Bits *b, const Huffman *h)
{
	uint32_t code = 0;
	for (uint32_t bits = 1; bits <= h->max_bits; bits++) {
		code = code << 1 | read_bits(b, 1);
		if (code - h->first[bits] < h->count[bits])
			return h->symbols[h->offset[bits] + code - h->first[bits]];
	}
	return -1;
}

static bool read_lengths(Decoder *d, uint8_t *lengths, uint32_t first, uint32_t last)
{
	uint8_t pre[PRETREE_SYMBOLS];
	for (uint32_t i = 0; i < PRETREE_SYMBOLS; i++)
		pre[i] = (uint8_t)read_bits(&d->bits, 4);
	if (!build(&d->pretree, pre, PRETREE_SYMBOLS))
		return false;
	uint32_t x = first;
	while (x < last) {
		int z = decode(&d->bits, &d->pretree);
		if (z < 0)
			return false;
		uint32_t run = 0;
		if (z == 17) {
			run = 4 + read_bits(&d->bits, 4);
		} else if (z == 18) {
			run = 20 + read_bits(&d->bits, 5);
		} else if (z == 19) {
			run = 4 + read_bits(&d->bits, 1);
			z = decode(&d->bits, &d->pretree);
			if (z < 0 || z > 16 || x + run > last)
				return false;
			const uint8_t value = (uint8_t)((lengths[x] + 17 - z) % 17);
			for (uint32_t i = 0; i < run; i++)
				lengths[x++] = value;
			continue;
		} else {
			lengths[x] = (uint8_t)((lengths[x] + 17 - z) % 17);
			x++;
			continue;
		}
		if (x + run > last)
			return false;
		memset(lengths + x, 0, run);
		x += run;
	}
	return true;
}

int avatar_lzx_expand(const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t out_bytes)
{
	init_tables();
	static Decoder decoder;
	Decoder *d = &decoder;
	memset(d, 0, sizeof *d);
	d->bits = (Bits){ .in = in, .in_bytes = in_bytes };
	d->repeats[0] = d->repeats[1] = d->repeats[2] = 1;
	if (read_bits(&d->bits, 1))
		return XSDK_AVATAR_ERROR_UNSUPPORTED; // E8 translation; never set in the assets seen (SPECS.md, 3.2)

	uint32_t produced = 0;
	int32_t block_remaining = 0;
	uint32_t block_type = 0;
	int32_t carry = 0; // bytes a match ran past its block, taken off the next one
	while (produced < out_bytes) {
		if (block_remaining <= 0) {
			carry = -block_remaining;
			block_type = read_bits(&d->bits, 3);
			const uint32_t size = read_bits(&d->bits, 16) << 8 | read_bits(&d->bits, 8);
			block_remaining = (int32_t)size - carry;
			if (block_type == BLOCK_ALIGNED) {
				uint8_t lengths[ALIGNED_SYMBOLS];
				for (uint32_t i = 0; i < ALIGNED_SYMBOLS; i++)
					lengths[i] = (uint8_t)read_bits(&d->bits, 3);
				if (!build(&d->aligned, lengths, ALIGNED_SYMBOLS))
					return XSDK_AVATAR_ERROR_FORMAT;
			}
			if (block_type == BLOCK_VERBATIM || block_type == BLOCK_ALIGNED) {
				if (!read_lengths(d, d->main_lengths, 0, CHARS) ||
					!read_lengths(d, d->main_lengths, CHARS, MAIN_SYMBOLS) ||
					!build(&d->main, d->main_lengths, MAIN_SYMBOLS) ||
					!read_lengths(d, d->length_lengths, 0, LENGTH_SYMBOLS) ||
					!build(&d->length, d->length_lengths, LENGTH_SYMBOLS))
					return XSDK_AVATAR_ERROR_FORMAT;
				d->has_length_tree = d->length.max_bits > 0;
			} else if (block_type == BLOCK_UNCOMPRESSED) {
				// Align to the next 16-bit word, dropping a whole word if already aligned, then 3 repeated offsets.
				const uint32_t aligned = (d->bits.consumed / 16 + 1) * 16;
				uint32_t byte = aligned / 8;
				if (byte + 12 > in_bytes)
					return XSDK_AVATAR_ERROR_FORMAT;
				for (uint32_t i = 0; i < 3; i++, byte += 4)
					d->repeats[i] = avatar_le32(in + byte);
				uint32_t count = (uint32_t)block_remaining;
				if (count > out_bytes - produced)
					count = out_bytes - produced;
				if (byte + count > in_bytes)
					return XSDK_AVATAR_ERROR_FORMAT;
				memcpy(out + produced, in + byte, count);
				produced += count;
				byte += count + (size & 1);
				block_remaining -= (int32_t)count;
				d->bits = (Bits){ .in = in, .in_bytes = in_bytes, .pos = byte, .consumed = byte * 8 };
				continue;
			} else {
				return XSDK_AVATAR_ERROR_FORMAT;
			}
		}
		while (block_remaining > 0 && produced < out_bytes) {
			const int symbol = decode(&d->bits, &d->main);
			if (symbol < 0 || overrun(&d->bits))
				return XSDK_AVATAR_ERROR_FORMAT;
			if (symbol < CHARS) {
				out[produced++] = (uint8_t)symbol;
				block_remaining--;
				continue;
			}
			const uint32_t m = (uint32_t)symbol - CHARS;
			uint32_t length = m & 7;
			if (length == 7) {
				if (!d->has_length_tree)
					return XSDK_AVATAR_ERROR_FORMAT;
				const int extra = decode(&d->bits, &d->length);
				if (extra < 0)
					return XSDK_AVATAR_ERROR_FORMAT;
				length += (uint32_t)extra;
			}
			length += MIN_MATCH;
			const uint32_t slot = m >> 3;
			uint32_t offset;
			if (slot == 0) {
				offset = d->repeats[0];
			} else if (slot == 1) {
				offset = d->repeats[1];
				d->repeats[1] = d->repeats[0];
				d->repeats[0] = offset;
			} else if (slot == 2) {
				offset = d->repeats[2];
				d->repeats[2] = d->repeats[0];
				d->repeats[0] = offset;
			} else {
				const uint32_t extra = extra_bits[slot];
				offset = position_base[slot] - 2;
				if (block_type == BLOCK_ALIGNED && extra >= 3) {
					if (extra > 3)
						offset += read_long(&d->bits, extra - 3) << 3;
					const int aligned = decode(&d->bits, &d->aligned);
					if (aligned < 0)
						return XSDK_AVATAR_ERROR_FORMAT;
					offset += (uint32_t)aligned;
				} else if (block_type == BLOCK_ALIGNED && extra == 0) {
					offset = 1;
				} else {
					offset += read_long(&d->bits, extra);
				}
				d->repeats[2] = d->repeats[1];
				d->repeats[1] = d->repeats[0];
				d->repeats[0] = offset;
			}
			if (offset == 0 || offset > produced || length > out_bytes - produced)
				return XSDK_AVATAR_ERROR_FORMAT;
			for (uint32_t i = 0; i < length; i++, produced++)
				out[produced] = out[produced - offset];
			block_remaining -= (int32_t)length;
		}
	}
	return XSDK_AVATAR_OK;
}
