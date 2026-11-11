// An avatar's parts, found and decoded, with each part's colours (avatar_runtime.h). No GPU here.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "avatar_runtime.h"
#include "xsdk/log.h"

// The face features' component masks, in the Head's slot order (FORMATS.md, section 3.5).
static const uint32_t layer_masks[AVATAR_HEAD_LAYERS] = { XSDK_AVATAR_SLOT_SKIN_FEATURES,
	XSDK_AVATAR_SLOT_FACIAL_HAIR, XSDK_AVATAR_SLOT_EYEBROWS, XSDK_AVATAR_SLOT_EYES, XSDK_AVATAR_SLOT_MOUTH,
	XSDK_AVATAR_SLOT_EYE_SHADOW };

// Per slot, the description colours its red, green and blue stand for (FORMATS.md, section 3.6).
enum { WHITE = XSDK_AVATAR_COLORS, BLACK };
static const uint32_t layer_sources[AVATAR_HEAD_LAYERS][3] = {
	{ XSDK_AVATAR_COLOR_FEATURE_1, XSDK_AVATAR_COLOR_FEATURE_2, XSDK_AVATAR_COLOR_SKIN },
	{ XSDK_AVATAR_COLOR_FACIAL_HAIR, WHITE, XSDK_AVATAR_COLOR_SKIN },
	{ XSDK_AVATAR_COLOR_EYEBROWS, WHITE, XSDK_AVATAR_COLOR_SKIN },
	{ XSDK_AVATAR_COLOR_EYES, WHITE, XSDK_AVATAR_COLOR_SKIN },
	{ XSDK_AVATAR_COLOR_MOUTH, WHITE, XSDK_AVATAR_COLOR_SKIN },
	{ XSDK_AVATAR_COLOR_EYE_SHADOW, BLACK, BLACK },
};

// Face colours for stock avatars, which have no description: the user's own description's (FORMATS.md, 4.6).
static const uint32_t stock_colors[XSDK_AVATAR_COLORS] = {
	0xFFDCAD8F, 0xFF4D331F, 0xFFB56157, 0xFF727F35, 0xFF493421, 0xFF5395CA, 0xFF493421, 0xFFCF5969, 0xFFCF5969,
};

// Stand-ins for missing items, per gender: the slots an avatar looks wrong without. Missing accessories are left
// out. Stock outfits that drew right on the console (test runs 1-3, EVIDENCE.md).
typedef struct {
	uint32_t slot;
	const char *names[2]; // male, female
	uint32_t argb[2];
} StandIn;

static const StandIn stand_ins[] = {
	{ XSDK_AVATAR_SLOT_HAIR, { "High and Tight", "Low Ponytail" }, { 0, 0 } },
	{ XSDK_AVATAR_SLOT_SHIRT, { "Punk Jacket and Tee", "Hacking Jacket" }, { 0xFF3359BF, 0xFF8C3340 } },
	{ XSDK_AVATAR_SLOT_TROUSERS, { "Black Skinny Fit Jeans", "Denim Skirt" }, { 0xFF26262E, 0xFF405999 } },
	{ XSDK_AVATAR_SLOT_SHOES, { "Brown Formal Shoes", "Ballet Flats" }, { 0xFF663D1F, 0xFF26262E } },
};

enum {
	STAND_IN_SLOTS =
		XSDK_AVATAR_SLOT_HAIR | XSDK_AVATAR_SLOT_SHIRT | XSDK_AVATAR_SLOT_TROUSERS | XSDK_AVATAR_SLOT_SHOES,
	BODY_MALE = 0, // the bodies' pack records (FORMATS.md, section 4.1)
	BODY_FEMALE = 1,
	ITEM_SLOTS = 0xFFFF, // the part of a component mask a description's item keeps (avatar.h)
	NO_RECORD = UINT32_MAX, // a part from an item package, not the pack
	SKIN_SLOTS = XSDK_AVATAR_SLOT_HEAD | XSDK_AVATAR_SLOT_BODY,
};

// The face features (FORMATS.md, sections 3.5 and 4.6), and the description colour each one's own colour sets.
enum {
	FACE_FEATURES = XSDK_AVATAR_SLOT_EYES | XSDK_AVATAR_SLOT_EYEBROWS | XSDK_AVATAR_SLOT_MOUTH |
		XSDK_AVATAR_SLOT_FACIAL_HAIR | XSDK_AVATAR_SLOT_SKIN_FEATURES | XSDK_AVATAR_SLOT_EYE_SHADOW |
		XSDK_AVATAR_SLOT_NOSE | XSDK_AVATAR_SLOT_CHIN | XSDK_AVATAR_SLOT_EARS,
};
static const uint32_t feature_colors[][2] = {
	{ XSDK_AVATAR_SLOT_EYES, XSDK_AVATAR_COLOR_EYES },
	{ XSDK_AVATAR_SLOT_EYEBROWS, XSDK_AVATAR_COLOR_EYEBROWS },
	{ XSDK_AVATAR_SLOT_MOUTH, XSDK_AVATAR_COLOR_MOUTH },
	{ XSDK_AVATAR_SLOT_FACIAL_HAIR, XSDK_AVATAR_COLOR_FACIAL_HAIR },
	{ XSDK_AVATAR_SLOT_EYE_SHADOW, XSDK_AVATAR_COLOR_EYE_SHADOW },
};

static const XsdkAvatarPack *pack;

// An ARGB colour's red, green and blue, 0-1.
static void argb_to_rgb(uint32_t argb, float *rgb)
{
	for (uint32_t k = 0; k < 3; k++)
		rgb[k] = (float)avatar_bits(argb, 16 - 8 * k, 8) / 255.0f;
}

// A description colour (index into colors), or WHITE or BLACK, as red, green and blue.
static void to_rgb(uint32_t index, const uint32_t *colors, float *rgb)
{
	if (index == WHITE || index == BLACK) {
		rgb[0] = rgb[1] = rgb[2] = index == WHITE ? 1.0f : 0.0f;
		return;
	}
	argb_to_rgb(colors[index], rgb);
}

// A stock id's component mask: its first four bytes, big-endian (FORMATS.md, section 4.6).
static uint32_t id_mask(const uint8_t *id)
{
	return avatar_be32(id);
}

// Whether a part's failure stops the whole load. Running out of memory does: the avatar would come out with parts
// missing and the load reporting success. Anything else (a missing item, a file not understood) leaves the part out
// or gives it a stand-in, logged, and the load goes on.
static bool stops_load(int result)
{
	return result == XSDK_AVATAR_ERROR_MEMORY;
}

// What a part's failure gives the load: the failure if it stops it, else OK.
static int unless_stopping(int result)
{
	return stops_load(result) ? result : XSDK_AVATAR_OK;
}

// The hiding shape of a stock part: its pack record links to a body template (FORMATS.md, section 3.7).
static int load_template(AvatarPart *part)
{
	uint8_t id[XSDK_AVATAR_ID_BYTES];
	uint32_t record, size;
	if (!xsdk_avatar_pack_link(pack, part->record, id) || !xsdk_avatar_id_is_stock(id, &record) ||
		id_mask(id) != XSDK_AVATAR_SLOT_TEMPLATE)
		return XSDK_AVATAR_OK;
	void *strb;
	int result = avatar_content_read_stock(record, &strb, &size);
	if (result != XSDK_AVATAR_OK)
		return unless_stopping(result);
	result = xsdk_avatar_shape_decode(&part->shape, strb, size);
	free(strb);
	if (result != XSDK_AVATAR_OK)
		xsdk_log_write("avatar: %s: template %s: %d", part->label, xsdk_avatar_pack_name(pack, record), result);
	return unless_stopping(result);
}

// A part's model and shape: the model, and a package's own shape block (or none: not an error); a stock part's shape
// comes from the template its record links to. Only a failure that stops the load is returned for the shape.
static int decode_part(AvatarPart *part, const void *strb, uint32_t size)
{
	int result = xsdk_avatar_model_decode(&part->model, strb, size);
	if (result == XSDK_AVATAR_OK && part->record == NO_RECORD) {
		const int shape = xsdk_avatar_shape_decode(&part->shape, strb, size);
		if (stops_load(shape))
			result = shape;
	}
	return result;
}

static void free_part(AvatarPart *part)
{
	xsdk_avatar_model_free(&part->model);
	xsdk_avatar_shape_free(&part->shape);
}

// The outfit's top: the highest vertex of any part, so far.
static void raise_top(AvatarOutfit *outfit, const XsdkAvatarModel *model)
{
	for (uint32_t m = 0; m < model->mesh_count; m++) {
		const XsdkAvatarMesh *mesh = &model->meshes[m];
		for (uint32_t i = 0; i < mesh->vertex_count; i++)
			if (mesh->positions[3 * i + 1] > outfit->top)
				outfit->top = mesh->positions[3 * i + 1];
	}
}

// Decodes an STRB file (freeing it) into the outfit's next part: a stock record, or NO_RECORD for an item package.
// *out gets the part, or NULL for a model with no meshes (Bald) or one that doesn't decode.
static int add_part(
	AvatarOutfit *outfit, void *strb, uint32_t size, const char *label, uint32_t record, AvatarPart **out)
{
	*out = NULL;
	if (outfit->part_count == AVATAR_MAX_PARTS) {
		xsdk_log_write("avatar: %s left out: too many parts", label);
		free(strb);
		return XSDK_AVATAR_OK;
	}
	AvatarPart *part = &outfit->parts[outfit->part_count];
	memset(part, 0, sizeof *part);
	part->record = record;
	part->tinted = true;
	int result = decode_part(part, strb, size);
	free(strb);
	if (result != XSDK_AVATAR_OK || part->model.mesh_count == 0) {
		if (result != XSDK_AVATAR_OK)
			xsdk_log_write("avatar: %s: no model (%d)", label, result);
		free_part(part);
		return unless_stopping(result);
	}
	snprintf(part->label, sizeof part->label, "%s", label);
	if (record != NO_RECORD && (result = load_template(part)) != XSDK_AVATAR_OK) {
		free_part(part);
		return result;
	}
	if (part->shape.hidden_count || part->shape.override_count)
		xsdk_log_write("avatar: %s hides %u triangles and moves %u vertices of record %u", label,
			(unsigned)part->shape.hidden_count, (unsigned)part->shape.override_count,
			(unsigned)part->shape.target_record);
	raise_top(outfit, &part->model);
	outfit->part_count++;
	*out = part;
	return XSDK_AVATAR_OK;
}

// A face feature that isn't a texture layer (chin, nose, ears, ...): a shape that moves the Head's vertices
// (FORMATS.md, section 3.7).
static int apply_face_shape(AvatarPart *head, uint32_t record)
{
	uint32_t size;
	void *strb;
	int result = avatar_content_read_stock(record, &strb, &size);
	if (result != XSDK_AVATAR_OK)
		return unless_stopping(result);
	XsdkAvatarShape shape;
	result = xsdk_avatar_shape_decode(&shape, strb, size);
	free(strb);
	if (result != XSDK_AVATAR_OK)
		return unless_stopping(result);
	result = shape.target_record == head->record ? xsdk_avatar_shape_apply(&shape, &head->model)
												 : XSDK_AVATAR_ERROR_NOT_FOUND;
	if (result != XSDK_AVATAR_OK)
		xsdk_log_write("avatar: face shape %s: %d", xsdk_avatar_pack_name(pack, record), result);
	xsdk_avatar_shape_free(&shape);
	return XSDK_AVATAR_OK;
}

// Each part's shape applied to the part it targets: the body's vertices under a garment (FORMATS.md, section 3.7).
// The hiding is left to avatar_render.c.
static void apply_shapes(AvatarOutfit *outfit)
{
	for (uint32_t p = 0; p < outfit->part_count; p++) {
		const XsdkAvatarShape *shape = &outfit->parts[p].shape;
		if (!shape->override_count)
			continue;
		for (uint32_t q = 0; q < outfit->part_count; q++) {
			if (outfit->parts[q].record != shape->target_record)
				continue;
			const int result = xsdk_avatar_shape_apply(shape, &outfit->parts[q].model);
			if (result != XSDK_AVATAR_OK)
				xsdk_log_write("avatar: %s can't move %s's vertices: %d", outfit->parts[p].label,
					outfit->parts[q].label, result);
		}
	}
}

// A face feature that is a texture layer: the Head's slot s filled from the record's texture.
static int load_face_layer(AvatarPart *head, uint32_t s, uint32_t record)
{
	uint32_t size;
	void *strb;
	int result = avatar_content_read_stock(record, &strb, &size);
	if (result != XSDK_AVATAR_OK)
		return unless_stopping(result);
	result = xsdk_avatar_texture_decode(&head->layers[s], strb, size);
	free(strb);
	if (result != XSDK_AVATAR_OK)
		xsdk_log_write("avatar: face layer %s: %d", xsdk_avatar_pack_name(pack, record), result);
	return unless_stopping(result);
}

// The Head's slot for a face feature's mask; AVATAR_HEAD_LAYERS for a feature that is a shape (nose, chin, ears).
static uint32_t layer_slot(uint32_t mask)
{
	uint32_t s = 0;
	while (s < AVATAR_HEAD_LAYERS && layer_masks[s] != mask)
		s++;
	return s;
}

// The Head's layers filled from the description's face features, and every layer's colours.
static int fill_head(AvatarPart *head, const XsdkAvatarDescription *d)
{
	head->is_head = true;
	to_rgb(XSDK_AVATAR_COLOR_SKIN, d->colors, head->skin);
	for (uint32_t s = 0; s < AVATAR_HEAD_LAYERS; s++)
		for (uint32_t c = 0; c < 3; c++)
			to_rgb(layer_sources[s][c], d->colors, head->layer_colors[s][c]);
	for (uint32_t f = 0; f < d->feature_count; f++) {
		uint32_t record;
		if (!xsdk_avatar_id_is_stock(d->features[f], &record))
			continue;
		const uint32_t s = layer_slot(id_mask(d->features[f]));
		const int result =
			s == AVATAR_HEAD_LAYERS ? apply_face_shape(head, record) : load_face_layer(head, s, record);
		if (result != XSDK_AVATAR_OK)
			return result;
	}
	return XSDK_AVATAR_OK;
}

// The colour sources (FORMATS.md, section 3.6): the skin for the body and head, the hair colour for hair, else the
// item's own; an item given none (alpha 0) takes its colour table's first entry, or without one isn't tinted.
static void set_colors(AvatarPart *part, uint32_t slots, const uint32_t *item_colors, const XsdkAvatarDescription *d)
{
	for (uint32_t c = 0; c < 3; c++) {
		if (slots & SKIN_SLOTS)
			to_rgb(XSDK_AVATAR_COLOR_SKIN, d->colors, part->colors[c]);
		else if (slots & XSDK_AVATAR_SLOT_HAIR)
			to_rgb(XSDK_AVATAR_COLOR_HAIR, d->colors, part->colors[c]);
		else
			argb_to_rgb(item_colors[c], part->colors[c]);
	}
	const bool given = avatar_bits(item_colors[0] | item_colors[1] | item_colors[2], 24, 8) != 0; // any alpha
	if (given || (slots & (SKIN_SLOTS | XSDK_AVATAR_SLOT_HAIR)))
		return;
	if (part->model.color_entry_count)
		for (uint32_t c = 0; c < 3; c++)
			argb_to_rgb(part->model.color_entries[0][c], part->colors[c]);
	else
		part->tinted = false;
}

static uint32_t gender_of(const XsdkAvatarDescription *d)
{
	for (uint32_t i = 0; i < d->item_count; i++) {
		uint32_t record;
		if (xsdk_avatar_id_is_stock(d->items[i].id, &record) && (record == BODY_MALE || record == BODY_FEMALE))
			return record == BODY_MALE ? XSDK_AVATAR_MALE : XSDK_AVATAR_FEMALE;
	}
	return XSDK_AVATAR_MALE;
}

// Stand-ins for the slots missing items left empty.
static int add_stand_ins(AvatarOutfit *outfit, uint32_t empty, const XsdkAvatarDescription *d)
{
	const uint32_t g = outfit->gender == XSDK_AVATAR_FEMALE ? 1 : 0;
	for (size_t s = 0; s < sizeof stand_ins / sizeof stand_ins[0]; s++) {
		const StandIn *in = &stand_ins[s];
		uint32_t record, size;
		if (!(empty & in->slot) || xsdk_avatar_pack_find(pack, in->names[g], &record) != XSDK_AVATAR_OK)
			continue;
		void *strb;
		AvatarPart *part = NULL;
		int result = avatar_content_read_stock(record, &strb, &size);
		if (result == XSDK_AVATAR_OK)
			result = add_part(outfit, strb, size, in->names[g], record, &part);
		if (stops_load(result))
			return result;
		if (!part)
			continue;
		const uint32_t colors[3] = { in->argb[g], in->argb[g], in->argb[g] };
		part->slots = in->slot;
		set_colors(part, in->slot, colors, d);
		xsdk_log_write("avatar: stand-in %s", in->names[g]);
	}
	return XSDK_AVATAR_OK;
}

static void build_skeleton(AvatarOutfit *outfit)
{
	for (uint32_t p = 0; p < outfit->part_count; p++) {
		const AvatarPart *part = &outfit->parts[p];
		if (part->record != BODY_MALE && part->record != BODY_FEMALE)
			continue;
		const int result = xsdk_avatar_skeleton_build(&outfit->skeleton, &part->model);
		if (result != XSDK_AVATAR_OK) {
			xsdk_log_write("avatar: skeleton from %s: %d", part->label, result);
			outfit->skeleton.bone_count = 0;
		}
		return;
	}
	xsdk_log_write("avatar: no body, no skeleton: drawn in the bind pose");
}

// An item's STRB file, from the pack or its package: *record its pack record (NO_RECORD for a package), label its
// name or package file name.
static int read_item(const XsdkAvatarItem *item, uint32_t *record, char label[33], void **strb, uint32_t *size)
{
	if (xsdk_avatar_id_is_stock(item->id, record)) {
		snprintf(label, 33, "%s", xsdk_avatar_pack_name(pack, *record));
		return avatar_content_read_stock(*record, strb, size);
	}
	*record = NO_RECORD;
	uint32_t title;
	xsdk_avatar_id_package_name(item->id, label, &title);
	return avatar_content_read_item(item->id, strb, size);
}

// Loads the description's items into the outfit, each as a part; *wanted gets the slots of items the console
// lacks that a stand-in can fill, *filled those an item filled.
static int load_items(AvatarOutfit *outfit, const XsdkAvatarDescription *d, uint32_t *wanted, uint32_t *filled)
{
	for (uint32_t i = 0; i < d->item_count; i++) {
		const XsdkAvatarItem *item = &d->items[i];
		if (item->slots & XSDK_AVATAR_SLOT_PROP) {
			outfit->left_out_count++;
			continue;
		}
		uint32_t record, size;
		char label[33];
		void *strb;
		int result = read_item(item, &record, label, &strb, &size);
		if (stops_load(result))
			return result;
		if (result != XSDK_AVATAR_OK) {
			*wanted |= item->slots & STAND_IN_SLOTS;
			if (item->slots & STAND_IN_SLOTS)
				outfit->replaced_count++;
			else
				outfit->left_out_count++;
			continue;
		}
		*filled |= item->slots;
		AvatarPart *part;
		result = add_part(outfit, strb, size, label, record, &part);
		if (result != XSDK_AVATAR_OK)
			return result;
		if (!part)
			continue;
		part->slots = item->slots;
		set_colors(part, item->slots, item->colors, d);
		if (item->slots & XSDK_AVATAR_SLOT_HEAD) {
			result = fill_head(part, d);
			if (result != XSDK_AVATAR_OK)
				return result;
		}
	}
	return XSDK_AVATAR_OK;
}

// Whether a part fills the hat slot (a hat, or a costume's headgear).
static bool wears_hat(const AvatarOutfit *outfit)
{
	for (uint32_t p = 0; p < outfit->part_count; p++)
		if (outfit->parts[p].slots & XSDK_AVATAR_SLOT_HAT)
			return true;
	return false;
}

// The stock record a hair's record links to: its variant made to sit under a hat (FORMATS.md, section 3.7: "Raised
// Front" links to "Quiff (Hat)"). NO_RECORD if it links to none, or to something that isn't hair.
static uint32_t hat_variant(const AvatarPart *hair)
{
	uint8_t id[XSDK_AVATAR_ID_BYTES];
	uint32_t record;
	if (hair->record == NO_RECORD || !xsdk_avatar_pack_link(pack, hair->record, id) ||
		!xsdk_avatar_id_is_stock(id, &record) || id_mask(id) != XSDK_AVATAR_SLOT_HAIR)
		return NO_RECORD;
	return record;
}

// Under a hat, each stock hair swapped for its hat variant, which stays under the hat; the full hair pokes out
// through it (test run 9). The variant takes the hair's colours. A variant that doesn't load leaves the hair as it is.
static int hair_under_hat(AvatarOutfit *outfit)
{
	if (!wears_hat(outfit))
		return XSDK_AVATAR_OK;
	for (uint32_t p = 0; p < outfit->part_count; p++) {
		AvatarPart *hair = &outfit->parts[p];
		const uint32_t record = (hair->slots & XSDK_AVATAR_SLOT_HAIR) ? hat_variant(hair) : NO_RECORD;
		if (record == NO_RECORD)
			continue;
		void *strb;
		uint32_t size;
		int result = avatar_content_read_stock(record, &strb, &size);
		if (result != XSDK_AVATAR_OK)
			return unless_stopping(result);
		XsdkAvatarModel model;
		result = xsdk_avatar_model_decode(&model, strb, size);
		free(strb);
		if (result != XSDK_AVATAR_OK || model.mesh_count == 0) {
			xsdk_log_write("avatar: %s under a hat: %s doesn't load (%d)", hair->label,
				xsdk_avatar_pack_name(pack, record), result);
			if (result == XSDK_AVATAR_OK)
				xsdk_avatar_model_free(&model);
			return unless_stopping(result);
		}
		xsdk_log_write("avatar: %s under a hat: %s", hair->label, xsdk_avatar_pack_name(pack, record));
		xsdk_avatar_model_free(&hair->model);
		hair->model = model;
		hair->record = record;
		snprintf(hair->label, sizeof hair->label, "%s", xsdk_avatar_pack_name(pack, record));
	}
	return XSDK_AVATAR_OK;
}

int avatar_outfit_load(AvatarOutfit *outfit, const XsdkAvatarDescription *d)
{
	memset(outfit, 0, sizeof *outfit);
	int result = avatar_content_pack(&pack);
	if (result != XSDK_AVATAR_OK)
		return result;
	outfit->gender = gender_of(d);
	uint32_t wanted = 0, filled = 0;
	result = load_items(outfit, d, &wanted, &filled);
	if (result == XSDK_AVATAR_OK)
		result = add_stand_ins(outfit, wanted & ~filled, d);
	if (result == XSDK_AVATAR_OK)
		result = hair_under_hat(outfit);
	if (result != XSDK_AVATAR_OK) {
		avatar_outfit_free(outfit); // only running out of memory gets here: no avatar with parts missing
		return result;
	}
	build_skeleton(outfit);
	apply_shapes(outfit);
	return outfit->part_count ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_NOT_FOUND;
}

// A stock face feature into the description (FORMATS.md, sections 3.5, 4.6), its colour into the colour it sets.
static int describe_feature(XsdkAvatarDescription *d, uint32_t record, uint32_t mask, uint32_t argb)
{
	if (d->feature_count == XSDK_AVATAR_FEATURES)
		return XSDK_AVATAR_ERROR_FORMAT;
	xsdk_avatar_pack_stock_id(pack, record, d->features[d->feature_count++]);
	for (size_t c = 0; c < sizeof feature_colors / sizeof feature_colors[0]; c++)
		if (mask == feature_colors[c][0])
			d->colors[feature_colors[c][1]] = argb;
	return XSDK_AVATAR_OK;
}

// A stock item into the description, its colour as the skin (head, body), the hair colour or its own three.
static int describe_item(XsdkAvatarDescription *d, uint32_t record, uint32_t mask, uint32_t argb)
{
	if (d->item_count == XSDK_AVATAR_ITEMS)
		return XSDK_AVATAR_ERROR_FORMAT;
	XsdkAvatarItem *item = &d->items[d->item_count++];
	xsdk_avatar_pack_stock_id(pack, record, item->id);
	item->slots = mask & ITEM_SLOTS;
	if (mask & SKIN_SLOTS)
		d->colors[XSDK_AVATAR_COLOR_SKIN] = argb;
	else if (mask & XSDK_AVATAR_SLOT_HAIR)
		d->colors[XSDK_AVATAR_COLOR_HAIR] = argb;
	else
		item->colors[0] = item->colors[1] = item->colors[2] = argb;
	return XSDK_AVATAR_OK;
}

int avatar_outfit_describe_stock(XsdkAvatarDescription *d, const XsdkAvatarStockPart *parts, uint32_t count)
{
	memset(d, 0, sizeof *d);
	int result = avatar_content_pack(&pack);
	if (result != XSDK_AVATAR_OK)
		return result;
	memcpy(d->colors, stock_colors, sizeof d->colors);
	for (uint32_t p = 0; p < count && result == XSDK_AVATAR_OK; p++) {
		uint32_t record;
		if (xsdk_avatar_pack_find(pack, parts[p].name, &record) != XSDK_AVATAR_OK) {
			xsdk_log_write("avatar: no stock part named %s", parts[p].name);
			return XSDK_AVATAR_ERROR_NOT_FOUND;
		}
		const uint32_t mask = xsdk_avatar_pack_mask(pack, record);
		result = mask & FACE_FEATURES ? describe_feature(d, record, mask, parts[p].argb)
									  : describe_item(d, record, mask, parts[p].argb);
	}
	return result;
}

void avatar_outfit_hidden(const AvatarOutfit *outfit, const AvatarPart *part, uint8_t *hidden, uint32_t triangles)
{
	memset(hidden, 0, triangles);
	if (part->record == NO_RECORD)
		return;
	for (uint32_t q = 0; q < outfit->part_count; q++) {
		const XsdkAvatarShape *shape = &outfit->parts[q].shape;
		// A shape is made for its target's model as it was: 3 indices of 2 bytes a triangle.
		if (shape->target_record != part->record || shape->target_index_bytes != triangles * 3 * sizeof(uint16_t))
			continue;
		for (uint32_t k = 0; k < shape->hidden_count; k++)
			if (shape->hidden[k] < triangles)
				hidden[shape->hidden[k]] = 1;
	}
}

void avatar_outfit_free(AvatarOutfit *outfit)
{
	for (uint32_t p = 0; p < outfit->part_count; p++) {
		xsdk_avatar_model_free(&outfit->parts[p].model);
		xsdk_avatar_shape_free(&outfit->parts[p].shape);
		for (uint32_t s = 0; s < AVATAR_HEAD_LAYERS; s++)
			xsdk_avatar_texture_free(&outfit->parts[p].layers[s]);
	}
	memset(outfit, 0, sizeof *outfit);
}
