#ifndef XSDK_AVATAR_MATH_H
#define XSDK_AVATAR_MATH_H

// The library's small vector and quaternion math, in one place. Vectors are float[3], quaternions float[4] as
// x, y, z, w (as XsdkAvatarBonePose holds them). Builds for the PC.

#include <math.h>
#include <stdint.h>

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
