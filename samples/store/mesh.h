#ifndef STORE_MESH_H
#define STORE_MESH_H

// The store's own drawing (the library draws only avatars): triangles built once on the CPU, put in GPU memory, and
// drawn in ranges, lit as the avatars are (the library's lighting) or glowing, opaque, blended, or over the screen.

#include <stdbool.h>
#include <stdint.h>

// How a surface's colour is patterned (store_shaders.py).
enum {
	MATERIAL_PLAIN,
	MATERIAL_FLOOR,  // planks
	MATERIAL_BRICK,
	MATERIAL_BOARDS, // wall boards
	MATERIAL_GLOW,   // not lit: neon, screens, lamps, the overlay
};

// How a range is drawn.
enum {
	MESH_OPAQUE,  // depth tested and written
	MESH_BLENDED, // by its alpha, depth tested, not written
	MESH_OVERLAY, // by its alpha, over everything: the screen's own, in pixels
};

enum {
	MESH_LIGHT_VECTORS = 7, // the light as the library's pixel shaders take it, then the camera's axes
};

typedef struct {
	uint32_t first;
	uint32_t count;
} MeshRange;

// Shaders, states and room to build in; after xsdk_gpu_open.
bool mesh_open(void);
// After a fence past the last frame that drew any of it.
void mesh_close(void);

// Building, before mesh_upload: ranges are what was added between a mark and mesh_since.
uint32_t mesh_mark(void);
MeshRange mesh_since(uint32_t mark);
// A quad by its corners, in order round it, facing normal.
void mesh_quad4(const float *p0, const float *p1, const float *p2, const float *p3, const float *normal,
	const float color[4], uint32_t material);
// A quad from corner a along edges u and v, facing u x v.
void mesh_quad(const float a[3], const float u[3], const float v[3], const float color[4], uint32_t material);
// A box between two corners, all six faces outward.
void mesh_box(const float min[3], const float max[3], const float color[4], uint32_t material);
// Into GPU memory; nothing more can be added.
bool mesh_upload(void);

// Drawing: vp column-major. Leaves the opaque state set (mesh_use_opaque).
void mesh_draw(MeshRange range, const float vp[16], const float light[MESH_LIGHT_VECTORS][4], uint32_t mode);
// The same, every vertex moved by place's x, y and z after scaling by its w: for what moves as one (falling snow).
void mesh_draw_placed(MeshRange range, const float vp[16], const float light[MESH_LIGHT_VECTORS][4], uint32_t mode,
	const float place[4]);
// A soft shadow of radius metres round (x, z) on the floor.
void mesh_draw_shadow(const float vp[16], float x, float z, float radius);
// Depth tested and written, no blending: for the store's opaque parts and the avatars.
void mesh_use_opaque(void);

#endif
