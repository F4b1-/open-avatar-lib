#ifndef STORE_EMOTES_H
#define STORE_EMOTES_H

// What the emote wheel plays: the stock emotes (an animation with a face to match, or talking), then the player's own
// animations, every .xaan file in the Animations folder beside the title (made in Blender, converted by
// tools/anim_convert.py), named after their files.

#include <stdbool.h>
#include <stdint.h>

#include "xsdk/avatar.h"

enum {
	EMOTES_MAX = 14, // the stock ten and up to four of the player's own
};

// The face an avatar goes back to after an emote.
extern const XsdkAvatarFace emotes_neutral;

// Loads the player's own animations, their root moved onto the avatar's skeleton. Returns how many emotes there are.
uint32_t emotes_load(const XsdkAvatar *avatar);
void emotes_free(void);
// Each emote's name for the wheel, into names (EMOTES_MAX); returns how many.
uint32_t emotes_names(const char **names);
// Plays emote i on the avatar (its body's version, by gender), with its face, then back to standing. Returns whether
// it talks: the title moves the mouth for a while.
bool emotes_play(XsdkAvatar *avatar, uint32_t gender, uint32_t i);

#endif
