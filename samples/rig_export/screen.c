// A text screen drawn by the CPU into the front buffer (screen.h).
//
// tiled_index comes from libxenon:drivers/console/console.c:60-61, as xsdk's tests/tiled_index.h carries it, under
// this notice:
//
// Copyright 2009  Georg Lukas <georg@op-co.de>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.

#include "screen.h"

#include <string.h>

#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/thread.h"

enum {
	BUFFERS = 2,
	SWAP_INTERVAL = 1,
	BEGIN_TIMEOUT_MS = 1000,
	FENCE_TIMEOUT_MS = 2000,
	USERS = 4,
	GLYPH_W = 5,
	GLYPH_H = 7,
	TITLE_SCALE = 6,
	TEXT_SCALE = 4,
	MARGIN = 96,
	TITLE_Y = 72,
	LINES_Y = 176,
	LINE_STEP = 10 * TEXT_SCALE, // rows a line
	BACKGROUND = 0xFF1C2230,
	TITLE_ARGB = 0xFFFFFFFF,
};

static bool open;
static uint32_t width, height;

// A pixel's index in 32x32 tiles (libxenon, above).
static uint32_t tiled_index(uint32_t x, uint32_t y, uint32_t w)
{
	return ((y >> 5) * 32 * w + ((x >> 5) << 10) + (x & 3) + ((y & 1) << 2) + (((x & 31) >> 2) << 3) +
			   (((y & 31) >> 1) << 6)) ^
		((y & 8) << 2);
}

// The font: per character 7 rows of 5 bits, the leftmost pixel the highest bit. Our own, drawn for this screen.
typedef struct {
	char c;
	uint8_t rows[GLYPH_H];
} Glyph;

static const Glyph glyphs[] = {
	{ 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	{ 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
	{ 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
	{ 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
	{ 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
	{ 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
	{ 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
	{ 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	{ 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
	{ 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
	{ 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
	{ 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
	{ 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
	{ 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
	{ 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
	{ 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
	{ 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
	{ 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
	{ 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
	{ 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
	{ 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
	{ 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
	{ 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
	{ '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
	{ '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
	{ '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
	{ '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
	{ '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
	{ '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
	{ '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
	{ '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
	{ '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
	{ '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
	{ '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
	{ ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
	{ ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
	{ ';', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08 } },
	{ '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
	{ '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
	{ '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
	{ '\\', { 0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01 } },
	{ '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
	{ ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
	{ '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
	{ '\'', { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
	{ '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
	{ '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
	{ '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
	{ '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
	{ '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
};

static const uint8_t *glyph_of(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	for (size_t i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
		if (glyphs[i].c == c)
			return glyphs[i].rows;
	return NULL; // a space, or a character the font hasn't
}

// The front buffer holds the bytes B, G, R, A as the display reads them (xsdk's tests/bar_scene.c).
static uint32_t as_stored(uint32_t argb)
{
	return __builtin_bswap32(argb);
}

static void text(volatile uint32_t *pixels, const char *s, uint32_t x, uint32_t y, uint32_t scale, uint32_t argb)
{
	const uint32_t stored = as_stored(argb);
	for (; *s && x + GLYPH_W * scale <= width; s++, x += (GLYPH_W + 1) * scale) {
		const uint8_t *rows = glyph_of(*s);
		if (!rows)
			continue;
		for (uint32_t r = 0; r < GLYPH_H; r++)
			for (uint32_t c = 0; c < GLYPH_W; c++)
				if (rows[r] >> (GLYPH_W - 1 - c) & 1)
					for (uint32_t dy = 0; dy < scale; dy++)
						for (uint32_t dx = 0; dx < scale; dx++)
							if (y + r * scale + dy < height)
								pixels[tiled_index(x + c * scale + dx, y + r * scale + dy, width)] = stored;
	}
}

bool screen_open(void)
{
	NTSTATUS status = xsdk_gpu_open();
	if (NT_SUCCESS(status))
		status = xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, SWAP_INTERVAL, &width, &height);
	if (!NT_SUCCESS(status)) {
		xsdk_log_write("rig_export: no screen (0x%08X): the log alone says what happens", (unsigned)status);
		return false;
	}
	open = true;
	return true;
}

void screen_show(const char *title, const ScreenLines *lines)
{
	uint32_t address;
	if (!open || !NT_SUCCESS(xsdk_present_begin(&address, BEGIN_TIMEOUT_MS)))
		return;
	volatile uint32_t *pixels = xsdk_present_pixels(address);
	// The whole buffer, its rows rounded up to whole tiles as resolves fill it.
	const uint32_t count = xsdk_gpu_resolve_bytes(width, height) / 4, background = as_stored(BACKGROUND);
	for (uint32_t i = 0; i < count; i++)
		pixels[i] = background;
	text(pixels, title, MARGIN, TITLE_Y, TITLE_SCALE, TITLE_ARGB);
	for (uint32_t i = 0; i < lines->count && i < SCREEN_MAX_LINES; i++)
		text(pixels, lines->text[i], MARGIN, LINES_Y + i * LINE_STEP, TEXT_SCALE, lines->argb[i]);
	// The CPU's stores reach memory before the display reads it.
	__asm__ volatile("sync" ::: "memory");
	xsdk_present_end();
}

void screen_wait_button(void)
{
	if (!open)
		return;
	// A button held from before (the one that started the title) counts only once let go.
	uint16_t held = 0xFFFF;
	for (;;) {
		uint16_t buttons = 0;
		for (uint32_t user = 0; user < USERS; user++) {
			INPUT_STATE input;
			if (XamInputGetState(user, 0, &input) == ERROR_SUCCESS)
				buttons |= input.gamepad.buttons;
		}
		if (buttons & (uint16_t)~held & (uint16_t)~INPUT_GAMEPAD_GUIDE)
			return;
		held &= buttons;
		(void)xsdk_thread_sleep(16);
	}
}

void screen_close(void)
{
	if (!open)
		return;
	xsdk_present_close();
	(void)xsdk_gpu_wait(xsdk_gpu_fence(), FENCE_TIMEOUT_MS);
	(void)xsdk_gpu_close();
	open = false;
}
