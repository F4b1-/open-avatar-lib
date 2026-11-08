#ifndef XSDK_AVATAR_RUNTIME_H
#define XSDK_AVATAR_RUNTIME_H

// The avatar layer's internal interfaces (console only): reading the console's files (avatar_content.c), putting an
// avatar's parts together (avatar_outfit.c), and drawing them (avatar_render.c). avatar.c ties them together.

#include <stdbool.h>
#include <stdint.h>

#include "xsdk/avatar.h"
#include "xsdk/texture.h"

// ---- avatar_content.c: the stock pack, item packages and profiles ----

// The stock pack, opened on first use and kept until avatar_content_close.
int avatar_content_pack(const XsdkAvatarPack **pack);
// A stock record's STRB file, malloc'd; NULL (logged) if it can't be read.
void *avatar_content_read_stock(uint32_t record, uint32_t *size_bytes);
// An item package's asset_v2.bin, found by the item's id in the shared content folders, malloc'd; NULL if the
// console doesn't have it.
void *avatar_content_read_item(const uint8_t *id, uint32_t *size_bytes);
// The description of the profile signed in at user_index.
int avatar_content_read_profile(uint32_t user_index, uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES]);
// Frees the stock animations and closes the pack.
void avatar_content_close(void);

// ---- avatar_outfit.c: an avatar's parts, decoded, with their colours; no GPU ----

enum {
	AVATAR_MAX_PARTS = 24,
	AVATAR_HEAD_LAYERS = 6, // the Head's texture slots (SPECS.md, section 3.5)
};

typedef struct {
	char label[48];
	XsdkAvatarModel model;
	// The stock record it came from (MaleBody 0, FemaleBody 1, Head 2, ...), or UINT32_MAX for an item package.
	uint32_t record;
	// What it changes on another part: triangles to hide and vertices to move (SPECS.md, section 3.7).
	XsdkAvatarShape shape;
	// False when it has neither custom colours nor a colour table: its mask is left out (SPECS.md, section 3.6).
	bool tinted;
	float colors[3][3]; // custom colours 1-3, 0-1 RGB
	// The Head: its face layers (data NULL where none), per layer the colours of its red, green and blue, the skin.
	bool is_head;
	XsdkAvatarTexture layers[AVATAR_HEAD_LAYERS];
	float layer_colors[AVATAR_HEAD_LAYERS][3][3];
	float skin[3];
} AvatarPart;

typedef struct {
	uint32_t part_count;
	AvatarPart parts[AVATAR_MAX_PARTS];
	// Built from the body as decoded, before items' shapes move its vertices; bone_count 0 without a body.
	XsdkAvatarSkeleton skeleton;
	uint32_t gender;
	uint32_t replaced_count;
	uint32_t left_out_count;
	float top;
} AvatarOutfit;

// The parts a description names: stock ones from the pack, items from their packages, stand-ins for missing ones.
int avatar_outfit_load(AvatarOutfit *outfit, const XsdkAvatarDescription *description);
// A description of stock parts, with a plain face.
int avatar_outfit_describe_stock(
	XsdkAvatarDescription *description, const XsdkAvatarStockPart *parts, uint32_t count);
// Marks the triangles of part that the outfit's other parts hide (SPECS.md, section 3.7), numbered across its meshes:
// a byte per triangle, 1 if hidden. The renderer and the export leave them out.
void avatar_outfit_hidden(const AvatarOutfit *outfit, const AvatarPart *part, uint8_t *hidden, uint32_t triangles);
void avatar_outfit_free(AvatarOutfit *outfit);

// ---- avatar_export.c: an outfit as a rig to animate (SPECS.md, section 4.9); no GPU ----

// The outfit's skeleton and parts as a glTF binary, malloc'd into *glb.
int avatar_export_outfit(const AvatarOutfit *outfit, void **glb, uint32_t *size_bytes);

// avatar_bake.c: a part's material as one picture, its textures mixed as the renderer mixes them, in the coordinates
// of uv_set (the base texture's; the Head's first layer's), addressed as clamp says; RGBA rows from the top.
typedef struct {
	uint32_t width, height;
	uint8_t *rgba; // malloc'd
	uint32_t uv_set;
	bool clamp;
	bool see_through; // blended by its alpha (sunglasses lenses); else alpha is 1
} AvatarBake;

// hidden: the part's triangles other parts hide (avatar_outfit_hidden). XSDK_AVATAR_ERROR_NOT_FOUND if the material
// has no texture to bake.
int avatar_bake_material(const AvatarPart *part, uint32_t material, const uint8_t *hidden, AvatarBake *out);

// avatar_png.c: RGBA rows as a PNG file, malloc'd into *png.
int avatar_png_encode(const uint8_t *rgba, uint32_t width, uint32_t height, uint8_t **png, uint32_t *size_bytes);

// ---- avatar_render.c: an outfit in GPU memory, and its draws ----

enum {
	AVATAR_MAX_DRAWS = 64,
};

enum {
	// The Head's layers that animate (SPECS.md, section 3.5), and its halves.
	AVATAR_LAYER_EYEBROWS = 2,
	AVATAR_LAYER_EYES = 3,
	AVATAR_LAYER_MOUTH = 4,
	AVATAR_SIDES = 2, // 0 the avatar's left half (+x), 1 its right
};

// Per half of the face, the frame each Head layer shows.
typedef uint8_t AvatarFaceFrames[AVATAR_SIDES][AVATAR_HEAD_LAYERS];

typedef struct {
	bool is_head;
	uint32_t side;         // the Head's: which half (AVATAR_SIDES)
	bool see_through;      // blended by its alpha, after the rest (sunglasses lenses)
	uint32_t vertex_start; // in vertices, from the drawable's first
	uint32_t vertex_count;
	uint32_t index_start; // in indices, even
	uint32_t index_count;
	uint32_t texture_count;                // 3 for an item, 6 for the Head
	const XsdkTexture *textures[6];        // by sampler binding
	const XsdkSamplerState *samplers[6];   // wrap or clamp, as each binding's address mode says
	float constants[19][4];                // the pixel shader's push constants
	uint32_t constant_count;
} AvatarDraw;

typedef struct {
	volatile uint32_t *vertices;
	volatile uint32_t *indices;
	uint32_t texture_count;
	XsdkTexture *textures;
	uint32_t draw_count;
	AvatarDraw draws[AVATAR_MAX_DRAWS];
	// The Head's layers with every frame, each layer's frames in one allocation; frame_count 0 where none.
	uint32_t face_frame_count[AVATAR_HEAD_LAYERS];
	XsdkTexture *face_frames[AVATAR_HEAD_LAYERS];
	volatile uint32_t *face_memory[AVATAR_HEAD_LAYERS];
	uint32_t gpu_bytes; // as allocated, in whole pages
	uint32_t cpu_bytes; // its heap memory beside the struct
} AvatarDrawable;

// The shaders, samplers and blank texture, made on first use.
int avatar_render_open(void);
void avatar_render_close(void);
// Puts the outfit's vertices, indices and textures in GPU memory; the outfit may be freed after.
int avatar_render_build(AvatarDrawable *drawable, const AvatarOutfit *outfit);
// Draws it: the camera's view-projection (for its axes, which the lighting takes), mvp and world, all column-major
// (world turns normals into world space), the XSDK_AVATAR_BONES skin matrices, and the face's frames (clamped to
// the frames each layer has).
void avatar_render_draw(const AvatarDrawable *drawable, const float view_projection[16], const float mvp[16],
	const float world[16], const float (*skin)[12], const AvatarFaceFrames *face);
void avatar_render_free(AvatarDrawable *drawable);

#endif
