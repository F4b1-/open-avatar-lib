// PC test of the camera and placement helpers (src/avatar_camera.c): where points land through the camera, where
// the placement puts an avatar's front, and that the camera equals, bit for bit, the cameras the samples wrote for
// themselves before it (their formulas copied here), so that they can switch to it with nothing on screen changing.
//   camera_check
// Prints each check; exits 1 if one fails.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xsdk/avatar.h"

static int failures;

static void check(bool ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAILED", what);
	failures += !ok;
}

// p through the column-major m: clip x, y, z, w.
static void transform(const float m[16], const float p[3], float out[4])
{
	for (int r = 0; r < 4; r++)
		out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

static bool near_to(float a, float b)
{
	return fabsf(a - b) < 1e-4f;
}

// samples/store/main.c's look(), before the helper.
static void store_look(float vp[16], const float eye[3], const float target[3], float aspect)
{
	const float focal = 1.0f / tanf(0.5f * 0.95f), near = 0.05f, far = 40.0f, k = far / (far - near);
	float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
	const float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	for (int i = 0; i < 3; i++)
		f[i] /= fl;
	float r[3] = { -f[2], 0.0f, f[0] };
	const float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
	r[0] /= rl;
	r[2] /= rl;
	const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
	const float *rows[4] = { r, u, f, f };
	const float scale[4] = { focal / aspect, focal, k, 1.0f };
	for (int row = 0; row < 4; row++) {
		const float *a = rows[row];
		for (int col = 0; col < 3; col++)
			vp[4 * col + row] = a[col] * scale[row];
		const float along = -(a[0] * eye[0] + a[1] * eye[1] + a[2] * eye[2]);
		vp[12 + row] = row == 2 ? (along - near) * k : along * scale[row];
	}
}

// samples/viewer/main.c's camera(), before the helper: orbiting target at yaw.
static void viewer_camera(float vp[16], const float target[3], float yaw, float distance, float aspect)
{
	const float focal = 1.0f / tanf(0.5f * 0.6f);
	const float near = 0.1f, far = 30.0f, k = far / (far - near);
	const float eye[3] = { target[0] + distance * sinf(yaw), target[1], target[2] + distance * cosf(yaw) };
	const float f[3] = { -sinf(yaw), 0.0f, -cosf(yaw) };
	const float r[3] = { -f[2], 0.0f, f[0] };
	const float u[3] = { 0.0f, 1.0f, 0.0f };
	const float scale[4] = { focal / aspect, focal, k, 1.0f };
	const float *axes[4] = { r, u, f, f };
	for (int row = 0; row < 4; row++) {
		const float *a = axes[row];
		for (int col = 0; col < 3; col++)
			vp[4 * col + row] = a[col] * scale[row];
		const float along = -(a[0] * eye[0] + a[1] * eye[1] + a[2] * eye[2]);
		vp[12 + row] = row == 2 ? (along - near) * k : along * scale[row];
	}
}

static float largest_difference(const float a[16], const float b[16])
{
	float most = 0.0f;
	for (int i = 0; i < 16; i++)
		most = fmaxf(most, fabsf(a[i] - b[i]));
	return most;
}

static void check_geometry(void)
{
	const float eye[3] = { 1.0f, 1.5f, 3.0f }, target[3] = { 0.0f, 1.0f, 0.0f };
	const float near = 0.1f, far = 30.0f;
	float vp[16], c[4];
	xsdk_avatar_camera(vp, eye, target, 0.8f, 16.0f / 9.0f, near, far);
	const float distance = sqrtf(1.0f + 0.25f + 9.0f);
	transform(vp, target, c);
	check(near_to(c[0], 0.0f) && near_to(c[1], 0.0f) && near_to(c[3], distance), "the target in the middle");
	float f[3], p[3];
	for (int k = 0; k < 3; k++)
		f[k] = (target[k] - eye[k]) / distance;
	for (int k = 0; k < 3; k++)
		p[k] = eye[k] + near * f[k];
	transform(vp, p, c);
	check(near_to(c[2] / c[3], 0.0f), "depth 0 at near");
	for (int k = 0; k < 3; k++)
		p[k] = eye[k] + far * f[k];
	transform(vp, p, c);
	check(near_to(c[2] / c[3], 1.0f), "depth 1 at far");
	// Seen from +x+z, the point at +x-z of the target is on the screen's right; one above it, up.
	const float right_of[3] = { 0.5f, 1.0f, -0.5f / 3.0f }, above[3] = { 0.0f, 1.5f, 0.0f };
	transform(vp, right_of, c);
	check(c[0] > 0.0f, "right on the right");
	transform(vp, above, c);
	check(c[1] > 0.0f, "up up");
	const float down_eye[3] = { 0.0f, 5.0f, 0.0f }, origin[3] = { 0.0f, 0.0f, 0.0f };
	xsdk_avatar_camera(vp, down_eye, origin, 0.8f, 1.0f, near, far);
	bool finite = true;
	for (int i = 0; i < 16; i++)
		finite = finite && isfinite(vp[i]);
	check(finite, "looking straight down");
}

static void check_samples_cameras(void)
{
	const float eyes[][3] = { { 0.3f, 1.4f, 4.0f }, { -2.5f, 2.0f, -1.0f }, { 3.0f, 0.4f, 0.2f } };
	const float targets[][3] = { { 0.0f, 0.9f, 1.0f }, { 1.0f, 1.0f, 2.0f }, { -1.0f, 1.2f, 0.0f } };
	float most = 0.0f;
	for (int i = 0; i < 3; i++) {
		float mine[16], theirs[16];
		xsdk_avatar_camera(mine, eyes[i], targets[i], 0.95f, 16.0f / 9.0f, 0.05f, 40.0f);
		store_look(theirs, eyes[i], targets[i], 16.0f / 9.0f);
		most = fmaxf(most, largest_difference(mine, theirs));
	}
	printf("store camera: largest difference %g\n", (double)most);
	check(most == 0.0f, "the store's camera, bit for bit");
	most = 0.0f;
	for (float yaw = -3.0f; yaw < 3.2f; yaw += 0.7f) {
		const float target[3] = { 0.4f, 1.0f, 0.0f };
		const float eye[3] = { target[0] + 3.0f * sinf(yaw), target[1], target[2] + 3.0f * cosf(yaw) };
		float mine[16], theirs[16];
		xsdk_avatar_camera(mine, eye, target, 0.6f, 4.0f / 3.0f, 0.1f, 30.0f);
		viewer_camera(theirs, target, yaw, 3.0f, 4.0f / 3.0f);
		most = fmaxf(most, largest_difference(mine, theirs));
	}
	printf("viewer camera: largest difference %g\n", (double)most);
	check(most < 1e-5f, "the viewer's camera, to rounding (it builds its axes from sin and cos)");
}

static void check_placement(void)
{
	float world[16], c[4];
	xsdk_avatar_placement(world, 2.0f, 0.5f, -1.0f, 1.5707963f);
	const float front[3] = { 0.0f, 0.0f, 1.0f };
	transform(world, front, c);
	check(near_to(c[0], 3.0f) && near_to(c[1], 0.5f) && near_to(c[2], -1.0f) && near_to(c[3], 1.0f),
		"heading pi/2 faces +x");
	const float store[16] = { cosf(0.7f), 0, -sinf(0.7f), 0, 0, 1, 0, 0, sinf(0.7f), 0, cosf(0.7f), 0, 1, 0, 2, 1 };
	xsdk_avatar_placement(world, 1.0f, 0.0f, 2.0f, 0.7f);
	check(memcmp(world, store, sizeof world) == 0, "the store's placement, bit for bit");
}

int main(void)
{
	check_geometry();
	check_samples_cameras();
	check_placement();
	printf(failures ? "camera: %d FAILED\n" : "camera: all passed\n", failures);
	return failures ? 1 : 0;
}
