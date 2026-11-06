// The stock asset pack: AvatarAssetPack.toc (SPECS.md, section 4.1) inside the STFS package FFFE07DF00000002 (4.3).

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

enum {
	TOC_RECORD_FIXED = 8 + 0xB4,
	LINK_OFFSET = 0x9C, // a related asset's stock id in the record's head (SPECS.md, section 3.7)
	MAX_NAME = 96,
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

static int read_records(XsdkAvatarPack *pack)
{
	uint8_t header[0x30];
	int result = read_toc(pack, 0, header, sizeof header);
	if (result != XSDK_AVATAR_OK)
		return result;
	const uint32_t version = avatar_be32(header + 0x18);
	pack->count = avatar_be32(header + 0x1C);
	if (version != 2 || pack->count == 0 || pack->count > 0x10000)
		return XSDK_AVATAR_ERROR_UNSUPPORTED; // version 1 (the legacy pack) isn't needed here
	const uint32_t locales = avatar_be32(header + 0x28);
	const uint32_t first = 0x30;
	const uint32_t stride = TOC_RECORD_FIXED + 4 * locales + 8;
	if (locales == 0 || locales > 64)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t records_end = first + stride * pack->count;
	pack->asset_offsets = calloc(pack->count, sizeof(uint32_t));
	pack->asset_sizes = calloc(pack->count, sizeof(uint32_t));
	pack->names = calloc(pack->count, MAX_NAME);
	pack->links = calloc(pack->count, XSDK_AVATAR_ID_BYTES);
	pack->masks = calloc(pack->count, sizeof(uint32_t));
	pack->genders = calloc(pack->count, 1);
	uint8_t *records = malloc(records_end);
	if (!pack->asset_offsets || !pack->asset_sizes || !pack->names || !pack->links || !pack->masks || !pack->genders || !records) {
		free(records);
		return XSDK_AVATAR_ERROR_MEMORY;
	}
	result = read_toc(pack, 0, records, records_end);
	// The names lie between the records and the first asset (SPECS.md, section 4.1); that stretch is read once.
	uint32_t names_end = records_end;
	for (uint32_t i = 0; result == XSDK_AVATAR_OK && i < pack->count; i++) {
		const uint8_t *r = records + first + i * stride;
		const uint32_t name = avatar_be32(r + TOC_RECORD_FIXED);
		pack->asset_offsets[i] = avatar_be32(r + TOC_RECORD_FIXED + 4 * locales);
		pack->asset_sizes[i] = avatar_be32(r + TOC_RECORD_FIXED + 4 * locales + 4);
		memcpy(pack->links[i], r + LINK_OFFSET, XSDK_AVATAR_ID_BYTES);
		pack->masks[i] = avatar_be32(r);
		pack->genders[i] = r[4];
		if (pack->asset_offsets[i] > pack->toc.bytes || pack->asset_sizes[i] > pack->toc.bytes - pack->asset_offsets[i])
			result = XSDK_AVATAR_ERROR_FORMAT;
		if (name && name + MAX_NAME * 2 > names_end)
			names_end = name + MAX_NAME * 2;
	}
	if (names_end > pack->toc.bytes)
		names_end = pack->toc.bytes;
	uint8_t *strings = result == XSDK_AVATAR_OK ? malloc(names_end - records_end) : NULL;
	if (result == XSDK_AVATAR_OK && !strings)
		result = XSDK_AVATAR_ERROR_MEMORY;
	if (result == XSDK_AVATAR_OK)
		result = read_toc(pack, records_end, strings, names_end - records_end);
	for (uint32_t i = 0; result == XSDK_AVATAR_OK && i < pack->count; i++) {
		const uint32_t name = avatar_be32(records + first + i * stride + TOC_RECORD_FIXED);
		if (name >= records_end && name < names_end)
			copy_name(pack->names[i], strings + (name - records_end), names_end - name);
	}
	free(strings);
	free(records);
	return result;
}

int xsdk_avatar_pack_open(XsdkAvatarPack **out, XsdkAvatarRead read, void *context)
{
	*out = NULL;
	XsdkAvatarPack *pack = calloc(1, sizeof *pack);
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
	static const uint8_t stock_tail[8] = { 0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0 };
	const uint32_t mask = xsdk_avatar_pack_mask(pack, index);
	const uint32_t gender = xsdk_avatar_pack_gender(pack, index);
	const uint8_t head[8] = { (uint8_t)(mask >> 24), (uint8_t)(mask >> 16), (uint8_t)(mask >> 8), (uint8_t)mask,
		(uint8_t)(index >> 8), (uint8_t)index, (uint8_t)(gender >> 8), (uint8_t)gender };
	memcpy(id, head, sizeof head);
	memcpy(id + 8, stock_tail, sizeof stock_tail);
}
