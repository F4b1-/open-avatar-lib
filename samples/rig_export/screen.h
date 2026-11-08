#ifndef RIG_EXPORT_SCREEN_H
#define RIG_EXPORT_SCREEN_H

// A text screen drawn by the CPU straight into the front buffer: no shaders, for a title that only reports what it is
// doing. Letters are a 5x7 pixel font of our own (screen.c): capitals (small letters show as capitals), digits and
// . , : ; - _ / \ ( ) % ' ! ? > + = and space.

#include <stdbool.h>
#include <stdint.h>

enum {
	SCREEN_MAX_LINES = 14,
	SCREEN_LINE_CHARS = 52, // at the text scale, across 1280 pixels with margins
};

typedef struct {
	char text[SCREEN_MAX_LINES][SCREEN_LINE_CHARS + 1];
	uint32_t argb[SCREEN_MAX_LINES];
	uint32_t count;
} ScreenLines;

// Opens the GPU and a 720p display; false (logged) if either fails, and the title goes on without a screen.
bool screen_open(void);
// Shows a title and the lines; nothing without a screen.
void screen_show(const char *title, const ScreenLines *lines);
// Waits for a button pressed on any controller.
void screen_wait_button(void);
void screen_close(void);

#endif
