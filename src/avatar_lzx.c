// LZX as avatar assets use it (SPECS.md, section 3.2): each chunk a stream of its own with a 32 KB window, its own
// header, Huffman lengths and repeated offsets; 16-bit little-endian words read most significant bit first.
// Written from the public description of LZX (libmspack's lzxd.c documents the same algorithm; no code taken).
//
// A stream is a run of blocks, each with a 3-bit type and a 24-bit size: verbatim and aligned blocks carry Huffman
// trees and then symbols (a literal byte, or a match: a length and an offset back into what was produced); an
// uncompressed block carries its bytes as they are.

#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

enum {
	WINDOW_BITS = 15,
	POSITION_SLOTS = 30, // for a 2^15 window
	CHARS = 256,
	MAIN_SYMBOLS = CHARS + 8 * POSITION_SLOTS, // a literal byte, or a match: position slot * 8 + length header
	LENGTH_SYMBOLS = 249,
	ALIGNED_SYMBOLS = 8,
	PRETREE_SYMBOLS = 20,
	MIN_MATCH = 2,
	MAX_CODE_BITS = 16,
	MAX_EXTRA_BITS = 17, // the position slots' extra bits grow by one every two slots, up to this
	REPEATED_OFFSETS = 3,

	// A block's header: its type (3 bits), then its size in bytes (24 bits, read as 16 then 8).
	BLOCK_TYPE_BITS = 3,
	BLOCK_SIZE_HIGH_BITS = 16,
	BLOCK_SIZE_LOW_BITS = 8,
	BLOCK_VERBATIM = 1,
	BLOCK_ALIGNED = 2,
	BLOCK_UNCOMPRESSED = 3,
	ALIGNED_LENGTH_BITS = 3, // each aligned tree code length
	ALIGNED_OFFSET_BITS = 3, // the low bits of an offset an aligned block codes with its own tree

	// A match symbol, less CHARS: the length header in the low 3 bits, the position slot above.
	LENGTH_HEADER_BITS = 3,
	LENGTH_HEADER_MORE = 7, // the length goes on in the length tree

	// The pre-tree codes a tree's code lengths: 0-16 change one length by that much (mod 17); the others are runs.
	PRETREE_LENGTH_BITS = 4, // each pre-tree code length
	LENGTH_MOD = 17,
	PRETREE_ZEROS_SHORT = 17, // 4 + (4 bits) lengths of 0
	PRETREE_ZEROS_LONG = 18,  // 20 + (5 bits) lengths of 0
	PRETREE_SAME = 19,        // 4 + (1 bit) lengths of the next code's length
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
	uint32_t repeats[REPEATED_OFFSETS];
	uint32_t block_type;
	int32_t block_remaining; // bytes left in the block; below 0 when a match ran past its end
} Decoder;

static uint8_t extra_bits[POSITION_SLOTS];
static uint32_t position_base[POSITION_SLOTS];

// Each position slot's extra bits (0, 0, 0, 0, 1, 1, 2, 2, ...) and the first offset it codes.
static void init_tables(void)
{
	if (position_base[POSITION_SLOTS - 1])
		return;
	uint32_t extra = 0;
	for (uint32_t i = 0; i < POSITION_SLOTS; i += 2) {
		extra_bits[i] = extra_bits[i + 1] = (uint8_t)extra;
		if (i != 0 && extra < MAX_EXTRA_BITS)
			extra++;
	}
	for (uint32_t i = 1; i < POSITION_SLOTS; i++)
		position_base[i] = position_base[i - 1] + (1u << extra_bits[i - 1]);
}

// Tops the buffer up to more than 32 bits, a 16-bit word at a time; past the input's end, with zeros.
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

// The next `count` bits (0-16), most significant first.
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

// As read_bits, for up to 32 bits.
static uint32_t read_long(Bits *b, uint32_t count)
{
	return count > 16 ? read_bits(b, count - 16) << 16 | read_bits(b, 16) : read_bits(b, count);
}

// Whether the decoder has read past the input (and so only zeros it made up), allowing one word of padding.
static bool overrun(const Bits *b)
{
	return b->consumed > (b->in_bytes + 2) * 8;
}

// A canonical Huffman code from each symbol's code length (0 for a symbol not used); false if a length is too long.
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

// A code length changed by a pre-tree code of 0-16: lengths are stored as differences, mod 17.
static uint8_t changed_length(uint8_t length, uint32_t code)
{
	return (uint8_t)((length + LENGTH_MOD - code) % LENGTH_MOD);
}

// Updates lengths[first..last) from the stream: a pre-tree, then its codes (a change to one length, or a run).
static bool read_lengths(Decoder *d, uint8_t *lengths, uint32_t first, uint32_t last)
{
	uint8_t pre[PRETREE_SYMBOLS];
	for (uint32_t i = 0; i < PRETREE_SYMBOLS; i++)
		pre[i] = (uint8_t)read_bits(&d->bits, PRETREE_LENGTH_BITS);
	if (!build(&d->pretree, pre, PRETREE_SYMBOLS))
		return false;
	uint32_t x = first;
	while (x < last) {
		int code = decode(&d->bits, &d->pretree);
		if (code < 0)
			return false;
		if (code < PRETREE_ZEROS_SHORT) {
			lengths[x] = changed_length(lengths[x], (uint32_t)code);
			x++;
			continue;
		}
		uint32_t run;
		uint8_t value = 0;
		if (code == PRETREE_ZEROS_SHORT) {
			run = 4 + read_bits(&d->bits, 4);
		} else if (code == PRETREE_ZEROS_LONG) {
			run = 20 + read_bits(&d->bits, 5);
		} else { // PRETREE_SAME
			run = 4 + read_bits(&d->bits, 1);
			code = decode(&d->bits, &d->pretree);
			if (code < 0 || code >= PRETREE_ZEROS_SHORT)
				return false;
			value = changed_length(lengths[x], (uint32_t)code);
		}
		if (x + run > last)
			return false;
		memset(lengths + x, value, run);
		x += run;
	}
	return true;
}

// The main and length trees a verbatim or aligned block carries: lengths for the literals, the matches, the lengths.
static bool read_main_trees(Decoder *d)
{
	if (!read_lengths(d, d->main_lengths, 0, CHARS) || !read_lengths(d, d->main_lengths, CHARS, MAIN_SYMBOLS) ||
		!build(&d->main, d->main_lengths, MAIN_SYMBOLS) || !read_lengths(d, d->length_lengths, 0, LENGTH_SYMBOLS) ||
		!build(&d->length, d->length_lengths, LENGTH_SYMBOLS))
		return false;
	d->has_length_tree = d->length.max_bits > 0;
	return true;
}

// The aligned tree an aligned block carries before its main trees: 8 code lengths of 3 bits.
static bool read_aligned_tree(Decoder *d)
{
	uint8_t lengths[ALIGNED_SYMBOLS];
	for (uint32_t i = 0; i < ALIGNED_SYMBOLS; i++)
		lengths[i] = (uint8_t)read_bits(&d->bits, ALIGNED_LENGTH_BITS);
	return build(&d->aligned, lengths, ALIGNED_SYMBOLS);
}

// An uncompressed block's body, after its header: aligned to the next 16-bit word (a whole word dropped if already
// aligned), the 3 repeated offsets (u32 each), the bytes, and a padding byte if the size is odd. Copies as many bytes
// as fit into out, then restarts the bit stream after them.
static int copy_uncompressed(Decoder *d, uint32_t size, uint8_t *out, uint32_t out_bytes, uint32_t *produced)
{
	AvatarReader r = avatar_reader(d->bits.in, d->bits.in_bytes);
	avatar_reader_seek(&r, (d->bits.consumed / 16 + 1) * 2);
	for (uint32_t i = 0; i < REPEATED_OFFSETS; i++)
		d->repeats[i] = avatar_read_le32(&r);
	uint32_t count = (uint32_t)d->block_remaining;
	if (count > out_bytes - *produced)
		count = out_bytes - *produced;
	const uint8_t *bytes = avatar_read_bytes(&r, count);
	if (r.failed)
		return XSDK_AVATAR_ERROR_FORMAT;
	memcpy(out + *produced, bytes, count);
	*produced += count;
	d->block_remaining -= (int32_t)count;
	const uint32_t next = r.at + (size & 1);
	d->bits = (Bits){ .in = r.data, .in_bytes = r.size, .pos = next, .consumed = next * 8 };
	return XSDK_AVATAR_OK;
}

// Starts the next block: its header, then its trees (verbatim, aligned) or its bytes (uncompressed). A match that ran
// past the last block's end counts against this one.
static int start_block(Decoder *d, uint8_t *out, uint32_t out_bytes, uint32_t *produced)
{
	const int32_t carry = -d->block_remaining;
	d->block_type = read_bits(&d->bits, BLOCK_TYPE_BITS);
	const uint32_t size =
		read_bits(&d->bits, BLOCK_SIZE_HIGH_BITS) << BLOCK_SIZE_LOW_BITS | read_bits(&d->bits, BLOCK_SIZE_LOW_BITS);
	d->block_remaining = (int32_t)size - carry;
	if (d->block_type == BLOCK_UNCOMPRESSED)
		return copy_uncompressed(d, size, out, out_bytes, produced);
	if (d->block_type != BLOCK_VERBATIM && d->block_type != BLOCK_ALIGNED)
		return XSDK_AVATAR_ERROR_FORMAT;
	// An aligned block's own tree comes first; then both carry the same trees.
	if (d->block_type == BLOCK_ALIGNED && !read_aligned_tree(d))
		return XSDK_AVATAR_ERROR_FORMAT;
	return read_main_trees(d) ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_FORMAT;
}

// A match's offset from its position slot: slots 0-2 are the repeated offsets (1 and 2 swapped to the front), the
// others a base plus extra bits (in an aligned block, the low 3 of them from the aligned tree). 0 for a bad code.
static uint32_t match_offset(Decoder *d, uint32_t slot)
{
	uint32_t *repeats = d->repeats;
	if (slot < REPEATED_OFFSETS) {
		const uint32_t offset = repeats[slot];
		repeats[slot] = repeats[0];
		repeats[0] = offset;
		return offset;
	}
	const uint32_t extra = extra_bits[slot];
	uint32_t offset = position_base[slot] - 2;
	if (d->block_type == BLOCK_ALIGNED && extra >= ALIGNED_OFFSET_BITS) {
		offset += read_long(&d->bits, extra - ALIGNED_OFFSET_BITS) << ALIGNED_OFFSET_BITS;
		const int aligned = decode(&d->bits, &d->aligned);
		if (aligned < 0)
			return 0;
		offset += (uint32_t)aligned;
	} else if (d->block_type == BLOCK_ALIGNED && extra == 0) {
		offset = 1;
	} else {
		offset += read_long(&d->bits, extra);
	}
	repeats[2] = repeats[1];
	repeats[1] = repeats[0];
	repeats[0] = offset;
	return offset;
}

// Decodes the current block's symbols into out until the block or out is full: each a literal byte or a match.
static int decode_symbols(Decoder *d, uint8_t *out, uint32_t out_bytes, uint32_t *produced)
{
	uint32_t at = *produced;
	while (d->block_remaining > 0 && at < out_bytes) {
		const int symbol = decode(&d->bits, &d->main);
		if (symbol < 0 || overrun(&d->bits))
			return XSDK_AVATAR_ERROR_FORMAT;
		if (symbol < CHARS) {
			out[at++] = (uint8_t)symbol;
			d->block_remaining--;
			continue;
		}
		const uint32_t match = (uint32_t)symbol - CHARS;
		uint32_t length = avatar_bits(match, 0, LENGTH_HEADER_BITS);
		if (length == LENGTH_HEADER_MORE) {
			const int more = d->has_length_tree ? decode(&d->bits, &d->length) : -1;
			if (more < 0)
				return XSDK_AVATAR_ERROR_FORMAT;
			length += (uint32_t)more;
		}
		length += MIN_MATCH;
		const uint32_t offset = match_offset(d, match >> LENGTH_HEADER_BITS);
		if (offset == 0 || offset > at || length > out_bytes - at)
			return XSDK_AVATAR_ERROR_FORMAT;
		for (uint32_t i = 0; i < length; i++, at++)
			out[at] = out[at - offset];
		d->block_remaining -= (int32_t)length;
	}
	*produced = at;
	return XSDK_AVATAR_OK;
}

int avatar_lzx_expand(const uint8_t *in, uint32_t in_bytes, uint8_t *out, uint32_t out_bytes)
{
	init_tables();
	// Static rather than on the stack: the trees take about 5 KB, and chunks are expanded one at a time.
	static Decoder decoder;
	Decoder *d = &decoder;
	memset(d, 0, sizeof *d);
	d->bits = (Bits){ .in = in, .in_bytes = in_bytes };
	for (uint32_t i = 0; i < REPEATED_OFFSETS; i++)
		d->repeats[i] = 1;
	if (read_bits(&d->bits, 1))
		return XSDK_AVATAR_ERROR_UNSUPPORTED; // E8 translation; never set in the assets seen (SPECS.md, 3.2)

	uint32_t produced = 0;
	while (produced < out_bytes) {
		int result = XSDK_AVATAR_OK;
		if (d->block_remaining <= 0)
			result = start_block(d, out, out_bytes, &produced);
		if (result == XSDK_AVATAR_OK && d->block_type != BLOCK_UNCOMPRESSED)
			result = decode_symbols(d, out, out_bytes, &produced);
		if (result != XSDK_AVATAR_OK)
			return result;
	}
	return XSDK_AVATAR_OK;
}
