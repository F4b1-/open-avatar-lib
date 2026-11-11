// A profile's avatar description (FORMATS.md, section 4.6): layout from All-Stars Avatar's avatar_desc.py (MIT),
// checked on the user's profile. Big-endian throughout. Builds for the PC.

#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

// The description's layout: byte offsets into its XSDK_AVATAR_DESCRIPTION_BYTES.
enum {
	HEIGHT_AT = 0x04,   // f32, inferred
	WEIGHT_AT = 0x08,   // f32, inferred
	FEATURES_AT = 0x0C, // XSDK_AVATAR_FEATURES face-feature ids, all zeros where none
	COLORS_AT = 0xFC,   // XSDK_AVATAR_COLORS ARGB colours, in XSDK_AVATAR_COLOR_* order
	ITEMS_AT = 0x120,   // XSDK_AVATAR_ITEMS item entries
	UNDER_AT = 0x300,   // XSDK_AVATAR_UNDER_ITEMS item entries: the outfit under a costume, inferred
};

// An item entry: its id, its slot mask (u16, then a u16 0), its three custom colours.
enum {
	ITEM_SLOTS_AT = 16,
	ITEM_COLORS_AT = 20,
	ITEM_ENTRY_BYTES = 32,
};

// A stock id: component mask (u32), record index in the pack (u16), gender (u16), then this tail; any other id is an
// item package's, its last four bytes the title it belongs to.
enum {
	STOCK_RECORD_AT = 4,
	STOCK_TAIL_AT = 8,
	TITLE_ID_AT = 12,
};
static const uint8_t stock_tail[8] = { 0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0 };
static const uint8_t no_id[XSDK_AVATAR_ID_BYTES];

// Reads count item entries from `from`, keeping those with an id; returns how many.
static uint32_t read_items(XsdkAvatarItem *items, const uint8_t *from, uint32_t count)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *entry = from + ITEM_ENTRY_BYTES * i;
		if (memcmp(entry, no_id, sizeof no_id) == 0)
			continue;
		memcpy(items[n].id, entry, XSDK_AVATAR_ID_BYTES);
		items[n].slots = avatar_be16(entry + ITEM_SLOTS_AT);
		for (uint32_t k = 0; k < 3; k++)
			items[n].colors[k] = avatar_be32(entry + ITEM_COLORS_AT + 4 * k);
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
	d->height = avatar_float(avatar_be32(m + HEIGHT_AT));
	d->weight = avatar_float(avatar_be32(m + WEIGHT_AT));
	for (uint32_t i = 0; i < XSDK_AVATAR_FEATURES; i++) {
		const uint8_t *id = m + FEATURES_AT + XSDK_AVATAR_ID_BYTES * i;
		if (memcmp(id, no_id, sizeof no_id) != 0)
			memcpy(d->features[d->feature_count++], id, XSDK_AVATAR_ID_BYTES);
	}
	for (uint32_t i = 0; i < XSDK_AVATAR_COLORS; i++)
		d->colors[i] = avatar_be32(m + COLORS_AT + 4 * i);
	d->item_count = read_items(d->items, m + ITEMS_AT, XSDK_AVATAR_ITEMS);
	d->under_count = read_items(d->under, m + UNDER_AT, XSDK_AVATAR_UNDER_ITEMS);
	return d->item_count ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_FORMAT;
}

bool xsdk_avatar_id_is_stock(const uint8_t *id, uint32_t *record)
{
	if (memcmp(id + STOCK_TAIL_AT, stock_tail, sizeof stock_tail) != 0)
		return false;
	*record = avatar_be16(id + STOCK_RECORD_AT);
	return true;
}

void xsdk_avatar_id_package_name(const uint8_t *id, char name[33], uint32_t *title_id)
{
	static const char digits[] = "0123456789ABCDEF";
	for (uint32_t i = 0; i < XSDK_AVATAR_ID_BYTES; i++) {
		name[2 * i] = digits[avatar_bits(id[i], 4, 4)];
		name[2 * i + 1] = digits[avatar_bits(id[i], 0, 4)];
	}
	name[2 * XSDK_AVATAR_ID_BYTES] = '\0';
	*title_id = avatar_be32(id + TITLE_ID_AT);
}
