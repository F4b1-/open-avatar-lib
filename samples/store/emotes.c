// The emote wheel's emotes (emotes.h).

#include "emotes.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "xsdk/log.h"

// A stock emote: an animation (male, female; NULL keeps standing), the face while it plays, and whether it talks.
typedef struct {
	const char *name, *male, *female;
	XsdkAvatarFace face;
	bool talks;
} Emote;

// Faces: left and right eye, left and right eyebrow, mouth.
#define HAPPY { XSDK_AVATAR_EYE_HAPPY, XSDK_AVATAR_EYE_HAPPY, 0, 0, XSDK_AVATAR_MOUTH_HAPPY }
#define LAUGHING                                                                                                    \
	{ XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_EYEBROW_RAISED,  \
		XSDK_AVATAR_MOUTH_LAUGHING }
#define FACE(eye, eyebrow, mouth)                                                                                   \
	{ XSDK_AVATAR_EYE_##eye, XSDK_AVATAR_EYE_##eye, XSDK_AVATAR_EYEBROW_##eyebrow, XSDK_AVATAR_EYEBROW_##eyebrow,  \
		XSDK_AVATAR_MOUTH_##mouth }

static const Emote stock[] = {
	{ "WAVE", XSDK_AVATAR_ANIMATION_WAVE, XSDK_AVATAR_ANIMATION_WAVE, HAPPY, false },
	{ "CLAP", XSDK_AVATAR_ANIMATION_CLAP, XSDK_AVATAR_ANIMATION_CLAP, HAPPY, false },
	{ "CHEER", XSDK_AVATAR_ANIMATION_CELEBRATE, XSDK_AVATAR_ANIMATION_CELEBRATE, LAUGHING, false },
	{ "LAUGH", "Animation Male Laugh", "Animation Female Laugh", LAUGHING, false },
	{ "TALK", NULL, NULL, { 0, 0, 0, 0, 0 }, true },
	{ "ANGRY", "Animation Male Angry", "Animation Female Angry", FACE(ANGRY, ANGRY, ANGRY), false },
	{ "SAD", "Animation Male Sad Cry", "Animation Female Sad Cry", FACE(SAD, SAD, SAD), false },
	{ "CONFUSED", "Animation Male Confused", "Animation Female Confused", FACE(CONFUSED, CONFUSED, CONFUSED), false },
	{ "SHOCKED", "Animation Male Shocked Surprised", "Animation Female Shocked Surprised",
		FACE(SHOCKED, RAISED, SHOCKED), false },
	{ "YAWN", "Animation Male Yawn", "Animation Female Yawn",
		{ XSDK_AVATAR_EYE_YAWNING, XSDK_AVATAR_EYE_YAWNING, 0, 0, XSDK_AVATAR_MOUTH_AI }, false },
};
enum { STOCK = sizeof stock / sizeof stock[0] };

const XsdkAvatarFace emotes_neutral = { 0, 0, 0, 0, 0 };

// The player's own, up to MAX_CUSTOM, after the stock emotes.
enum {
	MAX_CUSTOM = EMOTES_MAX - STOCK,
	NAME_LETTERS = 9,
};
static const char custom_folder[] = "Animations";
static XsdkAvatarAnimation customs[MAX_CUSTOM];
static char custom_names[MAX_CUSTOM][NAME_LETTERS + 1];
static uint32_t custom_count;

// A file's name as the wheel's font can write it: capital letters, 0 and spaces, NAME_LETTERS at most.
static void wheel_name(char *out, const char *file)
{
	uint32_t n = 0;
	for (const char *c = file; *c && *c != '.' && n < NAME_LETTERS; c++) {
		char ch = *c >= 'a' && *c <= 'z' ? (char)(*c - 'a' + 'A') : *c;
		out[n++] = (ch >= 'A' && ch <= 'Z') || ch == '0' ? ch : ' ';
	}
	out[n] = '\0';
}

// A whole file, malloc'd into *bytes; false if it can't be read.
static bool read_whole(const char *path, void **bytes, uint32_t *size)
{
	*bytes = NULL;
	FILE *file = fopen(path, "rb");
	if (!file)
		return false;
	(void)fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	(void)fseek(file, 0, SEEK_SET);
	*bytes = length > 0 ? malloc((size_t)length) : NULL;
	const bool read = *bytes && fread(*bytes, 1, (size_t)length, file) == (size_t)length;
	(void)fclose(file);
	*size = read ? (uint32_t)length : 0;
	return read;
}

// One animation of the player's own: made in Blender on the rig, its root turns about the rig's pivot, moved onto this
// avatar's (FORMATS.md, 4.8).
static void load_custom(const XsdkAvatar *avatar, const char *file)
{
	char path[300];
	snprintf(path, sizeof path, "%s/%s", custom_folder, file);
	void *bytes;
	uint32_t size;
	XsdkAvatarAnimation *animation = &customs[custom_count];
	const int result = read_whole(path, &bytes, &size)
		? xsdk_avatar_animation_load(animation, bytes, size, xsdk_avatar_skeleton(avatar))
		: XSDK_AVATAR_ERROR_READ;
	free(bytes);
	xsdk_log_write("store: %s: %d, %u frames, %.2f s", path, result, (unsigned)animation->frame_count,
		(double)xsdk_avatar_animation_length(animation));
	if (result == XSDK_AVATAR_OK)
		wheel_name(custom_names[custom_count++], file);
}

static bool is_animation_file(const char *name)
{
	const size_t length = strlen(name);
	return length >= 5 && strcasecmp(name + length - 5, ".xaan") == 0;
}

uint32_t emotes_load(const XsdkAvatar *avatar)
{
	DIR *dir = opendir(custom_folder);
	if (!dir) {
		xsdk_log_write("store: no %s folder: no animations of your own", custom_folder);
		return STOCK;
	}
	const struct dirent *entry;
	while ((entry = readdir(dir)) && custom_count < MAX_CUSTOM)
		if (is_animation_file(entry->d_name))
			load_custom(avatar, entry->d_name);
	(void)closedir(dir);
	return STOCK + custom_count;
}

void emotes_free(void)
{
	for (uint32_t i = 0; i < custom_count; i++)
		xsdk_avatar_animation_free(&customs[i]);
	custom_count = 0;
}

uint32_t emotes_names(const char **names)
{
	for (uint32_t i = 0; i < STOCK; i++)
		names[i] = stock[i].name;
	for (uint32_t i = 0; i < custom_count; i++)
		names[STOCK + i] = custom_names[i];
	return STOCK + custom_count;
}

bool emotes_play(XsdkAvatar *avatar, uint32_t gender, uint32_t i)
{
	XsdkAvatarPlayer *player = xsdk_avatar_player(avatar);
	player->speed = 1.0f;
	if (i >= STOCK) {
		// One of the player's own: once, then back to standing, with a neutral face.
		xsdk_avatar_player_play(player, &customs[i - STOCK], XSDK_AVATAR_ONCE);
		(void)xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
		xsdk_avatar_set_face(avatar, &emotes_neutral);
		xsdk_log_write("store: %s", custom_names[i - STOCK]);
		return false;
	}
	const Emote *e = &stock[i];
	const char *animation = gender == XSDK_AVATAR_FEMALE ? e->female : e->male;
	if (animation && xsdk_avatar_play(avatar, animation, XSDK_AVATAR_ONCE) == XSDK_AVATAR_OK)
		(void)xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
	else
		(void)xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
	xsdk_avatar_set_face(avatar, &e->face);
	xsdk_log_write("store: %s", e->name);
	return e->talks;
}
