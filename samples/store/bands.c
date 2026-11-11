// Drawing in bands (bands.h).

#include "bands.h"

#include <stdbool.h>

#include "xsdk/gpu.h"

enum {
	TILE_ROWS = 32, // a band's height is a multiple of the eDRAM tiles' height
};

// The tallest band (a multiple of TILE_ROWS) whose colour and depth both fit in eDRAM.
static uint32_t band_rows(uint32_t width, uint32_t height, uint32_t samples)
{
	uint32_t rows = (height + TILE_ROWS - 1) / TILE_ROWS * TILE_ROWS;
	while (rows > TILE_ROWS && 2 * xsdk_gpu_target_sample_tiles(width, rows, samples) > XSDK_EDRAM_TILES)
		rows -= TILE_ROWS;
	return rows;
}

void bands_draw(uint32_t width, uint32_t height, uint32_t samples, uint32_t address, const XsdkTexture *shot,
	uint32_t clear_argb, BandDraw draw, void *context)
{
	const uint32_t rows = band_rows(width, height, samples);
	// Every band's colour after a whole band's depth: xsdk's own layout (depth, then colour) follows each target's
	// size, so the last, shorter band's colour would lie where the others keep depth, and its clear (the clear colour,
	// read as depth about 0.996, some 10 m away) would hide what is farther in their rows (test run 6).
	const uint32_t color_tile = xsdk_gpu_target_sample_tiles(width, rows, samples);
	for (uint32_t top = 0; top < height; top += rows) {
		const uint32_t band = height - top < rows ? height - top : rows;
		xsdk_gpu_set_target_samples_at(width, band, samples, color_tile, 0);
		xsdk_gpu_set_viewport(0.0f, -(float)top, (float)width, (float)height, 0.0f, 1.0f);
		draw(context);
		if (shot) {
			xsdk_gpu_set_resolve_clears(false, false);
			xsdk_gpu_resolve(shot->address + top * shot->pitch * 4, width, band, clear_argb);
			xsdk_gpu_set_resolve_clears(true, true);
		}
		xsdk_gpu_resolve(address + top * width * 4, width, band, clear_argb);
	}
}
