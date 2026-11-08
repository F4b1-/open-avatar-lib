#ifndef STORE_WHEEL_H
#define STORE_WHEEL_H

// The emote wheel: a ring of named segments over the screen, the first at the top, going round clockwise; the one
// the stick points at lit up, its name in the middle. Built once into the mesh (mesh.h), drawn as an overlay.

#include <stdint.h>

enum {
	WHEEL_MAX = 14,
};

// Into the mesh, before mesh_upload: for a screen of width x height pixels, count segments named by names.
void wheel_build(uint32_t width, uint32_t height, const char *const *names, uint32_t count);
// The segment a stick points at (x right, y up, each -1 to 1), or -1 if it is barely pushed.
int wheel_pick(float x, float y);
// The wheel, segment selected lit (-1 for none).
void wheel_draw(int selected);
// The hint at the screen's corner, while the wheel is shut.
void wheel_draw_hint(void);

#endif
