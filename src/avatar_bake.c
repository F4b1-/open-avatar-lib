// Baking a part's material into one picture for the rig export (avatar_export.c, SPECS.md section 4.9): the
// textures the renderer mixes per pixel (avatar_render.c, tools/gen_shaders.py) mixed once on the CPU, in the base
// texture's coordinates. Every material samples its textures with texture coordinate sets of their own (SPECS.md,
// section 3.6), so the bake draws the material's triangles into the picture and, inside each, finds where the other
// textures are sampled by interpolating their sets. No GPU: runs on the PC too.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_runtime.h"

enum {
	BAKE_MIN = 64,           // texels a side, at least: the flat 32x32 textures get room for their seams
	BAKE_MAX = 512,          // and at most
	HEAD_BAKE = 256,         // the Head's, whose layers are 64 to 128 texels over a part of it
	MAX_LAYERS = AVATAR_HEAD_LAYERS,
	DILATE_PASSES = 8,       // texels grown out from the drawn triangles, for filtering across seams
	ITEM_BASE = 0,           // an item's layers
	ITEM_MASK = 1,
	ITEM_DECAL = 2,
};

// A texture decoded, the coordinate set it is sampled with, and how it addresses outside 0-1.
typedef struct {
	uint32_t width, height;
	uint8_t *rgba;
	uint32_t uv_set;
	bool clamp;
} Layer;

// The bake's picture as floats while it is made, and which texels a triangle covered.
typedef struct {
	uint32_t width, height;
	float *color; // 4 per texel
	uint8_t *covered;
} Canvas;

static void rgb565(uint16_t c, uint8_t out[3])
{
	const uint32_t r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
	out[0] = (uint8_t)(r << 3 | r >> 2);
	out[1] = (uint8_t)(g << 2 | g >> 4);
	out[2] = (uint8_t)(b << 3 | b >> 2);
}

// A 16-bit little-endian word of a block whose 16-bit words were stored byte-swapped (the 8-in-16 endian mode).
static uint32_t word16(const uint8_t *block, uint32_t at)
{
	return (uint32_t)block[at + 1] | (uint32_t)block[at] << 8;
}

// One 4x4 block (research/texture.py's dxt1_block and dxt5_block) into rows of width texels.
static void decode_block(const uint8_t *block, bool dxt5, uint8_t *out, uint32_t width)
{
	uint8_t alpha[8] = { 255, 255, 255, 255, 255, 255, 255, 255 };
	uint64_t alpha_bits = 0;
	const uint8_t *colors = block;
	if (dxt5) {
		// Bytes 0-7: two alphas and 16 3-bit indices; their 16-bit words swapped like the rest.
		uint8_t a[8];
		for (uint32_t k = 0; k < 8; k++)
			a[k] = block[k ^ 1];
		alpha[0] = a[0];
		alpha[1] = a[1];
		if (a[0] > a[1]) {
			for (uint32_t i = 1; i < 7; i++)
				alpha[1 + i] = (uint8_t)(((6 - i) * a[0] + i * a[1]) / 6);
		} else {
			for (uint32_t i = 1; i < 5; i++)
				alpha[1 + i] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5);
			alpha[6] = 0;
			alpha[7] = 255;
		}
		for (uint32_t k = 0; k < 6; k++)
			alpha_bits |= (uint64_t)a[2 + k] << (8 * k);
		colors = block + 8;
	}
	const uint16_t c0 = (uint16_t)word16(colors, 0), c1 = (uint16_t)word16(colors, 2);
	const uint32_t bits = word16(colors, 4) | word16(colors, 6) << 16;
	uint8_t p[4][4];
	rgb565(c0, p[0]);
	rgb565(c1, p[1]);
	p[0][3] = p[1][3] = p[2][3] = p[3][3] = 255;
	const bool four = dxt5 || c0 > c1;
	for (uint32_t c = 0; c < 3; c++) {
		if (four) {
			p[2][c] = (uint8_t)((2 * p[0][c] + p[1][c]) / 3);
			p[3][c] = (uint8_t)((p[0][c] + 2 * p[1][c]) / 3);
		} else {
			p[2][c] = (uint8_t)((p[0][c] + p[1][c]) / 2);
			p[3][c] = 0;
		}
	}
	if (!four)
		p[3][3] = 0;
	for (uint32_t i = 0; i < 16; i++) {
		uint8_t *texel = out + ((i / 4) * width + i % 4) * 4;
		memcpy(texel, p[bits >> (2 * i) & 3], 4);
		if (dxt5)
			texel[3] = alpha[alpha_bits >> (3 * i) & 7];
	}
}

// A texture's frame as RGBA bytes, rows from the top; false if it has no pixels or a format we don't know.
static bool decode(const XsdkAvatarTexture *t, uint32_t frame, Layer *out)
{
	const bool dxt5 = t->format == XSDK_AVATAR_TEXTURE_DXT5;
	if (!t->data || (!dxt5 && t->format != XSDK_AVATAR_TEXTURE_DXT1) || t->width < 4 || t->height < 4 ||
		frame >= t->frame_count)
		return false;
	const uint32_t block_bytes = dxt5 ? 16 : 8, across = t->width / 4, down = t->height / 4;
	if ((size_t)across * down * block_bytes > t->frame_bytes)
		return false;
	out->rgba = malloc((size_t)t->width * t->height * 4);
	if (!out->rgba)
		return false;
	out->width = t->width;
	out->height = t->height;
	const uint8_t *blocks = t->data + (size_t)frame * t->frame_bytes;
	for (uint32_t by = 0; by < down; by++)
		for (uint32_t bx = 0; bx < across; bx++)
			decode_block(blocks + ((size_t)by * across + bx) * block_bytes, dxt5,
				out->rgba + ((size_t)by * 4 * t->width + bx * 4) * 4, t->width);
	return true;
}

static int wrap_or_clamp(int i, int n, bool clamp)
{
	if (clamp)
		return i < 0 ? 0 : i >= n ? n - 1 : i;
	i %= n;
	return i < 0 ? i + n : i;
}

// Bilinear, as the GPU samples it: texel centres at half texels, wrapped or clamped. Colours 0-1.
static void sample(const Layer *l, float u, float v, float out[4])
{
	if (!l->rgba) {
		out[0] = out[1] = out[2] = out[3] = 0.0f; // a missing texture samples transparent black (the renderer's blank)
		return;
	}
	const float x = u * (float)l->width - 0.5f, y = v * (float)l->height - 0.5f;
	const float fx = floorf(x), fy = floorf(y), ax = x - fx, ay = y - fy;
	const int x0 = (int)fx, y0 = (int)fy;
	const int xs[2] = { wrap_or_clamp(x0, (int)l->width, l->clamp), wrap_or_clamp(x0 + 1, (int)l->width, l->clamp) };
	const int ys[2] = { wrap_or_clamp(y0, (int)l->height, l->clamp), wrap_or_clamp(y0 + 1, (int)l->height, l->clamp) };
	const float w[4] = { (1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay };
	for (uint32_t c = 0; c < 4; c++) {
		float s = 0.0f;
		for (uint32_t k = 0; k < 4; k++)
			s += w[k] * (float)l->rgba[((size_t)ys[k / 2] * l->width + (size_t)xs[k % 2]) * 4 + c];
		out[c] = s / 255.0f;
	}
}

static const float *uv_at(const XsdkAvatarMesh *mesh, uint32_t vertex, uint32_t set)
{
	static const float none[2] = { 0.0f, 0.0f };
	if (!mesh->uv_sets)
		return none;
	return mesh->uvs + 2 * ((size_t)mesh->uv_sets * vertex + (set < mesh->uv_sets ? set : 0));
}

static void mix3(float *c, const float *to, float amount)
{
	for (uint32_t k = 0; k < 3; k++)
		c[k] += (to[k] - c[k]) * amount;
}

// The colour at a point of a triangle, its three vertices weighted by b: the renderer's mixing (SPECS.md, 3.6).
static void shade(const AvatarPart *part, const Layer *layers, uint32_t count, const XsdkAvatarMesh *mesh,
	const uint32_t *v, const float *b, bool see_through, float out[4])
{
	float t[MAX_LAYERS][4];
	for (uint32_t k = 0; k < count; k++) {
		const float *a = uv_at(mesh, v[0], layers[k].uv_set), *p = uv_at(mesh, v[1], layers[k].uv_set),
					*q = uv_at(mesh, v[2], layers[k].uv_set);
		sample(&layers[k], b[0] * a[0] + b[1] * p[0] + b[2] * q[0], b[0] * a[1] + b[1] * p[1] + b[2] * q[1], t[k]);
	}
	if (part->is_head) {
		// The skin, then each layer by its alpha, its red, green and blue standing for the layer's colours.
		memcpy(out, part->skin, 3 * sizeof(float));
		for (uint32_t k = 0; k < count; k++) {
			float c[3];
			for (uint32_t i = 0; i < 3; i++)
				c[i] = t[k][0] * part->layer_colors[k][0][i] + t[k][1] * part->layer_colors[k][1][i] +
					t[k][2] * part->layer_colors[k][2][i];
			mix3(out, c, t[k][3]);
		}
		out[3] = 1.0f;
		return;
	}
	memcpy(out, t[ITEM_BASE], 3 * sizeof(float));
	if (part->tinted) {
		const float *m = t[ITEM_MASK];
		float tint[3];
		for (uint32_t i = 0; i < 3; i++)
			tint[i] = m[0] * part->colors[0][i] + m[1] * part->colors[1][i] + m[2] * part->colors[2][i];
		mix3(out, tint, m[3]);
	}
	mix3(out, t[ITEM_DECAL], t[ITEM_DECAL][3]);
	out[3] = see_through ? t[ITEM_BASE][3] : 1.0f;
}

// One triangle into the canvas, in the out layer's coordinates.
static void draw_triangle(Canvas *canvas, const Layer *out_layer, const AvatarPart *part, const Layer *layers,
	uint32_t count, const XsdkAvatarMesh *mesh, const uint32_t *v, bool see_through)
{
	float x[3], y[3];
	for (uint32_t i = 0; i < 3; i++) {
		const float *uv = uv_at(mesh, v[i], out_layer->uv_set);
		x[i] = uv[0] * (float)canvas->width;
		y[i] = uv[1] * (float)canvas->height;
	}
	const float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
	if (fabsf(area) < 1e-6f)
		return;
	float lo_x = fminf(x[0], fminf(x[1], x[2])), hi_x = fmaxf(x[0], fmaxf(x[1], x[2]));
	float lo_y = fminf(y[0], fminf(y[1], y[2])), hi_y = fmaxf(y[0], fmaxf(y[1], y[2]));
	// A triangle over many repeats of a wrapped texture would cover the picture many times over: once is enough.
	const float w = (float)canvas->width, h = (float)canvas->height;
	if (hi_x - lo_x > 2 * w || hi_y - lo_y > 2 * h)
		return;
	for (int ty = (int)floorf(lo_y); ty <= (int)ceilf(hi_y); ty++)
		for (int tx = (int)floorf(lo_x); tx <= (int)ceilf(hi_x); tx++) {
			const float px = (float)tx + 0.5f, py = (float)ty + 0.5f;
			float b[3];
			b[0] = ((x[1] - px) * (y[2] - py) - (x[2] - px) * (y[1] - py)) / area;
			b[1] = ((x[2] - px) * (y[0] - py) - (x[0] - px) * (y[2] - py)) / area;
			b[2] = 1.0f - b[0] - b[1];
			if (b[0] < -1e-4f || b[1] < -1e-4f || b[2] < -1e-4f)
				continue;
			if (out_layer->clamp && (tx < 0 || ty < 0 || tx >= (int)canvas->width || ty >= (int)canvas->height))
				continue;
			const uint32_t cx = (uint32_t)wrap_or_clamp(tx, (int)canvas->width, false);
			const uint32_t cy = (uint32_t)wrap_or_clamp(ty, (int)canvas->height, false);
			const size_t at = (size_t)cy * canvas->width + cx;
			shade(part, layers, count, mesh, v, b, see_through, canvas->color + 4 * at);
			canvas->covered[at] = 1;
		}
}

// Texels no triangle covered take their covered neighbours' average, a ring at a time, so that filtering across a
// seam doesn't pull in black; what is still empty after that takes the average of everything covered.
static void dilate(Canvas *c)
{
	const uint32_t w = c->width, h = c->height;
	uint8_t *next = malloc((size_t)w * h);
	if (!next)
		return;
	for (uint32_t pass = 0; pass < DILATE_PASSES; pass++) {
		memcpy(next, c->covered, (size_t)w * h);
		for (uint32_t y = 0; y < h; y++)
			for (uint32_t x = 0; x < w; x++) {
				const size_t at = (size_t)y * w + x;
				if (c->covered[at])
					continue;
				float sum[4] = { 0 };
				uint32_t n = 0;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = (int)x + dx, ny = (int)y + dy;
						if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h || !c->covered[(size_t)ny * w + nx])
							continue;
						for (uint32_t k = 0; k < 4; k++)
							sum[k] += c->color[4 * ((size_t)ny * w + nx) + k];
						n++;
					}
				if (n) {
					for (uint32_t k = 0; k < 4; k++)
						c->color[4 * at + k] = sum[k] / (float)n;
					next[at] = 1;
				}
			}
		memcpy(c->covered, next, (size_t)w * h);
	}
	free(next);
	float sum[4] = { 0 };
	uint32_t n = 0;
	for (size_t i = 0; i < (size_t)w * h; i++)
		if (c->covered[i]) {
			for (uint32_t k = 0; k < 4; k++)
				sum[k] += c->color[4 * i + k];
			n++;
		}
	for (size_t i = 0; i < (size_t)w * h; i++)
		if (!c->covered[i])
			for (uint32_t k = 0; k < 4; k++)
				c->color[4 * i + k] = n ? sum[k] / (float)n : 1.0f;
}

static const XsdkAvatarParameter *texture_parameter(const XsdkAvatarMaterial *material, uint32_t id)
{
	const XsdkAvatarParameter *p = xsdk_avatar_material_parameter(material, id);
	return p && p->is_texture ? p : NULL;
}

static void set_layer(Layer *l, const XsdkAvatarTexture *texture, const XsdkAvatarParameter *p)
{
	l->uv_set = p->uv_set != XSDK_AVATAR_NO_UV_SET ? p->uv_set : 0;
	l->clamp = p->address == XSDK_AVATAR_ADDRESS_CLAMP;
	if (texture && !decode(texture, 0, l))
		l->rgba = NULL;
}

static uint32_t side_size(uint32_t n)
{
	return n < BAKE_MIN ? BAKE_MIN : n > BAKE_MAX ? BAKE_MAX : n;
}

int avatar_bake_material(const AvatarPart *part, uint32_t material, const uint8_t *hidden, AvatarBake *out)
{
	memset(out, 0, sizeof *out);
	const XsdkAvatarModel *model = &part->model;
	if (material >= model->material_count)
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	const XsdkAvatarMaterial *m = &model->materials[material];
	Layer layers[MAX_LAYERS];
	memset(layers, 0, sizeof layers);
	uint32_t count = 0, width = 0, height = 0;
	const Layer *out_layer = NULL;
	bool any = false;
	if (part->is_head) {
		// Slot k: the face layer k (frame 0, the neutral face), sampled with the set its binding names.
		count = AVATAR_HEAD_LAYERS;
		for (uint32_t k = 0; k < AVATAR_HEAD_LAYERS; k++) {
			for (uint32_t i = 0; i < m->parameter_count; i++)
				if (m->parameters[i].is_texture && m->parameters[i].texture == k)
					set_layer(&layers[k], part->layers[k].data ? &part->layers[k] : NULL, &m->parameters[i]);
			any |= layers[k].rgba != NULL;
		}
		out_layer = &layers[0];
		width = height = HEAD_BAKE;
	} else {
		const XsdkAvatarParameter *ids[3] = { texture_parameter(m, XSDK_AVATAR_PARAMETER_BASE),
			part->tinted ? texture_parameter(m, XSDK_AVATAR_PARAMETER_MASK) : NULL,
			texture_parameter(m, XSDK_AVATAR_PARAMETER_DECAL) };
		if (!ids[0])
			return XSDK_AVATAR_ERROR_NOT_FOUND;
		count = 3;
		for (uint32_t k = 0; k < 3; k++) {
			if (!ids[k])
				continue;
			const XsdkAvatarTexture *t = ids[k]->texture < model->texture_count ? &model->textures[ids[k]->texture]
																				 : NULL;
			set_layer(&layers[k], t, ids[k]);
			if (layers[k].rgba) {
				width = layers[k].width > width ? layers[k].width : width;
				height = layers[k].height > height ? layers[k].height : height;
			}
		}
		any = layers[ITEM_BASE].rgba != NULL;
		out_layer = &layers[ITEM_BASE];
		width = side_size(width);
		height = side_size(height);
	}
	if (!any) {
		for (uint32_t k = 0; k < count; k++)
			free(layers[k].rgba);
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	}
	Canvas canvas = { width, height, calloc((size_t)width * height * 4, sizeof(float)),
		calloc((size_t)width * height, 1) };
	const bool see_through = m->technique == XSDK_AVATAR_TECHNIQUE_SEE_THROUGH;
	int result = XSDK_AVATAR_ERROR_MEMORY;
	if (canvas.color && canvas.covered) {
		for (uint32_t mi = 0, triangle = 0; mi < model->mesh_count; mi++) {
			const XsdkAvatarMesh *mesh = &model->meshes[mi];
			const uint32_t triangles = mesh->index_count / 3;
			if (mesh->material == material && mesh->uv_sets)
				for (uint32_t t = 0; t < triangles; t++)
					if (!hidden[triangle + t]) {
						const uint32_t v[3] = { mesh->indices[3 * t], mesh->indices[3 * t + 1],
							mesh->indices[3 * t + 2] };
						draw_triangle(&canvas, out_layer, part, layers, count, mesh, v, see_through);
					}
			triangle += triangles;
		}
		dilate(&canvas);
		out->rgba = malloc((size_t)width * height * 4);
		if (out->rgba) {
			for (size_t i = 0; i < (size_t)width * height * 4; i++) {
				const float c = canvas.color[i];
				out->rgba[i] = (uint8_t)(c <= 0.0f ? 0 : c >= 1.0f ? 255 : c * 255.0f + 0.5f);
			}
			out->width = width;
			out->height = height;
			out->uv_set = out_layer->uv_set;
			out->clamp = out_layer->clamp;
			out->see_through = see_through;
			result = XSDK_AVATAR_OK;
		}
	}
	free(canvas.color);
	free(canvas.covered);
	for (uint32_t k = 0; k < count; k++)
		free(layers[k].rgba);
	return result;
}
