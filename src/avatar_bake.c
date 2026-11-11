// Baking a part's material into one picture for the rig export (avatar_export.c, FORMATS.md section 4.9): the
// textures the renderer mixes per pixel (avatar_render.c, tools/gen_shaders.py) mixed once on the CPU, in the base
// texture's coordinates. Every material samples its textures with texture coordinate sets of their own (FORMATS.md,
// section 3.6), so the bake draws the material's triangles into the picture and, inside each, finds where the other
// textures are sampled by interpolating their sets. No GPU: runs on the PC too.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
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
	ITEM_LAYERS = 3,

	// DXT (FORMATS.md, section 3.4): 4x4 texel blocks, DXT1 8 bytes (two RGB565 colours, then 16 2-bit indices), DXT5
	// 16 (two alphas and 16 3-bit indices first). The textures store each 16-bit word byte-swapped (8-in-16).
	BLOCK_SIDE = 4,
	BLOCK_TEXELS = BLOCK_SIDE * BLOCK_SIDE,
	DXT1_BLOCK_BYTES = 8,
	DXT5_BLOCK_BYTES = 16,
	DXT5_ALPHA_BYTES = 8,
	COLOR_INDEX_BITS = 2,
	ALPHA_INDEX_BITS = 3,
	RGBA = 4, // bytes or floats a texel
};

// A texture decoded, the coordinate set it is sampled with, and how it addresses outside 0-1.
typedef struct {
	uint32_t width, height;
	uint8_t *rgba;
	uint32_t uv_set;
	bool clamp;
} Layer;

// The layers a material mixes, and the one whose coordinates the bake is in.
typedef struct {
	Layer layers[MAX_LAYERS];
	uint32_t count;
	const Layer *out; // the base texture (the Head: its first layer)
	uint32_t width, height; // the bake's size
} Layers;

// The bake's picture as floats while it is made, and which texels a triangle covered.
typedef struct {
	uint32_t width, height;
	float *color; // RGBA per texel
	uint8_t *covered;
} Canvas;

// A 5:6:5 colour (red in the top 5 bits) as 8-bit channels, each widened by repeating its top bits.
static void rgb565(uint16_t c, uint8_t out[3])
{
	const uint32_t r = avatar_bits(c, 11, 5), g = avatar_bits(c, 5, 6), b = avatar_bits(c, 0, 5);
	out[0] = (uint8_t)(r << 3 | r >> 2);
	out[1] = (uint8_t)(g << 2 | g >> 4);
	out[2] = (uint8_t)(b << 3 | b >> 2);
}

// A 16-bit little-endian word of a block whose 16-bit words were stored byte-swapped (the 8-in-16 endian mode).
static uint32_t word16(const uint8_t *block, uint32_t at)
{
	return avatar_be16(block + at);
}

// A DXT5 block's alpha half: its 8 alphas (two stored, the rest between them) and its 16 3-bit indices.
static uint64_t decode_alphas(const uint8_t *block, uint8_t alpha[8])
{
	uint8_t a[DXT5_ALPHA_BYTES];
	for (uint32_t k = 0; k < DXT5_ALPHA_BYTES; k++)
		a[k] = block[k ^ 1]; // its 16-bit words swapped like the rest
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
	uint64_t indices = 0;
	for (uint32_t k = 0; k < 6; k++)
		indices |= (uint64_t)a[2 + k] << (8 * k);
	return indices;
}

// A block's colour half: its 4 colours (two stored, the rest between them; in DXT1 with c0 <= c1 the fourth is
// transparent black) and returns its 16 2-bit indices.
static uint32_t decode_colors(const uint8_t *colors, bool dxt5, uint8_t p[4][4])
{
	const uint16_t c0 = (uint16_t)word16(colors, 0), c1 = (uint16_t)word16(colors, 2);
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
	return word16(colors, 4) | word16(colors, 6) << 16;
}

// One 4x4 block (research/texture.py's dxt1_block and dxt5_block) into rows of width texels.
static void decode_block(const uint8_t *block, bool dxt5, uint8_t *out, uint32_t width)
{
	uint8_t alpha[8];
	uint64_t alpha_indices = 0;
	if (dxt5)
		alpha_indices = decode_alphas(block, alpha);
	uint8_t p[4][4];
	const uint32_t indices = decode_colors(dxt5 ? block + DXT5_ALPHA_BYTES : block, dxt5, p);
	for (uint32_t i = 0; i < BLOCK_TEXELS; i++) {
		uint8_t *texel = out + ((i / BLOCK_SIDE) * width + i % BLOCK_SIDE) * RGBA;
		memcpy(texel, p[avatar_bits(indices, COLOR_INDEX_BITS * i, COLOR_INDEX_BITS)], RGBA);
		if (dxt5)
			texel[3] = alpha[alpha_indices >> (ALPHA_INDEX_BITS * i) & 7];
	}
}

// A texture's frame as RGBA bytes, rows from the top. XSDK_AVATAR_ERROR_NOT_FOUND if it has no pixels or a format
// we don't know (the layer is then left out), _MEMORY.
static int decode(const XsdkAvatarTexture *t, uint32_t frame, Layer *out)
{
	const bool dxt5 = t->format == XSDK_AVATAR_TEXTURE_DXT5;
	if (!t->data || (!dxt5 && t->format != XSDK_AVATAR_TEXTURE_DXT1) || t->width < BLOCK_SIDE ||
		t->height < BLOCK_SIDE || frame >= t->frame_count)
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	const uint32_t block_bytes = dxt5 ? DXT5_BLOCK_BYTES : DXT1_BLOCK_BYTES;
	const uint32_t across = t->width / BLOCK_SIDE, down = t->height / BLOCK_SIDE;
	if ((size_t)across * down * block_bytes > t->frame_bytes)
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	out->rgba = avatar_malloc((size_t)t->width * t->height * RGBA, "a baked texture");
	if (!out->rgba)
		return XSDK_AVATAR_ERROR_MEMORY;
	out->width = t->width;
	out->height = t->height;
	const uint8_t *blocks = t->data + (size_t)frame * t->frame_bytes;
	for (uint32_t by = 0; by < down; by++)
		for (uint32_t bx = 0; bx < across; bx++)
			decode_block(blocks + ((size_t)by * across + bx) * block_bytes, dxt5,
				out->rgba + ((size_t)by * BLOCK_SIDE * t->width + bx * BLOCK_SIDE) * RGBA, t->width);
	return XSDK_AVATAR_OK;
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
	for (uint32_t c = 0; c < RGBA; c++) {
		float s = 0.0f;
		for (uint32_t k = 0; k < 4; k++)
			s += w[k] * (float)l->rgba[((size_t)ys[k / 2] * l->width + (size_t)xs[k % 2]) * RGBA + c];
		out[c] = s / 255.0f;
	}
}

// A vertex's texture coordinates in a set (the first if the mesh has fewer; 0, 0 if it has none).
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

// The colour at a point of a triangle, its three vertices weighted by b: the renderer's mixing (FORMATS.md, 3.6).
static void shade(const AvatarPart *part, const Layers *ls, const XsdkAvatarMesh *mesh, const uint32_t *v,
	const float *b, bool see_through, float out[4])
{
	const Layer *layers = ls->layers;
	float t[MAX_LAYERS][4];
	for (uint32_t k = 0; k < ls->count; k++) {
		const float *a = uv_at(mesh, v[0], layers[k].uv_set), *p = uv_at(mesh, v[1], layers[k].uv_set),
					*q = uv_at(mesh, v[2], layers[k].uv_set);
		sample(&layers[k], b[0] * a[0] + b[1] * p[0] + b[2] * q[0], b[0] * a[1] + b[1] * p[1] + b[2] * q[1], t[k]);
	}
	if (part->is_head) {
		// The skin, then each layer by its alpha, its red, green and blue standing for the layer's colours.
		memcpy(out, part->skin, 3 * sizeof(float));
		for (uint32_t k = 0; k < ls->count; k++) {
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
static void draw_triangle(Canvas *canvas, const AvatarPart *part, const Layers *ls, const XsdkAvatarMesh *mesh,
	const uint32_t *v, bool see_through)
{
	float x[3], y[3];
	for (uint32_t i = 0; i < 3; i++) {
		const float *uv = uv_at(mesh, v[i], ls->out->uv_set);
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
			if (ls->out->clamp && (tx < 0 || ty < 0 || tx >= (int)canvas->width || ty >= (int)canvas->height))
				continue;
			const uint32_t cx = (uint32_t)wrap_or_clamp(tx, (int)canvas->width, false);
			const uint32_t cy = (uint32_t)wrap_or_clamp(ty, (int)canvas->height, false);
			const size_t at = (size_t)cy * canvas->width + cx;
			shade(part, ls, mesh, v, b, see_through, canvas->color + RGBA * at);
			canvas->covered[at] = 1;
		}
}

// Each texel not covered yet that touches a covered one takes the average of those it touches; next marks them.
static void grow_ring(Canvas *c, uint8_t *next)
{
	const uint32_t w = c->width, h = c->height;
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
					for (uint32_t k = 0; k < RGBA; k++)
						sum[k] += c->color[RGBA * ((size_t)ny * w + nx) + k];
					n++;
				}
			if (n) {
				for (uint32_t k = 0; k < RGBA; k++)
					c->color[RGBA * at + k] = sum[k] / (float)n;
				next[at] = 1;
			}
		}
	memcpy(c->covered, next, (size_t)w * h);
}

// The texels still not covered take the average of every covered one (white if none is).
static void fill_rest(Canvas *c)
{
	const size_t texels = (size_t)c->width * c->height;
	float sum[4] = { 0 };
	uint32_t n = 0;
	for (size_t i = 0; i < texels; i++)
		if (c->covered[i]) {
			for (uint32_t k = 0; k < RGBA; k++)
				sum[k] += c->color[RGBA * i + k];
			n++;
		}
	for (size_t i = 0; i < texels; i++)
		if (!c->covered[i])
			for (uint32_t k = 0; k < RGBA; k++)
				c->color[RGBA * i + k] = n ? sum[k] / (float)n : 1.0f;
}

// Texels no triangle covered take their covered neighbours' average, a ring at a time, so that filtering across a
// seam doesn't pull in black; what is still empty after that takes the average of everything covered.
static int dilate(Canvas *c)
{
	uint8_t *next = avatar_malloc((size_t)c->width * c->height, "a baked texture");
	if (!next)
		return XSDK_AVATAR_ERROR_MEMORY;
	for (uint32_t pass = 0; pass < DILATE_PASSES; pass++)
		grow_ring(c, next);
	free(next);
	fill_rest(c);
	return XSDK_AVATAR_OK;
}

static const XsdkAvatarParameter *texture_parameter(const XsdkAvatarMaterial *material, uint32_t id)
{
	const XsdkAvatarParameter *p = xsdk_avatar_material_parameter(material, id);
	return p && p->is_texture ? p : NULL;
}

// A layer from a texture binding: its pixels decoded (none if the texture has none). XSDK_AVATAR_ERROR_MEMORY only.
static int set_layer(Layer *l, const XsdkAvatarTexture *texture, const XsdkAvatarParameter *p)
{
	l->uv_set = p->uv_set != XSDK_AVATAR_NO_UV_SET ? p->uv_set : 0;
	l->clamp = p->address == XSDK_AVATAR_ADDRESS_CLAMP;
	const int result = texture ? decode(texture, 0, l) : XSDK_AVATAR_ERROR_NOT_FOUND;
	if (result != XSDK_AVATAR_OK)
		l->rgba = NULL;
	return result == XSDK_AVATAR_ERROR_MEMORY ? result : XSDK_AVATAR_OK;
}

static void free_layers(Layers *ls)
{
	for (uint32_t k = 0; k < ls->count; k++)
		free(ls->layers[k].rgba);
}

static uint32_t side_size(uint32_t n)
{
	return n < BAKE_MIN ? BAKE_MIN : n > BAKE_MAX ? BAKE_MAX : n;
}

// The Head's layers: slot k the face layer k (frame 0, the neutral face), sampled with the set its binding names.
// XSDK_AVATAR_ERROR_NOT_FOUND if no layer has pixels, _MEMORY.
static int head_layers(const AvatarPart *part, const XsdkAvatarMaterial *m, Layers *ls)
{
	int result = XSDK_AVATAR_OK;
	bool any = false;
	ls->count = AVATAR_HEAD_LAYERS;
	for (uint32_t k = 0; k < AVATAR_HEAD_LAYERS; k++) {
		const XsdkAvatarTexture *texture = part->layers[k].data ? &part->layers[k] : NULL;
		for (uint32_t i = 0; i < m->parameter_count && result == XSDK_AVATAR_OK; i++)
			if (m->parameters[i].is_texture && m->parameters[i].texture == k)
				result = set_layer(&ls->layers[k], texture, &m->parameters[i]);
		any |= ls->layers[k].rgba != NULL;
	}
	ls->out = &ls->layers[0];
	ls->width = ls->height = HEAD_BAKE;
	return result != XSDK_AVATAR_OK ? result : any ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_NOT_FOUND;
}

// An item's layers: base, mask (if the part is tinted) and decal, the bake as large as the largest (within
// BAKE_MIN-BAKE_MAX). XSDK_AVATAR_ERROR_NOT_FOUND without a base texture with pixels, _MEMORY.
static int item_layers(const AvatarPart *part, const XsdkAvatarMaterial *m, Layers *ls)
{
	const XsdkAvatarModel *model = &part->model;
	const XsdkAvatarParameter *bindings[ITEM_LAYERS] = { texture_parameter(m, XSDK_AVATAR_PARAMETER_BASE),
		part->tinted ? texture_parameter(m, XSDK_AVATAR_PARAMETER_MASK) : NULL,
		texture_parameter(m, XSDK_AVATAR_PARAMETER_DECAL) };
	if (!bindings[ITEM_BASE])
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	int result = XSDK_AVATAR_OK;
	uint32_t width = 0, height = 0;
	ls->count = ITEM_LAYERS;
	for (uint32_t k = 0; k < ITEM_LAYERS && result == XSDK_AVATAR_OK; k++) {
		if (!bindings[k])
			continue;
		const uint32_t id = bindings[k]->texture;
		const XsdkAvatarTexture *texture = id < model->texture_count ? &model->textures[id] : NULL;
		result = set_layer(&ls->layers[k], texture, bindings[k]);
		if (ls->layers[k].rgba) {
			width = ls->layers[k].width > width ? ls->layers[k].width : width;
			height = ls->layers[k].height > height ? ls->layers[k].height : height;
		}
	}
	ls->out = &ls->layers[ITEM_BASE];
	ls->width = side_size(width);
	ls->height = side_size(height);
	if (result != XSDK_AVATAR_OK)
		return result;
	return ls->layers[ITEM_BASE].rgba ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_NOT_FOUND;
}

// Every triangle of the material that no other part hides, into the canvas. hidden is numbered across the meshes.
static void draw_material(Canvas *canvas, const AvatarPart *part, uint32_t material, const uint8_t *hidden,
	const Layers *ls, bool see_through)
{
	const XsdkAvatarModel *model = &part->model;
	for (uint32_t mi = 0, triangle = 0; mi < model->mesh_count; mi++) {
		const XsdkAvatarMesh *mesh = &model->meshes[mi];
		const uint32_t triangles = mesh->index_count / 3;
		if (mesh->material == material && mesh->uv_sets)
			for (uint32_t t = 0; t < triangles; t++)
				if (!hidden[triangle + t]) {
					const uint32_t v[3] = { mesh->indices[3 * t], mesh->indices[3 * t + 1], mesh->indices[3 * t + 2] };
					draw_triangle(canvas, part, ls, mesh, v, see_through);
				}
		triangle += triangles;
	}
}

// The canvas's colours, 0-1, as bytes; NULL if out of memory.
static uint8_t *canvas_bytes(const Canvas *canvas)
{
	const size_t n = (size_t)canvas->width * canvas->height * RGBA;
	uint8_t *rgba = avatar_malloc(n, "a baked texture");
	if (!rgba)
		return NULL;
	for (size_t i = 0; i < n; i++) {
		const float c = canvas->color[i];
		rgba[i] = (uint8_t)(c <= 0.0f ? 0 : c >= 1.0f ? 255 : c * 255.0f + 0.5f);
	}
	return rgba;
}

int avatar_bake_material(const AvatarPart *part, uint32_t material, const uint8_t *hidden, AvatarBake *out)
{
	memset(out, 0, sizeof *out);
	const XsdkAvatarModel *model = &part->model;
	if (material >= model->material_count)
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	const XsdkAvatarMaterial *m = &model->materials[material];
	Layers ls = { 0 };
	Canvas canvas = { 0 };
	int result = part->is_head ? head_layers(part, m, &ls) : item_layers(part, m, &ls);
	if (result != XSDK_AVATAR_OK)
		goto done;

	result = XSDK_AVATAR_ERROR_MEMORY;
	const size_t texels = (size_t)ls.width * ls.height;
	canvas = (Canvas){ ls.width, ls.height, avatar_calloc(texels * RGBA, sizeof(float), "a baked texture"),
		avatar_calloc(texels, 1, "a baked texture") };
	if (!canvas.color || !canvas.covered)
		goto done;
	const bool see_through = m->technique == XSDK_AVATAR_TECHNIQUE_SEE_THROUGH;
	draw_material(&canvas, part, material, hidden, &ls, see_through);
	if (dilate(&canvas) != XSDK_AVATAR_OK || !(out->rgba = canvas_bytes(&canvas)))
		goto done;
	out->width = ls.width;
	out->height = ls.height;
	out->uv_set = ls.out->uv_set;
	out->clamp = ls.out->clamp;
	out->see_through = see_through;
	result = XSDK_AVATAR_OK;
done:
	free(canvas.color);
	free(canvas.covered);
	free_layers(&ls);
	return result;
}
