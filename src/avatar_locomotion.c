// Locomotion (avatar.h): walking, running and jumping where a stick points, with the library's own clips. No GPU and
// no files: it builds for the PC (tests/locomotion_check.c). Grew out of samples/store (refactoring step 6).

#include <math.h>
#include <string.h>

#include "xsdk/avatar.h"

static const float pi = 3.14159265f;
// A clip played slower or faster than this to match the ground speed would look wrong: the speed is held inside it.
static const float min_pace = 0.6f, max_pace = 2.0f;

int xsdk_avatar_locomotion_init(XsdkAvatarLocomotion *l, uint32_t gender, const XsdkAvatarSkeleton *skeleton,
	const XsdkAvatarAnimation *stand)
{
	memset(l, 0, sizeof *l);
	const int result = xsdk_avatar_animation_load_walk(&l->walk, gender, skeleton, &l->walk_speed);
	if (result != XSDK_AVATAR_OK)
		return result;
	if (!(l->walk_speed > 0.0f)) {
		xsdk_avatar_animation_free(&l->walk);
		memset(l, 0, sizeof *l);
		return XSDK_AVATAR_ERROR_FORMAT;
	}
	const int run = xsdk_avatar_animation_load_run(&l->run, gender, skeleton, &l->run_speed);
	const int jump = xsdk_avatar_animation_load_jump(&l->jump, gender, skeleton);
	if (run == XSDK_AVATAR_ERROR_MEMORY || jump == XSDK_AVATAR_ERROR_MEMORY) {
		xsdk_avatar_locomotion_free(l);
		return XSDK_AVATAR_ERROR_MEMORY;
	}
	l->can_run = run == XSDK_AVATAR_OK && l->run_speed > 0.0f;
	l->can_jump = jump == XSDK_AVATAR_OK;
	l->stand = stand;
	l->walk_top_speed = 1.4f;
	l->run_top_speed = 2.6f;
	l->turn_rate = 9.0f;
	l->radius = 0.28f;
	return XSDK_AVATAR_OK;
}

void xsdk_avatar_locomotion_free(XsdkAvatarLocomotion *l)
{
	xsdk_avatar_animation_free(&l->walk);
	xsdk_avatar_animation_free(&l->run);
	xsdk_avatar_animation_free(&l->jump);
	memset(l, 0, sizeof *l);
}

bool xsdk_avatar_locomotion_jump(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *player)
{
	if (!l->can_jump || l->state == XSDK_AVATAR_JUMPING)
		return false;
	player->speed = 1.0f;
	xsdk_avatar_player_play(player, &l->jump, XSDK_AVATAR_ONCE);
	xsdk_avatar_player_queue(player, l->stand, XSDK_AVATAR_LOOP);
	l->state = XSDK_AVATAR_JUMPING;
	return true;
}

// heading turned towards target by at most step radians, the shorter way round.
static float turn_towards(float heading, float target, float step)
{
	float d = target - heading;
	while (d > pi)
		d -= 2.0f * pi;
	while (d < -pi)
		d += 2.0f * pi;
	return heading + (fabsf(d) <= step ? d : (d > 0 ? step : -step));
}

// One step of moving: turning towards (dx, dz), going push of the top speed that way, out of what it bumps into; the
// clip to match, at the pace its feet go. Returns whether it has just started moving.
static bool move(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *player, float dx, float dz, float push, bool run,
	float seconds, XsdkAvatarCollide collide, void *context)
{
	const bool running = l->can_run && run;
	const float speed = push * (running ? l->run_top_speed : l->walk_top_speed);
	l->heading = turn_towards(l->heading, atan2f(dx, dz), l->turn_rate * seconds);
	l->x += dx / push * speed * seconds;
	l->z += dz / push * speed * seconds;
	if (collide)
		collide(context, &l->x, &l->z, l->radius);
	const uint32_t state = running ? XSDK_AVATAR_RUNNING : XSDK_AVATAR_WALKING;
	const bool started = l->state != state;
	if (started) {
		xsdk_avatar_player_play(player, running ? &l->run : &l->walk, XSDK_AVATAR_LOOP);
		l->state = state;
	}
	const float pace = speed / (running ? l->run_speed : l->walk_speed);
	player->speed = pace < min_pace ? min_pace : pace > max_pace ? max_pace : pace;
	return started;
}

bool xsdk_avatar_locomotion_update(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *player, float dx, float dz, bool run,
	float seconds, XsdkAvatarCollide collide, void *context)
{
	float push = sqrtf(dx * dx + dz * dz);
	if (push > 1.0f) {
		dx /= push;
		dz /= push;
		push = 1.0f;
	}
	if (l->state == XSDK_AVATAR_JUMPING) {
		// In place: the feet stay where they stood until the jump has landed and its standing has begun.
		if (player->current.loop)
			l->state = XSDK_AVATAR_STANDING;
		return false;
	}
	if (push > 0.0f)
		return move(l, player, dx, dz, push, run, seconds, collide, context);
	if (l->state == XSDK_AVATAR_WALKING || l->state == XSDK_AVATAR_RUNNING) {
		player->speed = 1.0f;
		xsdk_avatar_player_play(player, l->stand, XSDK_AVATAR_LOOP);
		l->state = XSDK_AVATAR_STANDING;
	}
	return false;
}
