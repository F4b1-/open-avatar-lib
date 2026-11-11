#ifndef XSDK_AVATAR_MATH_H
#define XSDK_AVATAR_MATH_H

// The library's small vector, quaternion and matrix math, in one place. Vectors are float[3], quaternions float[4] as
// x, y, z, w (as XsdkAvatarBonePose holds them), matrices float[16], column-major (avatar.h). Builds for the PC.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static const float avatar_identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

// a times b, column-major: b's transform, then a's.
static inline void avatar_matrix_multiply(float out[16], const float a[16], const float b[16])
{
	for (uint32_t c = 0; c < 4; c++)
		for (uint32_t r = 0; r < 4; r++)
			out[4 * c + r] =
				a[r] * b[4 * c] + a[4 + r] * b[4 * c + 1] + a[8 + r] * b[4 * c + 2] + a[12 + r] * b[4 * c + 3];
}

// a x b.
static inline void avatar_cross(float out[3], const float *a, const float *b)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static inline void avatar_quaternion_identity(float q[4])
{
	q[0] = q[1] = q[2] = 0.0f;
	q[3] = 1.0f;
}

static inline float avatar_dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline float avatar_dot4(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

// v scaled to length 1; false (v unchanged) if it has no length to scale.
static inline bool avatar_normalize3(float *v)
{
	const float length = sqrtf(avatar_dot3(v, v));
	if (!(length > 0.0f))
		return false;
	for (uint32_t k = 0; k < 3; k++)
		v[k] /= length;
	return true;
}

// q turned into -q (the same rotation) if it lies on the other side of `near` (their dot product below 0), so that
// interpolating between near and q takes the shorter way.
static inline void avatar_quaternion_keep_side(float *q, const float *near)
{
	if (avatar_dot4(q, near) < 0.0f)
		for (uint32_t k = 0; k < 4; k++)
			q[k] = -q[k];
}

// a towards b by weight along the shorter way, normalised (a normalised linear blend).
static inline void avatar_quaternion_blend(float *out, const float *a, const float *b, float weight)
{
	const float sign = avatar_dot4(a, b) < 0.0f ? -1.0f : 1.0f;
	float q[4], length = 0.0f;
	for (uint32_t k = 0; k < 4; k++) {
		q[k] = (1.0f - weight) * a[k] + weight * sign * b[k];
		length += q[k] * q[k];
	}
	length = length > 0.0f ? 1.0f / sqrtf(length) : 0.0f;
	for (uint32_t k = 0; k < 4; k++)
		out[k] = q[k] * length;
}

// The rotation matrix of a unit quaternion, rows of three.
static inline void avatar_quaternion_matrix(const float *q, float m[3][3])
{
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	m[0][0] = 1 - 2 * (y * y + z * z);
	m[0][1] = 2 * (x * y - z * w);
	m[0][2] = 2 * (x * z + y * w);
	m[1][0] = 2 * (x * y + z * w);
	m[1][1] = 1 - 2 * (x * x + z * z);
	m[1][2] = 2 * (y * z - x * w);
	m[2][0] = 2 * (x * z - y * w);
	m[2][1] = 2 * (y * z + x * w);
	m[2][2] = 1 - 2 * (x * x + y * y);
}

// v turned by the unit quaternion q (q v q*), into out.
static inline void avatar_quaternion_rotate(const float *q, const float *v, float *out)
{
	float m[3][3];
	avatar_quaternion_matrix(q, m);
	for (uint32_t k = 0; k < 3; k++)
		out[k] = m[k][0] * v[0] + m[k][1] * v[1] + m[k][2] * v[2];
}

#endif
