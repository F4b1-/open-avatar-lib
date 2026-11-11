// The stock asset pack: AvatarAssetPack.toc (FORMATS.md, section 4.1) inside the STFS package FFFE07DF00000002 (4.3).
// Big-endian. Opening it reads the table of records and their English names; the records' files are read on
// demand. Builds for the PC.

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

// The header (section 4.1): an id, the stock ids' 8-byte tail, then these.
enum {
	HEADER_BYTES = 0x30,
	VERSION_AT = 0x18,
	RECORD_COUNT_AT = 0x1C,
	LOCALE_COUNT_AT = 0x28, // version 2
	RECORDS_AT = 0x30,      // version 2
	SUPPORTED_VERSION = 2,  // the console's; version 1 (on All-Stars Avatar's disc) isn't needed here
	MAX_RECORDS = 0x10000,  // a stock id numbers its record in 16 bits; the console's pack has 1282
	MAX_LOCALES = 64,       // the console's pack has 18
};

// A record: a fixed part, then one u32 name offset per locale (English first), then its asset's offset and size.
enum {
	RECORD_MASK_AT = 0,                // u32 component mask (XSDK_AVATAR_SLOT_*)
	RECORD_GENDER_AT = 4,              // u8: XSDK_AVATAR_MALE, _FEMALE or _EITHER
	RECORD_LINK_AT = 0x9C,             // a related asset's stock id (section 3.7)
	RECORD_FIXED_BYTES = 8 + 0xB4,     // the mask, gender and flags, then 0xB4 bytes mostly zero
	RECORD_TAIL_BYTES = 8,             // the asset's u32 offset and u32 size, after the name offsets
	MAX_NAME = 96,                     // bytes kept of a name, its terminator included
};

struct XsdkAvatarPack {
	AvatarStfs stfs;
	AvatarStfsFile toc;
	uint32_t count;
	uint32_t *asset_offsets;
	uint32_t *asset_sizes;
	char (*names)[MAX_NAME];
	uint8_t (*links)[XSDK_AVATAR_ID_BYTES];
	uint32_t *masks;
	uint8_t *genders;
};

static int read_toc(const XsdkAvatarPack *pack, uint32_t offset, void *buffer, uint32_t length)
{
	return avatar_stfs_read_range(&pack->stfs, &pack->toc, offset, buffer, length);
}

// A UTF-16BE name, up to its terminator or `available` bytes, as ASCII ('?' for anything else).
static void copy_name(char *out, const uint8_t *utf16, uint32_t available)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i + 1 < available && n + 1 < MAX_NAME; i += 2) {
		const uint32_t c = avatar_be16(utf16 + i);
		if (c == 0)
			break;
		out[n++] = c < 0x80 ? (char)c : '?';
	}
	out[n] = '\0';
}

// The table's shape, from its header: how many records, how many name offsets each, how many bytes each.
typedef struct {
	uint32_t locales;
	uint32_t stride;
	uint32_t records_end;
} TocLayout;

static int read_header(XsdkAvatarPack *pack, TocLayout *layout)
{
	uint8_t header[HEADER_BYTES];
	const int result = read_toc(pack, 0, header, sizeof header);
	if (result != XSDK_AVATAR_OK)
		return result;
	pack->count = avatar_be32(header + RECORD_COUNT_AT);
	if (avatar_be32(header + VERSION_AT) != SUPPORTED_VERSION || pack->count == 0 || pack->count > MAX_RECORDS)
		return XSDK_AVATAR_ERROR_UNSUPPORTED;
	layout->locales = avatar_be32(header + LOCALE_COUNT_AT);
	if (layout->locales == 0 || layout->locales > MAX_LOCALES)
		return XSDK_AVATAR_ERROR_FORMAT;
	layout->stride = RECORD_FIXED_BYTES + 4 * layout->locales + RECORD_TAIL_BYTES;
	layout->records_end = RECORDS_AT + layout->stride * pack->count;
	return XSDK_AVATAR_OK;
}

static int allocate_tables(XsdkAvatarPack *pack)
{
	pack->asset_offsets = avatar_calloc(pack->count, sizeof(uint32_t), "the pack's table");
	pack->asset_sizes = avatar_calloc(pack->count, sizeof(uint32_t), "the pack's table");
	pack->names = avatar_calloc(pack->count, MAX_NAME, "the pack's names");
	pack->links = avatar_calloc(pack->count, XSDK_AVATAR_ID_BYTES, "the pack's table");
	pack->masks = avatar_calloc(pack->count, sizeof(uint32_t), "the pack's table");
	pack->genders = avatar_calloc(pack->count, 1, "the pack's table");
	const bool all = pack->asset_offsets && pack->asset_sizes && pack->names && pack->links && pack->masks &&
		pack->genders;
	return all ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_MEMORY;
}

// The English name's offset in record r.
static uint32_t name_offset(const uint8_t *r)
{
	return avatar_be32(r + RECORD_FIXED_BYTES);
}

// Takes each record's fields into the tables, checking its asset lies inside the file; *names_end gets where the
// names (between the records and the first asset, section 4.1) end, so they can be read in one go.
static int take_records(XsdkAvatarPack *pack, const TocLayout *layout, const uint8_t *records, uint32_t *names_end)
{
	*names_end = layout->records_end;
	for (uint32_t i = 0; i < pack->count; i++) {
		const uint8_t *r = records + RECORDS_AT + i * layout->stride;
		const uint8_t *tail = r + RECORD_FIXED_BYTES + 4 * layout->locales;
		pack->asset_offsets[i] = avatar_be32(tail);
		pack->asset_sizes[i] = avatar_be32(tail + 4);
		memcpy(pack->links[i], r + RECORD_LINK_AT, XSDK_AVATAR_ID_BYTES);
		pack->masks[i] = avatar_be32(r + RECORD_MASK_AT);
		pack->genders[i] = r[RECORD_GENDER_AT];
		if (pack->asset_offsets[i] > pack->toc.bytes || pack->asset_sizes[i] > pack->toc.bytes - pack->asset_offsets[i])
			return XSDK_AVATAR_ERROR_FORMAT;
		const uint32_t name = name_offset(r);
		if (name && name + MAX_NAME * 2 > *names_end)
			*names_end = name + MAX_NAME * 2; // a name's UTF-16 is at most two bytes a kept character
	}
	if (*names_end > pack->toc.bytes)
		*names_end = pack->toc.bytes;
	return XSDK_AVATAR_OK;
}

// Reads the stretch of names once and copies each record's.
static int take_names(XsdkAvatarPack *pack, const TocLayout *layout, const uint8_t *records, uint32_t names_end)
{
	const uint32_t start = layout->records_end;
	uint8_t *strings = avatar_malloc(names_end - start, "the pack's names");
	if (!strings)
		return XSDK_AVATAR_ERROR_MEMORY;
	const int result = read_toc(pack, start, strings, names_end - start);
	for (uint32_t i = 0; result == XSDK_AVATAR_OK && i < pack->count; i++) {
		const uint32_t name = name_offset(records + RECORDS_AT + i * layout->stride);
		if (name >= start && name < names_end)
			copy_name(pack->names[i], strings + (name - start), names_end - name);
	}
	free(strings);
	return result;
}

static int read_records(XsdkAvatarPack *pack)
{
	TocLayout layout;
	int result = read_header(pack, &layout);
	if (result == XSDK_AVATAR_OK)
		result = allocate_tables(pack);
	if (result != XSDK_AVATAR_OK)
		return result;
	uint8_t *records = avatar_malloc(layout.records_end, "the pack's records");
	if (!records)
		return XSDK_AVATAR_ERROR_MEMORY;
	uint32_t names_end;
	result = read_toc(pack, 0, records, layout.records_end);
	if (result == XSDK_AVATAR_OK)
		result = take_records(pack, &layout, records, &names_end);
	if (result == XSDK_AVATAR_OK)
		result = take_names(pack, &layout, records, names_end);
	free(records);
	return result;
}

int xsdk_avatar_pack_open(XsdkAvatarPack **out, XsdkAvatarRead read, void *context)
{
	*out = NULL;
	XsdkAvatarPack *pack = avatar_calloc(1, sizeof *pack, "the stock pack");
	if (!pack)
		return XSDK_AVATAR_ERROR_MEMORY;
	int result = avatar_stfs_open(&pack->stfs, read, context);
	if (result == XSDK_AVATAR_OK)
		result = avatar_stfs_find(&pack->stfs, "AvatarAssetPack.toc", &pack->toc);
	if (result == XSDK_AVATAR_OK && !pack->toc.consecutive)
		result = XSDK_AVATAR_ERROR_UNSUPPORTED; // the system's pack is one run of blocks
	if (result == XSDK_AVATAR_OK)
		result = read_records(pack);
	if (result != XSDK_AVATAR_OK) {
		xsdk_avatar_pack_close(pack);
		return result;
	}
	*out = pack;
	return XSDK_AVATAR_OK;
}

void xsdk_avatar_pack_close(XsdkAvatarPack *pack)
{
	if (!pack)
		return;
	free(pack->asset_offsets);
	free(pack->asset_sizes);
	free(pack->names);
	free(pack->links);
	free(pack->masks);
	free(pack->genders);
	free(pack);
}

uint32_t xsdk_avatar_pack_count(const XsdkAvatarPack *pack)
{
	return pack->count;
}

const char *xsdk_avatar_pack_name(const XsdkAvatarPack *pack, uint32_t index)
{
	return index < pack->count ? pack->names[index] : "";
}

uint32_t xsdk_avatar_pack_asset_size(const XsdkAvatarPack *pack, uint32_t index)
{
	return index < pack->count ? pack->asset_sizes[index] : 0;
}

int xsdk_avatar_pack_find(const XsdkAvatarPack *pack, const char *name, uint32_t *index)
{
	for (uint32_t i = 0; i < pack->count; i++) {
		if (strcmp(pack->names[i], name) == 0) {
			*index = i;
			return XSDK_AVATAR_OK;
		}
	}
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}

int xsdk_avatar_pack_read_asset(const XsdkAvatarPack *pack, uint32_t index, void *buffer)
{
	if (index >= pack->count || !pack->asset_sizes[index])
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	return read_toc(pack, pack->asset_offsets[index], buffer, pack->asset_sizes[index]);
}

bool xsdk_avatar_pack_link(const XsdkAvatarPack *pack, uint32_t index, uint8_t id[XSDK_AVATAR_ID_BYTES])
{
	if (index >= pack->count)
		return false;
	uint32_t record;
	if (!xsdk_avatar_id_is_stock(pack->links[index], &record))
		return false;
	memcpy(id, pack->links[index], XSDK_AVATAR_ID_BYTES);
	return true;
}

uint32_t xsdk_avatar_pack_mask(const XsdkAvatarPack *pack, uint32_t index)
{
	return index < pack->count ? pack->masks[index] : 0;
}

uint32_t xsdk_avatar_pack_gender(const XsdkAvatarPack *pack, uint32_t index)
{
	return index < pack->count ? pack->genders[index] : 0;
}

void xsdk_avatar_pack_stock_id(const XsdkAvatarPack *pack, uint32_t index, uint8_t id[XSDK_AVATAR_ID_BYTES])
{
	// Section 4.6: the component mask (u32), the record (u16), the gender (u16), big-endian, then the stock tail.
	static const uint8_t stock_tail[8] = { 0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0 };
	avatar_store_be32(id, xsdk_avatar_pack_mask(pack, index));
	avatar_store_be16(id + 4, index);
	avatar_store_be16(id + 6, xsdk_avatar_pack_gender(pack, index));
	memcpy(id + 8, stock_tail, sizeof stock_tail);
}
