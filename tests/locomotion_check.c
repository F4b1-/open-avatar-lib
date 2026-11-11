// PC test of locomotion (src/avatar_locomotion.c): driven frame by frame at 60 a second with a player, on the male
// body's skeleton from the console's stock pack, standing with its "Animation Generic Stand 0":
//   locomotion_check <FFFE07DF00000002>
// Standing still stays put; the stick pushed walks at the top speed the way it points, the avatar turning to face it,
// the walk played at its feet's pace; half pushed walks at half the speed; the trigger runs; letting go stands; a
// wall stops it; a jump stays in place and stands again after landing; the stick interrupts an emote. Exits 1 if a
// check fails.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsdk/avatar.h"

static const float dt = 1.0f / 60.0f;
static int failures;

static void check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAILED", what);
	failures += !ok;
}

static bool near_to(float a, float b, float within)
{
	return fabsf(a - b) <= within;
}

static int read_file(void *context, uint64_t offset, void *buffer, uint32_t length)
{
	FILE *file = context;
	if (fseek(file, (long)offset, SEEK_SET) != 0)
		return 1;
	return fread(buffer, 1, length, file) == length ? 0 : 1;
}

static void *read_record(const XsdkAvatarPack *pack, const char *name, uint32_t *size)
{
	uint32_t record;
	if (xsdk_avatar_pack_find(pack, name, &record) != XSDK_AVATAR_OK)
		return NULL;
	*size = xsdk_avatar_pack_asset_size(pack, record);
	void *strb = malloc(*size);
	if (strb && xsdk_avatar_pack_read_asset(pack, record, strb) != XSDK_AVATAR_OK) {
		free(strb);
		return NULL;
	}
	return strb;
}

// A wall along z = 1: nothing goes past it.
static void wall(void *context, float *x, float *z, float radius)
{
	(void)context;
	(void)x;
	if (*z > 1.0f - radius)
		*z = 1.0f - radius;
}

// Frames of the stick held at (dx, dz), run as given; the player moved on as xsdk_avatar_update would. Returns how
// many frames reported starting to move.
static uint32_t drive(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *p, float dx, float dz, bool run, uint32_t frames,
	XsdkAvatarCollide collide)
{
	uint32_t started = 0;
	for (uint32_t f = 0; f < frames; f++) {
		started += xsdk_avatar_locomotion_update(l, p, dx, dz, run, dt, collide, NULL);
		xsdk_avatar_player_update(p, dt);
	}
	return started;
}

static void restart(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *p)
{
	l->x = l->z = 0.0f;
	l->heading = 0.0f;
	drive(l, p, 0.0f, 0.0f, false, 60, NULL);
}

static void check_moving(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *p)
{
	drive(l, p, 0.0f, 0.0f, false, 60, NULL);
	check(l->state == XSDK_AVATAR_STANDING && l->x == 0.0f && l->z == 0.0f, "standing still stays put");

	// Pushed all the way to +x for a second: 1.4 m that way, facing +x (pi/2), the walk at its feet's pace.
	const uint32_t started = drive(l, p, 1.0f, 0.0f, false, 60, NULL);
	printf("walk: x %.3f z %.3f heading %.3f, speed %.3f (walk %.3f m/s)\n", (double)l->x, (double)l->z,
		(double)l->heading, (double)p->speed, (double)l->walk_speed);
	check(l->state == XSDK_AVATAR_WALKING && p->current.animation == &l->walk && started == 1, "walks, started once");
	check(near_to(l->x, 1.4f, 0.01f) && l->z == 0.0f, "1.4 m in a second, the way the stick points");
	check(near_to(l->heading, 1.5707963f, 0.001f), "turned to face it");
	check(near_to(p->speed, 1.4f / l->walk_speed, 0.001f), "the walk at its feet's pace");

	restart(l, p);
	drive(l, p, 0.0f, 0.5f, false, 60, NULL);
	check(near_to(l->z, 0.7f, 0.01f), "half pushed: half the speed");

	restart(l, p);
	drive(l, p, 0.0f, 1.0f, true, 60, NULL);
	printf("run: z %.3f, speed %.3f (run %.3f m/s)\n", (double)l->z, (double)p->speed, (double)l->run_speed);
	check(l->state == XSDK_AVATAR_RUNNING && p->current.animation == &l->run, "the trigger runs");
	check(near_to(l->z, 2.6f, 0.01f) && near_to(p->speed, 2.6f / l->run_speed, 0.001f), "at 2.6 m/s, its pace");

	drive(l, p, 0.0f, 0.0f, false, 1, NULL);
	check(l->state == XSDK_AVATAR_STANDING && p->current.animation == l->stand && p->speed == 1.0f, "letting go stands");

	restart(l, p);
	drive(l, p, 3.0f, 4.0f, false, 60, NULL);
	check(near_to(sqrtf(l->x * l->x + l->z * l->z), 1.4f, 0.01f), "a long direction counts as all the way");

	restart(l, p);
	drive(l, p, 0.0f, 1.0f, true, 120, wall);
	check(near_to(l->z, 1.0f - l->radius, 1e-6f), "the wall stops it");
}

static void check_jump(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *p)
{
	restart(l, p);
	check(xsdk_avatar_locomotion_jump(l, p) && l->state == XSDK_AVATAR_JUMPING, "jumps");
	check(!xsdk_avatar_locomotion_jump(l, p), "not again while jumping");
	// The stick pushed all through: it stays where it jumped until standing again.
	const float length = xsdk_avatar_animation_length(&l->jump);
	uint32_t frames = 0;
	while (l->state == XSDK_AVATAR_JUMPING && frames < 600) {
		xsdk_avatar_locomotion_update(l, p, 1.0f, 0.0f, false, dt, NULL, NULL);
		xsdk_avatar_player_update(p, dt);
		if (l->state == XSDK_AVATAR_JUMPING && l->x != 0.0f)
			break;
		frames++;
	}
	printf("jump: %.2f s, standing again after %.2f s\n", (double)length, (double)(frames * dt));
	check(l->x == 0.0f || l->state != XSDK_AVATAR_JUMPING, "in place while jumping");
	check(frames * dt > length - 0.5f && frames * dt < length + 0.2f, "standing again as the jump ends");
	drive(l, p, 1.0f, 0.0f, false, 1, NULL);
	check(l->state == XSDK_AVATAR_WALKING, "and walks off after");
}

static void check_busy(XsdkAvatarLocomotion *l, XsdkAvatarPlayer *p)
{
	restart(l, p);
	l->state = XSDK_AVATAR_BUSY;
	drive(l, p, 0.0f, 0.0f, false, 30, NULL);
	check(l->state == XSDK_AVATAR_BUSY, "an emote stays while the stick rests");
	check(drive(l, p, 0.0f, 1.0f, false, 1, NULL) == 1 && l->state == XSDK_AVATAR_WALKING, "the stick interrupts it");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: locomotion_check <FFFE07DF00000002>\n");
		return 2;
	}
	FILE *file = fopen(argv[1], "rb");
	XsdkAvatarPack *pack;
	if (!file || xsdk_avatar_pack_open(&pack, read_file, file) != XSDK_AVATAR_OK)
		return 2;
	uint32_t size;
	void *strb = read_record(pack, "MaleBody", &size);
	static XsdkAvatarModel body;
	static XsdkAvatarSkeleton skeleton;
	if (!strb || xsdk_avatar_model_decode(&body, strb, size) != XSDK_AVATAR_OK ||
		xsdk_avatar_skeleton_build(&skeleton, &body) != XSDK_AVATAR_OK)
		return 2;
	free(strb);
	strb = read_record(pack, XSDK_AVATAR_ANIMATION_STAND, &size);
	static XsdkAvatarAnimation stand;
	if (!strb || xsdk_avatar_animation_decode(&stand, strb, size) != XSDK_AVATAR_OK)
		return 2;
	free(strb);

	static XsdkAvatarLocomotion l;
	XsdkAvatarPlayer p;
	xsdk_avatar_player_init(&p);
	const int result = xsdk_avatar_locomotion_init(&l, XSDK_AVATAR_MALE, &skeleton, &stand);
	check(result == XSDK_AVATAR_OK && l.can_run && l.can_jump && l.walk_speed > 0.0f, "loads walk, run and jump");
	if (result == XSDK_AVATAR_OK) {
		xsdk_avatar_player_play(&p, &stand, XSDK_AVATAR_LOOP);
		check_moving(&l, &p);
		check_jump(&l, &p);
		check_busy(&l, &p);
	}
	xsdk_avatar_locomotion_free(&l);
	xsdk_avatar_animation_free(&stand);
	xsdk_avatar_model_free(&body);
	xsdk_avatar_pack_close(pack);
	fclose(file);
	printf(failures ? "locomotion: %d FAILED\n" : "locomotion: all passed\n", failures);
	return failures ? 1 : 0;
}
