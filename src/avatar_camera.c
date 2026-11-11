// Cameras and placement (avatar.h): the view-projection and world matrices a title hands the avatar layer. No GPU:
// builds for the PC (tests/camera_check.c).

#include <math.h>

#include "avatar_math.h"
#include "xsdk/avatar.h"

void xsdk_avatar_camera(float vp[16], const float eye[3], const float target[3], float fov_y, float aspect,
	float near, float far)
{
	// The camera's axes: forward to the target, right level with the ground (forward x up), up from the two.
	float forward[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
	if (!avatar_normalize3(forward))
		forward[2] = -1.0f; // eye on the target: look along -z
	float right[3] = { -forward[2], 0.0f, forward[0] };
	if (!avatar_normalize3(right))
		right[0] = 1.0f; // looking straight up or down
	float up[3];
	avatar_cross(up, right, forward);
	// Clip x and y: along right and up, scaled by the focal length; clip w: the distance along forward; clip z: the
	// same distance mapped from near..far onto 0..1 (times w, which the GPU divides out).
	const float focal = 1.0f / tanf(0.5f * fov_y), depth = far / (far - near);
	const float *rows[4] = { right, up, forward, forward };
	const float scale[4] = { focal / aspect, focal, depth, 1.0f };
	for (uint32_t row = 0; row < 4; row++) {
		const float *axis = rows[row];
		for (uint32_t col = 0; col < 3; col++)
			vp[4 * col + row] = axis[col] * scale[row];
		const float along = -avatar_dot3(axis, eye);
		vp[12 + row] = row == 2 ? (along - near) * depth : along * scale[row];
	}
}

void xsdk_avatar_placement(float world[16], float x, float y, float z, float heading)
{
	const float c = cosf(heading), s = sinf(heading);
	// Columns: the avatar's x (its left) and z (its front) turned about y, its y up, then where it stands.
	const float m[16] = { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, x, y, z, 1 };
	for (uint32_t i = 0; i < 16; i++)
		world[i] = m[i];
}
