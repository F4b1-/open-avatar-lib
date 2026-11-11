#ifndef STORE_LOCOMOTION_H
#define STORE_LOCOMOTION_H

// An avatar that walks, runs and jumps where a stick points: the library's walk, run and jump
// (xsdk_avatar_animation_load_walk, _run, _jump) fitted to the avatar's body, each played at the pace its feet cover
// the ground at, the avatar turning towards where it goes. Nothing of the store in it: what it bumps into comes from
// the title, so it can start a game of its own.
//
//     Locomotion me;
//     locomotion_load(&me, avatar);              // after the avatar loads
//     each frame: locomotion_update(&me, dx, dz, run_held, dt, collide); xsdk_avatar_update(avatar, dt);
//     at the end: locomotion_free(&me);          // after xsdk_avatar_close

#include <stdbool.h>
#include <stdint.h>

#include "xsdk/avatar.h"

enum {
	LOCOMOTION_STANDING,
	LOCOMOTION_WALKING,
	LOCOMOTION_RUNNING,
	LOCOMOTION_JUMPING, // in place, until the jump has landed and the standing after it plays
	LOCOMOTION_BUSY,    // the title plays something of its own (an emote) until the stick moves it or the title sets
	                    // STANDING again
};

typedef struct {
	XsdkAvatar *avatar;
	XsdkAvatarAnimation walk, run, jump;
	float walk_speed, run_speed; // metres a second the clips' feet cover, played at speed 1
	bool can_run, can_jump;      // whether the run and the jump loaded (the walk must)
	uint32_t state;              // LOCOMOTION_*
	float x, z;                  // where it stands, on the ground (y 0)
	float heading;               // radians about y from facing +z (xsdk_avatar_placement)
	// The feel, set by locomotion_load and free to change: the stick pushed all the way walks at walk_top_speed and
	// runs at run_top_speed (metres a second); it turns at turn_rate radians a second; it is a circle of radius metres.
	float walk_top_speed, run_top_speed, turn_rate, radius;
} Locomotion;

// Moves a circle of radius at (*x, *z) out of whatever it overlaps.
typedef void (*LocomotionCollide)(float *x, float *z, float radius);

// Loads the walk, run and jump for the avatar's body and skeleton, standing at (x, z) facing heading. False if the
// walk doesn't load (logged); a run or jump that doesn't is left out.
bool locomotion_load(Locomotion *l, XsdkAvatar *avatar, float x, float z, float heading);
// The clips; after the avatar is closed (or after a fence past the last frame that drew it).
void locomotion_free(Locomotion *l);
// Jumps in place, once, then stands: false (nothing done) if it can't jump or is jumping.
bool locomotion_jump(Locomotion *l);
// Moves it on by seconds: towards (dx, dz), a world direction whose length (up to 1) is how far the stick is pushed
// (0 stands), running while run is held; then out of what collide finds; and places the avatar there. True when it
// has just started walking or running, for the title to drop what it was doing (a face, talking).
bool locomotion_update(Locomotion *l, float dx, float dz, bool run, float seconds, LocomotionCollide collide);

#endif
