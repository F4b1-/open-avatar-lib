// The emote wheel (wheel.h): all of it built once; drawing picks the ranges.

#include "wheel.h"

#include <math.h>
#include <string.h>

#include "font.h"
#include "mesh.h"

enum {
	SLICES = 8, // quads to a segment's arc
};

static const float pi = 3.14159265f;
static const float inner = 92.0f, outer = 255.0f; // pixels
static const float gap = 0.025f;                  // radians between segments
static const float label_scale = 2.1f, label_thickness = 2.7f;
static const float name_scale = 2.6f, name_thickness = 3.2f;
static const float ring[4] = { 0.06f, 0.07f, 0.10f, 0.72f };
static const float lit[4] = { 0.95f, 0.42f, 0.72f, 0.88f };
static const float hub[4] = { 0.03f, 0.03f, 0.05f, 0.80f };
static const float ink[4] = { 0.96f, 0.95f, 0.92f, 1.0f };
static const char hint_text[] = "HOLD LB FOR EMOTES";

static uint32_t segments;
static MeshRange ring_range, label_range, hub_range, hint_range;
static MeshRange lit_ranges[WHEEL_MAX], name_ranges[WHEEL_MAX];
static float screen_vp[16];
static float cx, cy;

// The middle of segment i, as an angle on the screen (y down: angles grow clockwise), the first at the top.
static float middle_of(uint32_t i)
{
	return -pi / 2.0f + 2.0f * pi * (float)i / (float)segments;
}

// Part of a ring between radii r0 and r1, from angle a0 to a1.
static void arc(float r0, float r1, float a0, float a1, const float color[4])
{
	static const float normal[3] = { 0, 0, -1 };
	for (uint32_t s = 0; s < SLICES; s++) {
		const float b0 = a0 + (a1 - a0) * (float)s / SLICES, b1 = a0 + (a1 - a0) * (float)(s + 1) / SLICES;
		const float p0[3] = { cx + r0 * cosf(b0), cy + r0 * sinf(b0), 0 };
		const float p1[3] = { cx + r1 * cosf(b0), cy + r1 * sinf(b0), 0 };
		const float p2[3] = { cx + r1 * cosf(b1), cy + r1 * sinf(b1), 0 };
		const float p3[3] = { cx + r0 * cosf(b1), cy + r0 * sinf(b1), 0 };
		mesh_quad4(p0, p1, p2, p3, normal, color, MATERIAL_GLOW);
	}
}

// Text centred on (x, y) on the screen, in pixels: one grid unit scale pixels, y down.
static void screen_text(const char *text, float x, float y, float scale, float thickness)
{
	const float w = font_width(text) * scale, h = FONT_HEIGHT * scale;
	font_text(text, (const float[]){ x - w / 2, y + h / 2, 0 }, (const float[]){ scale, 0, 0 },
		(const float[]){ 0, -scale, 0 }, thickness, ink, MATERIAL_GLOW);
}

void wheel_build(uint32_t width, uint32_t height, const char *const *names, uint32_t count)
{
	segments = count < WHEEL_MAX ? count : WHEEL_MAX;
	cx = (float)width / 2.0f;
	cy = (float)height / 2.0f;
	const float half = pi / (float)segments;
	uint32_t mark = mesh_mark();
	for (uint32_t i = 0; i < segments; i++)
		arc(inner, outer, middle_of(i) - half + gap, middle_of(i) + half - gap, ring);
	ring_range = mesh_since(mark);
	for (uint32_t i = 0; i < segments; i++) {
		mark = mesh_mark();
		arc(inner, outer + 10.0f, middle_of(i) - half + gap, middle_of(i) + half - gap, lit);
		lit_ranges[i] = mesh_since(mark);
	}
	mark = mesh_mark();
	// More than ten segments: smaller labels, to fit their narrower segments.
	const float fit = segments > 10 ? 10.0f / (float)segments : 1.0f;
	const float r = 0.5f * (inner + outer);
	for (uint32_t i = 0; i < segments; i++)
		screen_text(names[i], cx + r * cosf(middle_of(i)), cy + r * sinf(middle_of(i)), label_scale * fit,
			label_thickness * fit);
	label_range = mesh_since(mark);
	mark = mesh_mark();
	arc(0.0f, inner - 6.0f, 0.0f, 2.0f * pi, hub);
	arc(0.0f, inner - 6.0f, 0.0f, 2.0f * pi, hub);
	hub_range = mesh_since(mark);
	for (uint32_t i = 0; i < segments; i++) {
		mark = mesh_mark();
		screen_text(names[i], cx, cy, name_scale * (strlen(names[i]) > 6 ? 0.8f : 1.0f), name_thickness);
		name_ranges[i] = mesh_since(mark);
	}
	mark = mesh_mark();
	const float hint_scale = 1.6f;
	font_text(hint_text, (const float[]){ 40.0f, (float)height - 40.0f, 0 }, (const float[]){ hint_scale, 0, 0 },
		(const float[]){ 0, -hint_scale, 0 }, 2.2f, (const float[]){ 1, 1, 1, 0.75f }, MATERIAL_GLOW);
	hint_range = mesh_since(mark);
	// Pixels to clip space: x 0 to width left to right, y 0 to height top to bottom; depth 0.
	memset(screen_vp, 0, sizeof screen_vp);
	screen_vp[0] = 2.0f / (float)width;
	screen_vp[5] = -2.0f / (float)height;
	screen_vp[12] = -1.0f;
	screen_vp[13] = 1.0f;
	screen_vp[15] = 1.0f;
}

int wheel_pick(float x, float y)
{
	if (x * x + y * y < 0.25f || !segments)
		return -1;
	// The stick's angle as the screen's (y down), from the top round clockwise.
	float a = atan2f(-y, x) + pi / 2.0f;
	while (a < 0.0f)
		a += 2.0f * pi;
	const int i = (int)floorf(a / (2.0f * pi / (float)segments) + 0.5f);
	return i % (int)segments;
}

void wheel_draw(int selected)
{
	static const float unlit[MESH_LIGHT_VECTORS][4];
	mesh_draw(ring_range, screen_vp, unlit, MESH_OVERLAY);
	if (selected >= 0 && (uint32_t)selected < segments)
		mesh_draw(lit_ranges[selected], screen_vp, unlit, MESH_OVERLAY);
	mesh_draw(label_range, screen_vp, unlit, MESH_OVERLAY);
	mesh_draw(hub_range, screen_vp, unlit, MESH_OVERLAY);
	if (selected >= 0 && (uint32_t)selected < segments)
		mesh_draw(name_ranges[selected], screen_vp, unlit, MESH_OVERLAY);
}

void wheel_draw_hint(void)
{
	static const float unlit[MESH_LIGHT_VECTORS][4];
	mesh_draw(hint_range, screen_vp, unlit, MESH_OVERLAY);
}
