#ifndef STORE_SCENE_H
#define STORE_SCENE_H

// The Avatar Thrift Store: floor, walls, furniture and goods, the neon sign and the lights, built into the mesh
// (mesh.h); and what the avatars bump into. Plain shapes and colours of our own; no art from anywhere else.

#include <stdbool.h>

#include "mesh.h"

// The floor runs from -STORE_HALF_X to STORE_HALF_X and -STORE_HALF_Z to STORE_HALF_Z, in metres.
#define STORE_HALF_X 5.0f
#define STORE_HALF_Z 4.0f
#define STORE_HEIGHT 3.0f

// Into the mesh, before mesh_upload.
void scene_build(void);
// The store's solid parts, and its glowing ones.
void scene_draw(const float vp[16], const float light[MESH_LIGHT_VECTORS][4]);
// What is drawn blended, after the avatars: the snow falling outside the window (seconds since starting move it),
// the window's glass and the neon's glow.
void scene_draw_glow(const float vp[16], const float light[MESH_LIGHT_VECTORS][4], float seconds);
// Moves a circle of radius at (x, z) out of the walls and furniture.
void scene_collide(float *x, float *z, float radius);

#endif
