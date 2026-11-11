#ifndef STORE_SCREENSHOT_H
#define STORE_SCREENSHOT_H

// Screenshots as Avatar Thrift Store takes them: both triggers save the screen as a PNG in game:\Screenshots, next to
// the title (Store_001.png, _002, ...).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xsdk/texture.h"

// The texture main.c resolves a screenshot frame into, band by band as the screen: tiled XSDK_FORMAT_B8G8R8A8 at the
// screen's size, so that xsdk_gpu_resolve writes it as it writes the front buffer.
bool screenshot_init(XsdkTexture *texture, uint32_t width, uint32_t height);
// Waits for the GPU to finish the resolve into texture, reads it back and saves it under the next free name; path
// gets the file written.
bool screenshot_save(const XsdkTexture *texture, char *path, size_t path_size);

#endif
