#ifndef STORE_FONT_H
#define STORE_FONT_H

// A small stroke font of our own: capital letters, 0 and space, each a few straight strokes on a grid 4 wide and 6
// high, drawn as thin quads into the mesh (mesh.h). Lower-case letters are drawn as capitals.

#include <stdint.h>

enum {
	FONT_ADVANCE = 6, // grid units from one letter to the next
	FONT_HEIGHT = 6,
};

// The text's width in grid units.
float font_width(const char *text);
// The text from origin (its bottom left), right and up being one grid unit across and up the letters, the strokes
// thickness units of the plane wide (the same units as right and up's lengths).
void font_text(const char *text, const float origin[3], const float right[3], const float up[3], float thickness,
	const float color[4], uint32_t material);

#endif
