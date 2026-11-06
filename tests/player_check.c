// PC test of the player (src/avatar_player.c): cross-fades, queueing, one-shots and speed, on two made-up
// animations one second long that hold bone 0 at x 1 (A) or x 2 (B), so bone 0's x shows the blend. No console files.
//   player_check     prints each failed check and exits 1, or prints "player: all passed"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "xsdk/avatar.h"

static uint32_t failures;

static void make(XsdkAvatarAnimation *animation, float x)
{
	animation->frame_count = 2;
	animation->frames_per_second = 1.0f;
	animation->bone_count = XSDK_AVATAR_BONES;
	animation->poses = calloc(2 * XSDK_AVATAR_BONES, sizeof(XsdkAvatarBonePose));
	if (!animation->poses)
		exit(2);
	for (uint32_t f = 0; f < 2; f++) {
		xsdk_avatar_pose_reset(animation->poses + f * XSDK_AVATAR_BONES, XSDK_AVATAR_BONES);
		animation->poses[f * XSDK_AVATAR_BONES].translation[0] = x;
	}
}

static float bone_x(const XsdkAvatarPlayer *player)
{
	XsdkAvatarBonePose pose[XSDK_AVATAR_BONES];
	xsdk_avatar_player_pose(player, pose);
	return pose[0].translation[0];
}

static void expect(const char *what, float got, float want)
{
	if (fabsf(got - want) > 1e-4f) {
		printf("player: %s: %.4f, not %.4f\n", what, (double)got, (double)want);
		failures++;
	}
}

static void expect_true(const char *what, bool got, bool want)
{
	if (got != want) {
		printf("player: %s: %s\n", what, got ? "true" : "false");
		failures++;
	}
}

int main(void)
{
	XsdkAvatarAnimation a, b;
	make(&a, 1.0f);
	make(&b, 2.0f);
	XsdkAvatarPlayer player;
	xsdk_avatar_player_init(&player);
	expect("nothing playing: the rest pose", bone_x(&player), 0.0f);
	expect_true("nothing playing hasn't finished", xsdk_avatar_player_finished(&player), false);

	xsdk_avatar_player_play(&player, &a, XSDK_AVATAR_LOOP);
	expect("the first animation starts at once", bone_x(&player), 1.0f);
	xsdk_avatar_player_update(&player, 0.5f);
	xsdk_avatar_player_play(&player, &b, XSDK_AVATAR_ONCE);
	expect("a cross-fade starts at the old pose", bone_x(&player), 1.0f);
	xsdk_avatar_player_update(&player, 0.15f);
	expect("half way through the 0.3 s fade", bone_x(&player), 1.5f);
	xsdk_avatar_player_update(&player, 0.15f);
	expect("the fade done", bone_x(&player), 2.0f);
	expect_true("a one-shot playing hasn't finished", xsdk_avatar_player_finished(&player), false);
	xsdk_avatar_player_update(&player, 0.7f);
	expect_true("a one-shot at its end has finished", xsdk_avatar_player_finished(&player), true);
	expect("a finished one-shot holds its last frame", bone_x(&player), 2.0f);

	// Queued after a one-shot: the fade ends with it, starting 0.3 s before its end.
	xsdk_avatar_player_play(&player, &b, XSDK_AVATAR_ONCE);
	xsdk_avatar_player_update(&player, 0.3f); // past the fade from b to b
	xsdk_avatar_player_queue(&player, &a, XSDK_AVATAR_LOOP);
	expect("queued after a one-shot: starts at length - fade", player.next_at, 0.7f);
	expect_true("a one-shot with a queue won't finish", xsdk_avatar_player_finished(&player), false);
	xsdk_avatar_player_update(&player, 0.39f);
	expect("before next_at the one-shot plays", bone_x(&player), 2.0f);
	xsdk_avatar_player_update(&player, 0.02f);
	expect_true("at next_at the queued one plays", player.current.animation == &a, true);
	xsdk_avatar_player_update(&player, 0.15f);
	expect("and fades in", bone_x(&player), 1.5f);

	// Queued after a loop: at the end of its current round.
	xsdk_avatar_player_play(&player, &a, XSDK_AVATAR_LOOP);
	xsdk_avatar_player_update(&player, 1.5f);
	xsdk_avatar_player_queue(&player, &b, XSDK_AVATAR_ONCE);
	expect("queued in a loop's second round", player.next_at, 1.7f);
	xsdk_avatar_player_play(&player, &a, XSDK_AVATAR_LOOP);
	expect_true("play drops the queue", player.next.animation == NULL, true);
	xsdk_avatar_player_update(&player, 0.8f);
	xsdk_avatar_player_queue(&player, &b, XSDK_AVATAR_ONCE);
	expect("queued too late for this round: the next", player.next_at, 1.7f);

	// Queued when a one-shot has finished, or nothing plays: at once.
	xsdk_avatar_player_play(&player, &b, XSDK_AVATAR_ONCE);
	xsdk_avatar_player_update(&player, 2.0f);
	xsdk_avatar_player_queue(&player, &a, XSDK_AVATAR_LOOP);
	expect_true("queued after a finished one-shot plays at once", player.current.animation == &a, true);

	xsdk_avatar_player_init(&player);
	player.speed = 2.0f;
	xsdk_avatar_player_play(&player, &a, XSDK_AVATAR_LOOP);
	xsdk_avatar_player_update(&player, 0.1f);
	expect("speed 2 runs time twice as fast", player.current.time, 0.2f);
	player.fade_seconds = 0.0f;
	xsdk_avatar_player_play(&player, &b, XSDK_AVATAR_LOOP);
	expect("fade_seconds 0 cuts", bone_x(&player), 2.0f);

	xsdk_avatar_animation_free(&a);
	xsdk_avatar_animation_free(&b);
	if (failures)
		return 1;
	printf("player: all passed\n");
	return 0;
}
