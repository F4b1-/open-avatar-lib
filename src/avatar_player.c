// The player: animations into poses, cross-faded and queued (avatar.h). No GPU and no files: it builds for the PC.

#include <math.h>
#include <string.h>

#include "xsdk/avatar.h"

static const float default_fade_seconds = 0.3f;

void xsdk_avatar_player_init(XsdkAvatarPlayer *player)
{
	memset(player, 0, sizeof *player);
	player->speed = 1.0f;
	player->fade_seconds = default_fade_seconds;
}

void xsdk_avatar_player_play(XsdkAvatarPlayer *player, const XsdkAvatarAnimation *animation, uint32_t mode)
{
	// The first animation starts at once: no fade from the rest pose.
	player->previous = player->current;
	player->fade = 0.0f;
	player->fade_length = player->current.animation ? player->fade_seconds : 0.0f;
	player->current = (XsdkAvatarTrack){ .animation = animation, .time = 0.0f, .loop = mode == XSDK_AVATAR_LOOP };
	player->next.animation = NULL;
}

void xsdk_avatar_player_queue(XsdkAvatarPlayer *player, const XsdkAvatarAnimation *animation, uint32_t mode)
{
	const XsdkAvatarTrack *current = &player->current;
	if (!current->animation || xsdk_avatar_player_finished(player)) {
		xsdk_avatar_player_play(player, animation, mode);
		return;
	}
	player->next = (XsdkAvatarTrack){ .animation = animation, .time = 0.0f, .loop = mode == XSDK_AVATAR_LOOP };
	// The fade ends as the current one does: at its end, or at the end of the loop's current round.
	const float length = xsdk_avatar_animation_length(current->animation);
	float end = length;
	if (current->loop && length > 0.0f)
		end = (floorf(current->time / length) + 1.0f) * length;
	player->next_at = end - player->fade_seconds;
	if (current->loop && player->next_at < current->time)
		player->next_at += length;
	if (player->next_at < current->time)
		player->next_at = current->time;
}

void xsdk_avatar_player_update(XsdkAvatarPlayer *player, float seconds)
{
	const float step = seconds * player->speed;
	player->current.time += step;
	player->previous.time += step;
	player->fade += step;
	if (player->fade >= player->fade_length)
		player->previous.animation = NULL;
	if (player->next.animation && player->current.time >= player->next_at) {
		const XsdkAvatarTrack next = player->next;
		xsdk_avatar_player_play(player, next.animation, next.loop ? XSDK_AVATAR_LOOP : XSDK_AVATAR_ONCE);
	}
}

bool xsdk_avatar_player_finished(const XsdkAvatarPlayer *player)
{
	const XsdkAvatarTrack *current = &player->current;
	return current->animation && !current->loop && !player->next.animation &&
		current->time >= xsdk_avatar_animation_length(current->animation);
}

static void track_pose(const XsdkAvatarTrack *track, XsdkAvatarBonePose *pose)
{
	if (track->animation)
		xsdk_avatar_animation_sample(track->animation, track->time, track->loop, pose);
	else
		xsdk_avatar_pose_reset(pose, XSDK_AVATAR_BONES);
}

void xsdk_avatar_player_pose(const XsdkAvatarPlayer *player, XsdkAvatarBonePose *pose)
{
	track_pose(&player->current, pose);
	if (player->fade >= player->fade_length)
		return;
	XsdkAvatarBonePose fading[XSDK_AVATAR_BONES];
	track_pose(&player->previous, fading);
	xsdk_avatar_pose_blend(pose, fading, pose, player->fade / player->fade_length, XSDK_AVATAR_BONES);
}
