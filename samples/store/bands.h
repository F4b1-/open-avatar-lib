#ifndef STORE_BANDS_H
#define STORE_BANDS_H

// A frame drawn in horizontal bands, each small enough for its colour and depth to fit in eDRAM at the samples a
// pixel asked for, each resolved into the frame (and, for a screenshot, into a texture too) before the next.

#include <stdint.h>

#include "xsdk/texture.h"

// Draws one band: the viewport is set so that the title draws the whole screen as usual.
typedef void (*BandDraw)(void *context);

// Draws the frame at address (width x height, from xsdk_present_begin) in bands of samples (1, 2 or 4) a pixel,
// calling draw for each, cleared to clear_argb; shot, if not NULL, gets the frame too.
void bands_draw(uint32_t width, uint32_t height, uint32_t samples, uint32_t address, const XsdkTexture *shot,
	uint32_t clear_argb, BandDraw draw, void *context);

#endif
