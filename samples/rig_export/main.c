// Rig export: writes rigs to animate in Blender (or any 3D program that reads glTF) beside the .xex, from this
// console's own files (FORMATS.md, section 4.9; the README's "Making animations"):
//   rig_male.glb, rig_female.glb  the bare body and head, male and female
//   rig_mine.glb                  the avatar of the first profile signed in, in its clothes
// Each holds the library's skeleton in the T-pose and the avatar's textures; tools/anim_convert.py turns an animation
// made on it into the library's own file. The files hold the console's meshes: keep them to yourself.
// The screen shows each file as it is written and how it went; any button then goes back to the dashboard.
// Log: game:\rig_export.log.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "screen.h"
#include "xsdk/avatar.h"
#include "xsdk/log.h"

enum {
	USERS = 4,
	FILES = 3,
	WAITING = 0xFF8A93A6,
	WORKING = 0xFFFFD34D,
	DONE = 0xFF6EE07A,
	FAILED = 0xFFFF6B6B,
	NOTE = 0xFFDDE3EE,
};

static const char title[] = "Rig export";

// A skin tone for the bare bodies.
static const XsdkAvatarStockPart male[] = { { "MaleBody", 0xFFDCAD8F }, { "Head", 0xFFDCAD8F } };
static const XsdkAvatarStockPart female[] = { { "FemaleBody", 0xFFDCAD8F }, { "Head", 0xFFDCAD8F } };

static const struct {
	const char *path;
	const char *what;
} files[FILES] = {
	{ "game:\\rig_male.glb", "rig_male.glb" },
	{ "game:\\rig_female.glb", "rig_female.glb" },
	{ "game:\\rig_mine.glb", "rig_mine.glb" },
};

static ScreenLines lines;

static void set_line(uint32_t i, uint32_t argb, const char *format, const char *what, const char *detail)
{
	snprintf(lines.text[i], sizeof lines.text[i], format, what, detail);
	lines.argb[i] = argb;
	if (lines.count <= i)
		lines.count = i + 1;
}

static const char *error_name(int result)
{
	switch (result) {
	case XSDK_AVATAR_ERROR_READ:
		return "can't read the console's files";
	case XSDK_AVATAR_ERROR_FORMAT:
		return "a file not understood";
	case XSDK_AVATAR_ERROR_MEMORY:
		return "out of memory";
	case XSDK_AVATAR_ERROR_NOT_FOUND:
		return "not found";
	case XSDK_AVATAR_ERROR_UNSUPPORTED:
		return "not supported";
	default:
		return "failed";
	}
}

static bool write_file(const char *path, const void *data, uint32_t size)
{
	FILE *file = fopen(path, "wb");
	if (!file)
		return false;
	const bool written = fwrite(data, 1, size, file) == size;
	return fclose(file) == 0 && written;
}

// Writes what an export gave, logs it and puts it on its line; frees it.
static bool save(uint32_t i, int result, void *glb, uint32_t size)
{
	const bool saved = result == XSDK_AVATAR_OK && write_file(files[i].path, glb, size);
	free(glb);
	char detail[40];
	if (saved) {
		snprintf(detail, sizeof detail, "%u KB", (unsigned)(size / 1024));
		set_line(i, DONE, "%-15s %s", files[i].what, detail);
		xsdk_log_write("rig_export: %s, %u KB", files[i].path, (unsigned)(size / 1024));
	} else {
		snprintf(detail, sizeof detail, "%s", result == XSDK_AVATAR_OK ? "can't write the file" : error_name(result));
		set_line(i, FAILED, "%-15s %s", files[i].what, detail);
		xsdk_log_write("rig_export: %s not written: %d", files[i].path, result);
	}
	return saved;
}

static void working(uint32_t i)
{
	set_line(i, WORKING, "%-15s %s", files[i].what, "writing...");
	screen_show(title, &lines);
}

int main(void)
{
	(void)screen_open();
	for (uint32_t i = 0; i < FILES; i++)
		set_line(i, WAITING, "%-15s %s", files[i].what, "waiting");
	screen_show(title, &lines);

	void *glb;
	uint32_t size;
	uint32_t written = 0;
	working(0);
	int result = xsdk_avatar_export_stock(male, 2, &glb, &size);
	written += save(0, result, glb, size);
	working(1);
	result = xsdk_avatar_export_stock(female, 2, &glb, &size);
	written += save(1, result, glb, size);
	working(2);
	result = XSDK_AVATAR_ERROR_NOT_FOUND;
	for (uint32_t user = 0; user < USERS && result != XSDK_AVATAR_OK; user++)
		result = xsdk_avatar_export_profile(user, &glb, &size);
	if (result == XSDK_AVATAR_ERROR_NOT_FOUND) {
		set_line(2, FAILED, "%-15s %s", files[2].what, "no avatar signed in");
		xsdk_log_write("rig_export: no profile with an avatar is signed in: no rig_mine.glb");
	} else
		written += save(2, result, glb, size);
	xsdk_avatar_shutdown();
	xsdk_log_write("rig_export: %u files written", (unsigned)written);

	// How it went, where the files are, and the way out.
	char summary[SCREEN_LINE_CHARS + 1];
	if (written == FILES)
		snprintf(summary, sizeof summary, "Done: all %u files written beside rig_export.xex.", (unsigned)FILES);
	else if (written)
		snprintf(summary, sizeof summary, "Done: %u of %u files written beside rig_export.xex.", (unsigned)written,
			(unsigned)FILES);
	else
		snprintf(summary, sizeof summary, "Failed: no file written.");
	set_line(FILES + 1, written == FILES ? DONE : written ? WORKING : FAILED, "%s%s", summary, "");
	if (written)
		set_line(FILES + 2, NOTE, "%s%s", "Copy them to the PC and import them in Blender.", "");
	set_line(lines.count + 1, NOTE, "%s%s", "Details: rig_export.log.", "");
	set_line(lines.count + 1, WAITING, "%s%s", "Press any button to go back to the dashboard.", "");
	screen_show(title, &lines);
	screen_wait_button();
	screen_close();
	return written ? 0 : 1;
}
