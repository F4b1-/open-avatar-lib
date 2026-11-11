// Walking, running and jumping (locomotion.h).

#include "locomotion.h"

#include <math.h>
#include <string.h>

#include "xsdk/log.h"

static const float pi = 3.14159265f;
// A clip played slower or faster than this to match the ground speed would look wrong: the speed is held inside it.
static const float min_pace = 0.6f, max_pace = 2.0f;

static const char *body_name(uint32_t gender)
{
	return gender == XSDK_AVATAR_FEMALE ? "female" : "male";
}

bool locomotion_load(Locomotion *l, XsdkAvatar *avatar, float x, float z, float heading)
{
	memset(l, 0, sizeof *l);
	l->avatar = avatar;
	l->x = x;
	l->z = z;
	l->heading = heading;
	l->walk_top_speed = 1.4f;
	l->run_top_speed = 2.6f;
	l->turn_rate = 9.0f;
	l->radius = 0.28f;
	XsdkAvatarInfo info;
	xsdk_avatar_info(avatar, &info);
	const XsdkAvatarSkeleton *skeleton = xsdk_avatar_skeleton(avatar);
	// Each clip for the avatar's body, its root moved onto the avatar's skeleton; the speeds for the pace.
	const int walk = xsdk_avatar_animation_load_walk(&l->walk, info.gender, skeleton, &l->walk_speed);
	xsdk_log_write("locomotion: walk for the %s body: %d, %.3f m/s", body_name(info.gender), walk,
		(double)l->walk_speed);
	if (walk != XSDK_AVATAR_OK || l->walk_speed <= 0.0f)
		return false;
	const int run = xsdk_avatar_animation_load_run(&l->run, info.gender, skeleton, &l->run_speed);
	l->can_run = run == XSDK_AVATAR_OK && l->run_speed > 0.0f;
	xsdk_log_write("locomotion: run for the %s body: %d, %.3f m/s", body_name(info.gender), run, (double)l->run_speed);
	const int jump = xsdk_avatar_animation_load_jump(&l->jump, info.gender, skeleton);
	l->can_jump = jump == XSDK_AVATAR_OK;
	xsdk_log_write("locomotion: jump for the %s body: %d", body_name(info.gender), jump);
	return true;
}

void locomotion_free(Locomotion *l)
{
	xsdk_avatar_animation_free(&l->walk);
	xsdk_avatar_animation_free(&l->run);
	xsdk_avatar_animation_free(&l->jump);
}

bool locomotion_jump(Locomotion *l)
{
	if (!l->can_jump || l->state == LOCOMOTION_JUMPING)
		return false;
	XsdkAvatarPlayer *player = xsdk_avatar_player(l->avatar);
	player->speed = 1.0f;
	xsdk_avatar_player_play(player, &l->jump, XSDK_AVATAR_ONCE);
	(void)xsdk_avatar_queue(l->avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
	l->state = LOCOMOTION_JUMPING;
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
static bool move(Locomotion *l, float dx, float dz, float push, bool run, float seconds, LocomotionCollide collide)
{
	XsdkAvatarPlayer *player = xsdk_avatar_player(l->avatar);
	const bool running = l->can_run && run;
	const float speed = push * (running ? l->run_top_speed : l->walk_top_speed);
	l->heading = turn_towards(l->heading, atan2f(dx, dz), l->turn_rate * seconds);
	l->x += dx / push * speed * seconds;
	l->z += dz / push * speed * seconds;
	if (collide)
		collide(&l->x, &l->z, l->radius);
	const uint32_t state = running ? LOCOMOTION_RUNNING : LOCOMOTION_WALKING;
	const bool started = l->state != state;
	if (started) {
		xsdk_avatar_player_play(player, running ? &l->run : &l->walk, XSDK_AVATAR_LOOP);
		l->state = state;
	}
	const float pace = speed / (running ? l->run_speed : l->walk_speed);
	player->speed = pace < min_pace ? min_pace : pace > max_pace ? max_pace : pace;
	return started;
}

bool locomotion_update(Locomotion *l, float dx, float dz, bool run, float seconds, LocomotionCollide collide)
{
	float push = sqrtf(dx * dx + dz * dz);
	if (push > 1.0f) {
		dx /= push;
		dz /= push;
		push = 1.0f;
	}
	bool started = false;
	if (l->state == LOCOMOTION_JUMPING) {
		// In place: the feet stay where they stood until the jump has landed and its standing has begun.
		if (xsdk_avatar_player(l->avatar)->current.loop)
			l->state = LOCOMOTION_STANDING;
	} else if (push > 0.0f) {
		started = move(l, dx, dz, push, run, seconds, collide);
	} else if (l->state == LOCOMOTION_WALKING || l->state == LOCOMOTION_RUNNING) {
		xsdk_avatar_player(l->avatar)->speed = 1.0f;
		(void)xsdk_avatar_play(l->avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
		l->state = LOCOMOTION_STANDING;
	}
	float world[16];
	xsdk_avatar_placement(world, l->x, 0.0f, l->z, l->heading);
	xsdk_avatar_set_transform(l->avatar, world);
	return started;
}
