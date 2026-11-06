// A profile's avatar description (SPECS.md, section 4.6): layout from All-Stars Avatar's avatar_desc.py (MIT),
// checked on the user's profile.

#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

static const uint8_t stock_tail[8] = { 0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0 };
static const uint8_t no_id[XSDK_AVATAR_ID_BYTES];

static uint32_t read_items(XsdkAvatarItem *items, const uint8_t *from, uint32_t count)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *e = from + 32 * i;
		if (memcmp(e, no_id, sizeof no_id) == 0)
			continue;
		memcpy(items[n].id, e, XSDK_AVATAR_ID_BYTES);
		items[n].slots = avatar_be16(e + 16);
		for (uint32_t k = 0; k < 3; k++)
			items[n].colors[k] = avatar_be32(e + 20 + 4 * k);
		n++;
	}
	return n;
}

int xsdk_avatar_description_parse(XsdkAvatarDescription *d, const void *bytes, uint32_t size_bytes)
{
	memset(d, 0, sizeof *d);
	if (size_bytes < XSDK_AVATAR_DESCRIPTION_BYTES)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint8_t *m = bytes;
	d->height = avatar_float(avatar_be32(m + 4));
	d->weight = avatar_float(avatar_be32(m + 8));
	for (uint32_t i = 0; i < XSDK_AVATAR_FEATURES; i++) {
		const uint8_t *id = m + 0x0C + XSDK_AVATAR_ID_BYTES * i;
		if (memcmp(id, no_id, sizeof no_id) != 0)
			memcpy(d->features[d->feature_count++], id, XSDK_AVATAR_ID_BYTES);
	}
	for (uint32_t i = 0; i < XSDK_AVATAR_COLORS; i++)
		d->colors[i] = avatar_be32(m + 0xFC + 4 * i);
	d->item_count = read_items(d->items, m + 0x120, XSDK_AVATAR_ITEMS);
	d->under_count = read_items(d->under, m + 0x300, XSDK_AVATAR_UNDER_ITEMS);
	return d->item_count ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_FORMAT;
}

bool xsdk_avatar_id_is_stock(const uint8_t *id, uint32_t *record)
{
	if (memcmp(id + 8, stock_tail, sizeof stock_tail) != 0)
		return false;
	*record = avatar_be16(id + 4);
	return true;
}

void xsdk_avatar_id_package_name(const uint8_t *id, char name[33], uint32_t *title_id)
{
	static const char digits[] = "0123456789ABCDEF";
	for (uint32_t i = 0; i < XSDK_AVATAR_ID_BYTES; i++) {
		name[2 * i] = digits[id[i] >> 4];
		name[2 * i + 1] = digits[id[i] & 15];
	}
	name[32] = '\0';
	*title_id = avatar_be32(id + 12);
}
