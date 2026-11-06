// The Avatar Thrift Store (scene.h): laid out after the user's concept picture of it, in simple shapes, a window in the
// left wall looking out on a snowy evening. Brick at the
// back, boards on the other walls, a plank floor; shelves with caps, folded shirts, game cases and shoe boxes; two
// clothing racks and a rail of hoodies; a fitting room; the counter with two old televisions under the neon sign;
// cardboard boxes, plants and track lights. x runs left to right as the store is entered, z from the back (-) to the
// front (+), y up.

#include "scene.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "font.h"
#include "mesh.h"

enum {
	MAX_SOLIDS = 32,
};

// What avatars bump into: rectangles on the floor, (min x, min z, max x, max z).
static float solids[MAX_SOLIDS][4];
static uint32_t solid_count;
static MeshRange solid_range, glow_range, glass_range;

// The window in the left wall (x = -STORE_HALF_X): from z WINDOW_BACK to WINDOW_FRONT, y WINDOW_BOTTOM to WINDOW_TOP.
#define WINDOW_BACK 0.3f
#define WINDOW_FRONT 2.1f
#define WINDOW_BOTTOM 0.9f
#define WINDOW_TOP 2.35f

// Snow falling outside: SNOW_LAYERS layers of flakes, each a block SNOW_HEIGHT high drawn twice, one over the other,
// moving down at its own speed and wrapping round, so that it never runs out.
enum {
	SNOW_LAYERS = 3,
	SNOW_FLAKES = 260, // a layer
};
#define SNOW_HEIGHT 4.0f
static MeshRange snow_ranges[SNOW_LAYERS];
static const float snow_speeds[SNOW_LAYERS] = { 0.32f, 0.47f, 0.66f }; // metres a second
static uint32_t snow_random = 0x9E3779B9u;

typedef struct {
	float r, g, b;
} Rgb;

static const Rgb dark_wood = { 0.30f, 0.19f, 0.12f };
static const Rgb light_wood = { 0.70f, 0.55f, 0.38f };
static const Rgb beige = { 0.93f, 0.88f, 0.74f };
static const Rgb garments[] = {
	{ 0.85f, 0.55f, 0.15f }, { 0.20f, 0.50f, 0.75f }, { 0.75f, 0.20f, 0.25f }, { 0.30f, 0.62f, 0.32f },
	{ 0.92f, 0.82f, 0.25f }, { 0.35f, 0.35f, 0.38f }, { 0.90f, 0.90f, 0.88f }, { 0.55f, 0.30f, 0.60f },
	{ 0.20f, 0.25f, 0.40f }, { 0.80f, 0.40f, 0.45f },
};
enum { GARMENTS = sizeof garments / sizeof garments[0] };

static void box(float x0, float y0, float z0, float x1, float y1, float z1, Rgb c, uint32_t material)
{
	mesh_box((const float[]){ x0, y0, z0 }, (const float[]){ x1, y1, z1 }, (const float[]){ c.r, c.g, c.b, 1.0f },
		material);
}

static void solid(float x0, float z0, float x1, float z1)
{
	if (solid_count == MAX_SOLIDS)
		return;
	float *s = solids[solid_count++];
	s[0] = x0;
	s[1] = z0;
	s[2] = x1;
	s[3] = z1;
}

// A quad facing a world axis: from corner a along u and v (a + u + v the opposite corner).
static void quad(const float a[3], const float u[3], const float v[3], Rgb c, float alpha, uint32_t material)
{
	mesh_quad(a, u, v, (const float[]){ c.r, c.g, c.b, alpha }, material);
}

static void room(void)
{
	const float x = STORE_HALF_X, z = STORE_HALF_Z, h = STORE_HEIGHT;
	const Rgb floor_wood = { 0.58f, 0.38f, 0.22f }, brick = { 0.62f, 0.30f, 0.22f }, boards = { 0.50f, 0.33f, 0.21f };
	const Rgb ceiling = { 0.16f, 0.15f, 0.15f }, rug = { 0.42f, 0.17f, 0.17f };
	quad((const float[]){ -x, 0, z }, (const float[]){ 2 * x, 0, 0 }, (const float[]){ 0, 0, -2 * z }, floor_wood, 1,
		MATERIAL_FLOOR);
	quad((const float[]){ -x, h, -z }, (const float[]){ 2 * x, 0, 0 }, (const float[]){ 0, 0, 2 * z }, ceiling, 1,
		MATERIAL_PLAIN);
	quad((const float[]){ -x, 0, -z }, (const float[]){ 2 * x, 0, 0 }, (const float[]){ 0, h, 0 }, brick, 1,
		MATERIAL_BRICK);
	quad((const float[]){ x, 0, z }, (const float[]){ -2 * x, 0, 0 }, (const float[]){ 0, h, 0 }, boards, 1,
		MATERIAL_BOARDS);
	// The left wall, round the window: before it, after it, under it and over it.
	const float w0 = WINDOW_BACK, w1 = WINDOW_FRONT, wy0 = WINDOW_BOTTOM, wy1 = WINDOW_TOP;
	quad((const float[]){ -x, 0, z }, (const float[]){ 0, 0, w1 - z }, (const float[]){ 0, h, 0 }, boards, 1,
		MATERIAL_BOARDS);
	quad((const float[]){ -x, 0, w0 }, (const float[]){ 0, 0, -z - w0 }, (const float[]){ 0, h, 0 }, boards, 1,
		MATERIAL_BOARDS);
	quad((const float[]){ -x, 0, w1 }, (const float[]){ 0, 0, w0 - w1 }, (const float[]){ 0, wy0, 0 }, boards, 1,
		MATERIAL_BOARDS);
	quad((const float[]){ -x, wy1, w1 }, (const float[]){ 0, 0, w0 - w1 }, (const float[]){ 0, h - wy1, 0 }, boards,
		1, MATERIAL_BOARDS);
	quad((const float[]){ x, 0, -z }, (const float[]){ 0, 0, 2 * z }, (const float[]){ 0, h, 0 }, boards, 1,
		MATERIAL_BOARDS);
	// Skirting along the walls, and a rug in the middle.
	box(-x, 0, -z, x, 0.1f, -z + 0.02f, dark_wood, MATERIAL_PLAIN);
	box(-x, 0, z - 0.02f, x, 0.1f, z, dark_wood, MATERIAL_PLAIN);
	box(-x, 0, -z, -x + 0.02f, 0.1f, z, dark_wood, MATERIAL_PLAIN);
	box(x - 0.02f, 0, -z, x, 0.1f, z, dark_wood, MATERIAL_PLAIN);
	quad((const float[]){ -1.3f, 0.003f, 2.6f }, (const float[]){ 2.6f, 0, 0 }, (const float[]){ 0, 0, -1.9f }, rug, 1,
		MATERIAL_PLAIN);
}

// A price tag facing +x (dx 1) or +z (dx 0), its text in dark ink.
static void tag(float x, float y, float z, bool faces_x, const char *text)
{
	const float w = 0.13f, hgt = 0.07f, unit = 0.0042f;
	const Rgb ink = { 0.15f, 0.12f, 0.10f };
	const float tw = font_width(text) * unit;
	if (faces_x) {
		box(x - 0.004f, y, z - w / 2, x, y + hgt, z + w / 2, beige, MATERIAL_PLAIN);
		font_text(text, (const float[]){ x + 0.001f, y + 0.022f, z + tw / 2 }, (const float[]){ 0, 0, -unit },
			(const float[]){ 0, unit, 0 }, 0.004f, (const float[]){ ink.r, ink.g, ink.b, 1 }, MATERIAL_PLAIN);
	} else {
		box(x - w / 2, y, z, x + w / 2, y + hgt, z + 0.004f, beige, MATERIAL_PLAIN);
		font_text(text, (const float[]){ x - tw / 2, y + 0.022f, z + 0.005f }, (const float[]){ unit, 0, 0 },
			(const float[]){ 0, unit, 0 }, 0.004f, (const float[]){ ink.r, ink.g, ink.b, 1 }, MATERIAL_PLAIN);
	}
}

// The shelves on the left wall: caps at the top, folded shirts, game cases, shoe boxes.
static void left_shelves(void)
{
	const float x0 = -4.98f, x1 = -4.6f, z0 = -3.6f, z1 = -0.6f;
	box(x0, 0, z0, x1, 2.5f, z0 + 0.04f, dark_wood, MATERIAL_PLAIN);
	box(x0, 0, z1 - 0.04f, x1, 2.5f, z1, dark_wood, MATERIAL_PLAIN);
	const float levels[4] = { 0.45f, 1.05f, 1.65f, 2.25f };
	for (uint32_t i = 0; i < 4; i++)
		box(x0, levels[i] - 0.04f, z0, x1, levels[i], z1, dark_wood, MATERIAL_PLAIN);
	// Caps: a crown and a peak.
	const Rgb caps[5] = { { 0.80f, 0.15f, 0.12f }, { 0.20f, 0.60f, 0.30f }, { 0.92f, 0.78f, 0.20f },
		{ 0.20f, 0.35f, 0.75f }, { 0.85f, 0.85f, 0.85f } };
	for (uint32_t i = 0; i < 5; i++) {
		const float c = -3.3f + 0.6f * (float)i;
		box(-4.92f, 2.25f, c - 0.11f, -4.7f, 2.36f, c + 0.11f, caps[i], MATERIAL_PLAIN);
		box(-4.7f, 2.25f, c - 0.09f, -4.6f, 2.27f, c + 0.09f, caps[i], MATERIAL_PLAIN);
	}
	// Folded shirts, three to a pile.
	for (uint32_t i = 0; i < 4; i++)
		for (uint32_t k = 0; k < 3; k++) {
			const float c = -3.2f + 0.72f * (float)i;
			box(-4.92f, 1.65f + 0.06f * (float)k, c - 0.15f, -4.65f, 1.71f + 0.06f * (float)k, c + 0.15f,
				garments[(i * 3 + k) % GARMENTS], MATERIAL_PLAIN);
		}
	// Game cases, standing in a row: plain colours.
	const Rgb cases[3] = { { 0.20f, 0.55f, 0.25f }, { 0.90f, 0.90f, 0.90f }, { 0.12f, 0.12f, 0.14f } };
	for (uint32_t i = 0; i < 14; i++) {
		const float c = -3.45f + 0.2f * (float)i;
		box(-4.92f, 1.05f, c, -4.78f, 1.24f, c + 0.022f, cases[i % 3], MATERIAL_PLAIN);
	}
	// Shoe boxes.
	const Rgb shoe_boxes[2] = { { 0.85f, 0.45f, 0.15f }, { 0.92f, 0.92f, 0.90f } };
	for (uint32_t i = 0; i < 6; i++) {
		const float c = -3.45f + 0.47f * (float)i;
		box(-4.94f, 0.45f, c, -4.64f, 0.58f, c + 0.38f, shoe_boxes[i % 2], MATERIAL_PLAIN);
	}
	tag(-4.59f, 1.52f, -2.0f, true, "FREE");
	tag(-4.59f, 0.92f, -1.2f, true, "0 MSP");
	solid(-5.0f, -3.65f, -4.55f, -0.55f);
}

// Hoodies on a rail on the back wall, left of the fitting room.
static void hoodie_rail(void)
{
	box(-4.3f, 1.93f, -3.88f, -2.3f, 1.96f, -3.84f, dark_wood, MATERIAL_PLAIN);
	for (uint32_t i = 0; i < 4; i++) {
		const float c = -4.0f + 0.5f * (float)i;
		box(c - 0.21f, 1.2f, -3.9f, c + 0.21f, 1.9f, -3.8f, garments[(i * 3 + 1) % GARMENTS], MATERIAL_PLAIN);
	}
	tag(-2.5f, 1.05f, -3.79f, false, "FREE");
	solid(-4.3f, -4.0f, -2.3f, -3.75f);
}

// The fitting room: two panels and a green curtain in folds.
static void fitting_room(void)
{
	box(-1.6f, 0, -4.0f, -1.55f, 2.3f, -3.0f, light_wood, MATERIAL_PLAIN);
	box(-0.25f, 0, -4.0f, -0.2f, 2.3f, -3.0f, light_wood, MATERIAL_PLAIN);
	box(-1.6f, 2.25f, -3.05f, -0.2f, 2.3f, -3.0f, light_wood, MATERIAL_PLAIN);
	for (uint32_t i = 0; i < 7; i++) {
		const float x = -1.55f + 0.1857f * (float)i, back = (i % 2) ? 0.025f : 0.0f;
		const Rgb green = { 0.15f, 0.45f - 0.06f * (float)(i % 2), 0.25f };
		box(x, 0.08f, -3.08f - back, x + 0.1857f, 2.22f, -3.04f - back, green, MATERIAL_PLAIN);
	}
	solid(-1.65f, -4.0f, -0.15f, -2.95f);
}

// A clothing rack: a rail between two poles on feet, garments hanging across it. along_x: the rail runs along x.
static void rack(float cx, float cz, float length, bool along_x, uint32_t colours, const char *price)
{
	const float h = 1.45f, half = length / 2;
	const float ax = along_x ? half : 0.02f, az = along_x ? 0.02f : half;
	box(cx - ax, h, cz - az, cx + ax, h + 0.03f, cz + az, light_wood, MATERIAL_PLAIN);
	for (int end = -1; end <= 1; end += 2) {
		const float px = along_x ? cx + (float)end * half : cx, pz = along_x ? cz : cz + (float)end * half;
		box(px - 0.02f, 0, pz - 0.02f, px + 0.02f, h + 0.03f, pz + 0.02f, light_wood, MATERIAL_PLAIN);
		const float fx = along_x ? 0.03f : 0.25f, fz = along_x ? 0.25f : 0.03f;
		box(px - fx, 0, pz - fz, px + fx, 0.03f, pz + fz, light_wood, MATERIAL_PLAIN);
	}
	const uint32_t count = (uint32_t)(length / 0.2f) - 1;
	for (uint32_t i = 0; i < count; i++) {
		const float c = -half + 0.2f * (float)(i + 1);
		const Rgb colour = garments[(colours + i * 7) % GARMENTS];
		if (along_x)
			box(cx + c - 0.025f, 0.92f, cz - 0.22f, cx + c + 0.025f, 1.42f, cz + 0.22f, colour, MATERIAL_PLAIN);
		else
			box(cx - 0.22f, 0.92f, cz + c - 0.025f, cx + 0.22f, 1.42f, cz + c + 0.025f, colour, MATERIAL_PLAIN);
	}
	if (along_x)
		tag(cx + half + 0.04f, 1.25f, cz + 0.03f, false, price);
	else
		tag(cx, 1.25f, cz + half + 0.04f, false, price);
	solid(cx - ax - 0.25f * !along_x - 0.05f, cz - az - 0.25f * along_x - 0.05f, cx + ax + 0.25f * !along_x + 0.05f,
		cz + az + 0.25f * along_x + 0.05f);
}

// An old television: a box, its screen glowing on the side it faces (+z, or -x).
static void television(float x0, float y0, float z0, float size, bool faces_z, Rgb screen)
{
	const Rgb shell = { 0.22f, 0.22f, 0.24f };
	box(x0, y0, z0, x0 + size, y0 + size * 0.85f, z0 + size * 0.9f, shell, MATERIAL_PLAIN);
	const float m = size * 0.1f;
	if (faces_z)
		quad((const float[]){ x0 + m, y0 + m, z0 + size * 0.9f + 0.002f }, (const float[]){ size - 2 * m, 0, 0 },
			(const float[]){ 0, size * 0.85f - 2 * m, 0 }, screen, 1, MATERIAL_GLOW);
	else
		quad((const float[]){ x0 - 0.002f, y0 + m, z0 + m }, (const float[]){ 0, 0, size * 0.9f - 2 * m },
			(const float[]){ 0, size * 0.85f - 2 * m, 0 }, screen, 1, MATERIAL_GLOW);
}

// The counter at the back right, two televisions and a white helmet on it.
static void counter(void)
{
	const Rgb body = { 0.42f, 0.28f, 0.18f }, top = { 0.25f, 0.16f, 0.10f };
	box(1.4f, 0, -3.3f, 3.9f, 0.95f, -2.7f, body, MATERIAL_BOARDS);
	box(1.35f, 0.95f, -3.35f, 3.95f, 1.0f, -2.65f, top, MATERIAL_PLAIN);
	television(1.6f, 1.0f, -3.2f, 0.48f, true, (Rgb){ 0.35f, 0.55f, 0.75f });
	television(3.2f, 1.0f, -3.2f, 0.42f, true, (Rgb){ 0.30f, 0.70f, 0.50f });
	box(2.5f, 1.0f, -3.1f, 2.8f, 1.3f, -2.85f, (Rgb){ 0.92f, 0.92f, 0.92f }, MATERIAL_PLAIN);
	quad((const float[]){ 2.55f, 1.08f, -2.848f }, (const float[]){ 0.2f, 0, 0 }, (const float[]){ 0, 0.14f, 0 },
		(Rgb){ 0.85f, 0.65f, 0.25f }, 1, MATERIAL_PLAIN);
	solid(1.35f, -4.0f, 3.95f, -2.65f);
}

// The sign above the counter: a dark board, the name in neon (the glow drawn later, blended).
static void neon_sign(bool glow)
{
	const float cx = 2.65f, z = -3.94f;
	if (!glow)
		box(1.2f, 1.85f, -3.99f, 4.1f, 2.8f, -3.95f, (Rgb){ 0.07f, 0.05f, 0.08f }, MATERIAL_PLAIN);
	const struct {
		const char *text;
		float unit, y;
		Rgb colour;
	} lines[2] = {
		{ "AVATAR", 0.042f, 2.33f, { 1.0f, 0.55f, 0.18f } },
		{ "THRIFT STORE", 0.034f, 1.97f, { 1.0f, 0.30f, 0.75f } },
	};
	for (uint32_t i = 0; i < 2; i++) {
		const float u = lines[i].unit, x = cx - font_width(lines[i].text) * u / 2;
		const Rgb c = lines[i].colour;
		if (glow)
			font_text(lines[i].text, (const float[]){ x, lines[i].y, z - 0.004f }, (const float[]){ u, 0, 0 },
				(const float[]){ 0, u, 0 }, 0.075f, (const float[]){ c.r, c.g, c.b, 0.28f }, MATERIAL_GLOW);
		else
			font_text(lines[i].text, (const float[]){ x, lines[i].y, z }, (const float[]){ u, 0, 0 },
				(const float[]){ 0, u, 0 }, 0.022f, (const float[]){ c.r, c.g, c.b, 1.0f }, MATERIAL_GLOW);
	}
}

// The shelves on the right wall: small televisions, boxes; two posters above.
static void right_shelves(void)
{
	const float x0 = 4.6f, x1 = 4.98f, z0 = -2.6f, z1 = 0.6f;
	box(x0, 0, z0, x1, 2.3f, z0 + 0.04f, dark_wood, MATERIAL_PLAIN);
	box(x0, 0, z1 - 0.04f, x1, 2.3f, z1, dark_wood, MATERIAL_PLAIN);
	const float levels[3] = { 0.6f, 1.3f, 2.0f };
	for (uint32_t i = 0; i < 3; i++)
		box(x0, levels[i] - 0.04f, z0, x1, levels[i], z1, dark_wood, MATERIAL_PLAIN);
	television(4.65f, 1.3f, -2.4f, 0.32f, false, (Rgb){ 0.25f, 0.45f, 0.70f });
	television(4.65f, 1.3f, -0.4f, 0.32f, false, (Rgb){ 0.65f, 0.40f, 0.30f });
	television(4.65f, 0.6f, -1.5f, 0.36f, false, (Rgb){ 0.30f, 0.65f, 0.55f });
	for (uint32_t i = 0; i < 5; i++)
		box(4.65f, 2.0f, -2.4f + 0.55f * (float)i, 4.95f, 2.22f, -2.1f + 0.55f * (float)i,
			garments[(i * 2 + 3) % GARMENTS], MATERIAL_PLAIN);
	quad((const float[]){ 4.995f, 2.2f, 1.2f }, (const float[]){ 0, 0, 0.6f }, (const float[]){ 0, 0.55f, 0 },
		(Rgb){ 0.25f, 0.35f, 0.55f }, 1, MATERIAL_PLAIN);
	quad((const float[]){ 4.995f, 2.1f, 2.2f }, (const float[]){ 0, 0, 0.5f }, (const float[]){ 0, 0.7f, 0 },
		(Rgb){ 0.65f, 0.30f, 0.25f }, 1, MATERIAL_PLAIN);
	tag(4.59f, 0.82f, 0.0f, true, "FREE");
	solid(4.55f, -2.65f, 5.0f, 0.65f);
}

// Cardboard boxes stacked at the front right.
static void cardboard(void)
{
	const Rgb card = { 0.62f, 0.46f, 0.28f }, darker = { 0.55f, 0.40f, 0.24f };
	box(3.6f, 0, 2.4f, 4.3f, 0.5f, 3.0f, card, MATERIAL_PLAIN);
	box(3.7f, 0.5f, 2.5f, 4.2f, 0.9f, 2.95f, darker, MATERIAL_PLAIN);
	box(4.3f, 0, 2.0f, 4.75f, 0.4f, 2.5f, darker, MATERIAL_PLAIN);
	solid(3.55f, 1.95f, 4.8f, 3.05f);
}

// A potted plant: a pot and leaves in heaps.
static void plant(float x, float z)
{
	const Rgb pot = { 0.62f, 0.32f, 0.20f }, leaf = { 0.22f, 0.50f, 0.22f }, leaf2 = { 0.18f, 0.42f, 0.20f };
	box(x - 0.2f, 0, z - 0.2f, x + 0.2f, 0.35f, z + 0.2f, pot, MATERIAL_PLAIN);
	box(x - 0.27f, 0.35f, z - 0.25f, x + 0.25f, 0.7f, z + 0.27f, leaf, MATERIAL_PLAIN);
	box(x - 0.17f, 0.7f, z - 0.2f, x + 0.2f, 1.05f, z + 0.15f, leaf2, MATERIAL_PLAIN);
	box(x - 0.08f, 1.05f, z - 0.1f, x + 0.12f, 1.3f, z + 0.08f, leaf, MATERIAL_PLAIN);
	solid(x - 0.3f, z - 0.3f, x + 0.3f, z + 0.3f);
}

// Track lights along the ceiling: bars, and spots with glowing lenses below.
static void track_lights(void)
{
	const Rgb black = { 0.08f, 0.08f, 0.09f }, lamp = { 1.0f, 0.92f, 0.75f };
	for (int row = -1; row <= 1; row += 2) {
		const float z = 1.6f * (float)row;
		box(-4.0f, 2.92f, z - 0.03f, 4.0f, 2.96f, z + 0.03f, black, MATERIAL_PLAIN);
		for (int i = 0; i < 4; i++) {
			const float x = -3.0f + 2.0f * (float)i;
			box(x - 0.06f, 2.77f, z - 0.06f, x + 0.06f, 2.92f, z + 0.06f, black, MATERIAL_PLAIN);
			quad((const float[]){ x - 0.05f, 2.768f, z - 0.05f }, (const float[]){ 0.1f, 0, 0 },
				(const float[]){ 0, 0, 0.1f }, lamp, 1, MATERIAL_GLOW);
		}
	}
}

// The window's frame, sill and cross-bars, in dark wood.
static void window_frame(void)
{
	const float x = -STORE_HALF_X, d = 0.12f, t = 0.07f;
	const float z0 = WINDOW_BACK, z1 = WINDOW_FRONT, y0 = WINDOW_BOTTOM, y1 = WINDOW_TOP;
	box(x, y0 - t, z0 - t, x + d, y0, z1 + t, dark_wood, MATERIAL_PLAIN);
	box(x, y1, z0 - t, x + d, y1 + t, z1 + t, dark_wood, MATERIAL_PLAIN);
	box(x, y0, z0 - t, x + d, y1, z0, dark_wood, MATERIAL_PLAIN);
	box(x, y0, z1, x + d, y1, z1 + t, dark_wood, MATERIAL_PLAIN);
	box(x, y0 - 0.1f, z0 - 0.12f, x + d + 0.1f, y0 - t, z1 + 0.12f, dark_wood, MATERIAL_PLAIN); // the sill
	const float mz = 0.5f * (z0 + z1), my = 0.5f * (y0 + y1), b = 0.025f;
	box(x + 0.03f, y0, mz - b, x + 0.06f, y1, mz + b, dark_wood, MATERIAL_PLAIN);
	box(x + 0.03f, my - b, z0, x + 0.06f, my + b, z1, dark_wood, MATERIAL_PLAIN);
}

// A box that glows: the outside is lit by the evening, not by the store's light.
static void outside_box(float x0, float y0, float z0, float x1, float y1, float z1, Rgb c)
{
	box(x0, y0, z0, x1, y1, z1, c, MATERIAL_GLOW);
}

// A snowy pine: tiers of dark green, each with snow on top.
static void pine(float x, float z, float height)
{
	const Rgb needles = { 0.07f, 0.16f, 0.14f }, snow = { 0.80f, 0.85f, 0.93f }, trunk = { 0.16f, 0.11f, 0.09f };
	outside_box(x - 0.08f, 0, z - 0.08f, x + 0.08f, 0.3f * height, z + 0.08f, trunk);
	for (uint32_t tier = 0; tier < 4; tier++) {
		const float half = (0.75f - 0.16f * (float)tier) * height * 0.5f;
		const float y0 = (0.22f + 0.2f * (float)tier) * height, y1 = y0 + 0.24f * height;
		outside_box(x - half, y0, z - half, x + half, y1, z + half, needles);
		outside_box(x - half * 0.8f, y1, z - half * 0.8f, x + half * 0.8f, y1 + 0.04f * height, z + half * 0.8f,
			snow);
	}
}

// What the window looks out on: snow on the ground, an evening sky darkening upwards, pines and a fence.
static void outside(void)
{
	const float x = -STORE_HALF_X;
	const Rgb ground = { 0.74f, 0.79f, 0.88f }, low_sky = { 0.30f, 0.37f, 0.55f }, high_sky = { 0.10f, 0.14f, 0.28f };
	const Rgb wood = { 0.20f, 0.15f, 0.13f }, snow = { 0.82f, 0.86f, 0.94f };
	quad((const float[]){ -14.0f, 0, 9.0f }, (const float[]){ 14.0f + x, 0, 0 }, (const float[]){ 0, 0, -16.0f },
		ground, 1, MATERIAL_GLOW);
	quad((const float[]){ -14.0f, 0, -7.0f }, (const float[]){ 0, 0, 16.0f }, (const float[]){ 0, 3.0f, 0 }, low_sky, 1,
		MATERIAL_GLOW);
	quad((const float[]){ -14.0f, 3.0f, -7.0f }, (const float[]){ 0, 0, 16.0f }, (const float[]){ 0, 9.0f, 0 },
		high_sky, 1, MATERIAL_GLOW);
	// Drifts heaped against the wall and here and there.
	outside_box(x - 0.6f, 0, -1.0f, x - 0.02f, 0.18f, 3.6f, snow);
	outside_box(-8.4f, 0, 0.8f, -7.2f, 0.12f, 2.0f, snow);
	pine(-7.6f, 0.4f, 2.6f);
	pine(-9.6f, 2.7f, 3.4f);
	pine(-8.4f, 4.0f, 2.2f);
	pine(-11.0f, -0.6f, 3.8f);
	pine(-6.9f, 3.1f, 1.8f);
	pine(-12.0f, 3.6f, 4.2f);
	// A fence, snow along its rail.
	for (uint32_t i = 0; i < 8; i++) {
		const float z = -1.4f + 0.8f * (float)i;
		outside_box(-6.3f, 0, z - 0.04f, -6.22f, 0.9f, z + 0.04f, wood);
		outside_box(-6.31f, 0.9f, z - 0.05f, -6.21f, 0.94f, z + 0.05f, snow);
	}
	outside_box(-6.3f, 0.6f, -1.4f, -6.24f, 0.68f, 4.2f, wood);
	outside_box(-6.31f, 0.68f, -1.4f, -6.23f, 0.71f, 4.2f, snow);
}

// One layer of flakes: small white squares scattered through a block outside the window, facing in.
static MeshRange snow_layer(float size)
{
	const uint32_t mark = mesh_mark();
	const float white[4] = { 0.95f, 0.97f, 1.0f, 0.9f };
	for (uint32_t i = 0; i < SNOW_FLAKES; i++) {
		float r[4];
		for (uint32_t k = 0; k < 4; k++) {
			snow_random = snow_random * 1664525u + 1013904223u;
			r[k] = (float)(snow_random >> 8) / (float)(1u << 24);
		}
		const float x = -10.0f + 4.85f * r[0], y = SNOW_HEIGHT * r[1], z = -1.8f + 6.2f * r[2];
		const float s = size * (0.7f + 0.6f * r[3]);
		quad((const float[]){ x, y, z }, (const float[]){ 0, 0, s }, (const float[]){ 0, s, 0 },
			(Rgb){ white[0], white[1], white[2] }, white[3], MATERIAL_GLOW);
	}
	return mesh_since(mark);
}

void scene_build(void)
{
	uint32_t mark = mesh_mark();
	room();
	window_frame();
	outside();
	left_shelves();
	hoodie_rail();
	fitting_room();
	rack(-1.8f, -1.2f, 1.8f, true, 0, "FREE");
	rack(2.6f, 1.05f, 1.7f, false, 4, "0 MSP");
	counter();
	neon_sign(false);
	right_shelves();
	cardboard();
	plant(-4.6f, 3.6f);
	plant(4.55f, -3.65f);
	track_lights();
	solid_range = mesh_since(mark);
	mark = mesh_mark();
	neon_sign(true);
	glow_range = mesh_since(mark);
	for (uint32_t l = 0; l < SNOW_LAYERS; l++)
		snow_ranges[l] = snow_layer(0.022f + 0.008f * (float)l);
	// The glass: a faint cold tint, blended over what is behind it.
	mark = mesh_mark();
	const float pane[3] = { -STORE_HALF_X + 0.04f, WINDOW_BOTTOM, WINDOW_FRONT };
	quad(pane, (const float[]){ 0, 0, WINDOW_BACK - WINDOW_FRONT }, (const float[]){ 0, WINDOW_TOP - WINDOW_BOTTOM, 0 },
		(Rgb){ 0.70f, 0.80f, 0.95f }, 0.10f, MATERIAL_GLOW);
	glass_range = mesh_since(mark);
}

void scene_draw(const float vp[16], const float light[MESH_LIGHT_VECTORS][4])
{
	mesh_draw(solid_range, vp, light, MESH_OPAQUE);
}

void scene_draw_glow(const float vp[16], const float light[MESH_LIGHT_VECTORS][4], float seconds)
{
	// Each layer of snow falls at its own speed, drifting a little, wrapped round its height: drawn twice, the second
	// copy a block above the first.
	for (uint32_t l = 0; l < SNOW_LAYERS; l++) {
		const float fall = fmodf(seconds * snow_speeds[l] + 1.3f * (float)l, SNOW_HEIGHT);
		const float drift_x = 0.12f * sinf(0.6f * seconds + 1.7f * (float)l);
		const float drift_z = 0.25f * sinf(0.4f * seconds + 2.9f * (float)l);
		for (uint32_t copy = 0; copy < 2; copy++) {
			const float place[4] = { drift_x, -fall + SNOW_HEIGHT * (float)copy, drift_z, 1.0f };
			mesh_draw_placed(snow_ranges[l], vp, light, MESH_BLENDED, place);
		}
	}
	mesh_draw(glass_range, vp, light, MESH_BLENDED);
	mesh_draw(glow_range, vp, light, MESH_BLENDED);
}

// Pushes a circle out of a rectangle (x0, z0, x1, z1), along the shorter way out.
static void push_out(float *x, float *z, float radius, const float *r)
{
	const float nx = *x < r[0] ? r[0] : *x > r[2] ? r[2] : *x;
	const float nz = *z < r[1] ? r[1] : *z > r[3] ? r[3] : *z;
	const float dx = *x - nx, dz = *z - nz, d2 = dx * dx + dz * dz;
	if (d2 >= radius * radius)
		return;
	if (d2 > 1e-8f) {
		const float d = sqrtf(d2);
		*x = nx + dx / d * radius;
		*z = nz + dz / d * radius;
		return;
	}
	const float out[4] = { *x - r[0], r[2] - *x, *z - r[1], r[3] - *z };
	uint32_t k = 0;
	for (uint32_t i = 1; i < 4; i++)
		if (out[i] < out[k])
			k = i;
	if (k == 0)
		*x = r[0] - radius;
	else if (k == 1)
		*x = r[2] + radius;
	else if (k == 2)
		*z = r[1] - radius;
	else
		*z = r[3] + radius;
}

void scene_collide(float *x, float *z, float radius)
{
	for (uint32_t s = 0; s < solid_count; s++)
		push_out(x, z, radius, solids[s]);
	const float lx = STORE_HALF_X - radius, lz = STORE_HALF_Z - radius;
	*x = *x < -lx ? -lx : *x > lx ? lx : *x;
	*z = *z < -lz ? -lz : *z > lz ? lz : *z;
}
