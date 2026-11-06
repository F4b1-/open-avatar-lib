// Screenshots as PNG files (screenshot.h). The PNG is written with stored (uncompressed) deflate blocks: no
// compression library, at the cost of bigger files (about 2.8 MB at 720p).

#include "screenshot.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xsdk/gpu.h"
#include "xsdk/log.h"

enum {
	FENCE_TIMEOUT_MS = 2000,
	STORED_BLOCK_MAX = 65535,
};

static const char folder[] = "game:\\Screenshots";

typedef struct {
	uint8_t *data;
	size_t size;
} Bytes;

static uint32_t crc_table[256];

static void make_crc_table(void)
{
	for (uint32_t n = 0; n < 256; n++) {
		uint32_t c = n;
		for (uint32_t k = 0; k < 8; k++)
			c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

static void put32(Bytes *b, uint32_t v)
{
	b->data[b->size++] = (uint8_t)(v >> 24);
	b->data[b->size++] = (uint8_t)(v >> 16);
	b->data[b->size++] = (uint8_t)(v >> 8);
	b->data[b->size++] = (uint8_t)v;
}

// A chunk whose type and data the caller has written from start + 4 on; adds its length before and CRC after.
static void end_chunk(Bytes *b, size_t start)
{
	const uint32_t length = (uint32_t)(b->size - start - 8);
	b->data[start] = (uint8_t)(length >> 24);
	b->data[start + 1] = (uint8_t)(length >> 16);
	b->data[start + 2] = (uint8_t)(length >> 8);
	b->data[start + 3] = (uint8_t)length;
	put32(b, crc32_of(b->data + start + 4, b->size - start - 4));
}

// ARGB pixels as an RGB PNG.
static bool encode_png(const uint32_t *argb, uint32_t width, uint32_t height, Bytes *png)
{
	const size_t raw_size = (size_t)height * (1 + 3 * (size_t)width);
	const size_t blocks = (raw_size + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX;
	png->data = malloc(raw_size + 5 * blocks + 128);
	png->size = 0;
	uint8_t *raw = malloc(raw_size);
	if (!png->data || !raw) {
		free(raw);
		return false;
	}
	// Each row: filter 0 (none), then red, green, blue.
	for (uint32_t y = 0, at = 0; y < height; y++) {
		raw[at++] = 0;
		for (uint32_t x = 0; x < width; x++) {
			const uint32_t p = argb[(size_t)y * width + x];
			raw[at++] = (uint8_t)(p >> 16);
			raw[at++] = (uint8_t)(p >> 8);
			raw[at++] = (uint8_t)p;
		}
	}
	static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	memcpy(png->data, signature, 8);
	png->size = 8;

	size_t start = png->size;
	png->size += 4;
	memcpy(png->data + png->size, "IHDR", 4);
	png->size += 4;
	put32(png, width);
	put32(png, height);
	const uint8_t ihdr_tail[5] = { 8, 2, 0, 0, 0 }; // 8 bits, RGB, deflate, no filter set, no interlace
	memcpy(png->data + png->size, ihdr_tail, 5);
	png->size += 5;
	end_chunk(png, start);

	start = png->size;
	png->size += 4;
	memcpy(png->data + png->size, "IDAT", 4);
	png->size += 4;
	png->data[png->size++] = 0x78; // zlib: deflate, 32 KB window
	png->data[png->size++] = 0x01;
	uint32_t a = 1, b = 0; // Adler-32 of the raw data
	for (size_t at = 0; at < raw_size;) {
		const size_t n = raw_size - at < STORED_BLOCK_MAX ? raw_size - at : STORED_BLOCK_MAX;
		png->data[png->size++] = at + n == raw_size ? 1 : 0; // BFINAL, BTYPE 00 (stored)
		png->data[png->size++] = (uint8_t)n;
		png->data[png->size++] = (uint8_t)(n >> 8);
		png->data[png->size++] = (uint8_t)~n;
		png->data[png->size++] = (uint8_t)(~n >> 8);
		memcpy(png->data + png->size, raw + at, n);
		for (size_t i = 0; i < n; i++) {
			a = (a + raw[at + i]) % 65521;
			b = (b + a) % 65521;
		}
		png->size += n;
		at += n;
	}
	put32(png, b << 16 | a);
	end_chunk(png, start);

	start = png->size;
	png->size += 4;
	memcpy(png->data + png->size, "IEND", 4);
	png->size += 4;
	end_chunk(png, start);
	free(raw);
	return true;
}

static bool write_file(const char *path, const Bytes *bytes)
{
	const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return false;
	size_t done = 0;
	while (done < bytes->size) {
		const ssize_t n = write(fd, bytes->data + done, bytes->size - done);
		if (n <= 0)
			break;
		done += (size_t)n;
	}
	return close(fd) == 0 && done == bytes->size;
}

bool screenshot_init(XsdkTexture *texture, uint32_t width, uint32_t height)
{
	make_crc_table();
	const NTSTATUS status = xsdk_texture_create(texture, XSDK_FORMAT_B8G8R8A8, width, height, 1, true);
	if (!NT_SUCCESS(status))
		xsdk_log_write("screenshot: no texture for %ux%u: 0x%08X", (unsigned)width, (unsigned)height,
			(unsigned)status);
	return NT_SUCCESS(status);
}

bool screenshot_save(const XsdkTexture *texture, char *path, size_t path_size)
{
	if (!NT_SUCCESS(xsdk_gpu_wait(xsdk_gpu_fence(), FENCE_TIMEOUT_MS))) {
		xsdk_log_write("screenshot: the GPU didn't finish the frame");
		return false;
	}
	const uint32_t width = texture->width, height = texture->height;
	uint32_t *pixels = malloc((size_t)width * height * 4);
	Bytes png = { 0 };
	bool saved = false;
	if (pixels) {
		// Untiled; xsdk_gpu_resolve writes 0xBBGGRRAA words, as into the front buffer (xsdk:src/gpu_resolve.c).
		xsdk_texture_download_rows(texture, 0, 0, height, pixels, width * 4);
		for (size_t i = 0; i < (size_t)width * height; i++)
			pixels[i] = __builtin_bswap32(pixels[i]);
		struct stat info;
		if (encode_png(pixels, width, height, &png) && (stat(folder, &info) == 0 || mkdir(folder, 0777) == 0)) {
			for (int n = 1; n < 1000 && !saved; n++) {
				snprintf(path, path_size, "%s\\AvatarViewer_%03d.png", folder, n);
				if (stat(path, &info) == 0)
					continue;
				saved = write_file(path, &png);
				break;
			}
		}
	}
	free(png.data);
	free(pixels);
	return saved;
}
