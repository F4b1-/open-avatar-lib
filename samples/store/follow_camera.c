// The camera following the avatar (follow_camera.h).

#include "follow_camera.h"

#include <math.h>
#include <stdint.h>

#include "xsdk/avatar.h"

static const float yaw_step = 0.045f, pitch_step = 0.03f; // radians a frame, the stick pushed all the way
static const float min_pitch = 0.05f, max_pitch = 1.1f;
static const float wall_gap = 0.15f; // how near the eye comes to the room's walls and ceiling
// 0.95 radians (54 degrees) high, seeing 5 cm to 40 m.
static const float fov_y = 0.95f, near = 0.05f, far = 40.0f;

void follow_camera_turn(FollowCamera *c, float x, float y)
{
	c->yaw -= yaw_step * x;
	c->pitch -= pitch_step * y;
	c->pitch = c->pitch < min_pitch ? min_pitch : c->pitch > max_pitch ? max_pitch : c->pitch;
}

void follow_camera_direction(const FollowCamera *c, float x, float y, float out[2])
{
	const float forward[2] = { -sinf(c->yaw), -cosf(c->yaw) };
	const float right[2] = { cosf(c->yaw), -sinf(c->yaw) };
	out[0] = right[0] * x + forward[0] * y;
	out[1] = right[1] * x + forward[1] * y;
}

static float clamp(float v, float lo, float hi)
{
	return fmaxf(lo, fminf(hi, v));
}

void follow_camera_view(const FollowCamera *c, float x, float z, float aspect, float vp[16], float axes[3][3])
{
	const float target[3] = { x, c->height, z };
	float eye[3] = { x + c->distance * sinf(c->yaw) * cosf(c->pitch), c->height + c->distance * sinf(c->pitch),
		z + c->distance * cosf(c->yaw) * cosf(c->pitch) };
	eye[0] = clamp(eye[0], -c->room[0] + wall_gap, c->room[0] - wall_gap);
	eye[1] = fminf(c->room[1] - wall_gap, eye[1]);
	eye[2] = clamp(eye[2], -c->room[2] + wall_gap, c->room[2] - wall_gap);
	xsdk_avatar_camera(vp, eye, target, fov_y, aspect, near, far);
	// The axes are the view-projection's x, y and w rows, scaled.
	const uint32_t rows[3] = { 0, 1, 3 };
	for (uint32_t a = 0; a < 3; a++) {
		float length = 0.0f;
		for (uint32_t col = 0; col < 3; col++)
			length += vp[4 * col + rows[a]] * vp[4 * col + rows[a]];
		length = sqrtf(length);
		for (uint32_t col = 0; col < 3; col++)
			axes[a][col] = vp[4 * col + rows[a]] / length;
	}
}
