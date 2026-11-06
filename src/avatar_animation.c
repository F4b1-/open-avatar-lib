// Animations (SPECS.md, section 4.5): the animation block decoded into bone poses, sampled at a time, and poses
// blended.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "xsdk/avatar.h"

enum {
	BLOCK_ANIMATION = 1,
	TABLE_AT = 0x30,          // the channel table, after the header's 12 words
	TABLE_SLOTS = 72 * 3,     // every table has room for 72 bones' three channels; unused slots are zero bits
	CHANNEL_BITS = 146,       // f32 step, f32 minimum per axis, a 6-bit width per axis
	CHANNEL_STEP = 0x36ED4402, // 7.071068e-6, as meshes'
};

// Per-axis scale of quantised values, as for mesh positions (SPECS.md, section 3.3).
static const float axis_scale[3] = { 2.0f, 1.6329932f, 1.7320508f };

typedef struct {
	float step;
	float min[3];
	uint32_t width[3];
} Channel;

// count bits (up to 32) of a little-endian bit stream from bit at, least significant first.
static uint32_t stream_bits(const uint8_t *data, uint64_t at, uint32_t count)
{
	uint32_t value = 0;
	for (uint32_t k = 0; k < count; k++, at++)
		value |= (uint32_t)(data[at >> 3] >> (at & 7) & 1) << k;
	return value;
}

static float dot4(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

// a towards b by weight along the shorter way, normalised.
static void quaternion_blend(float *out, const float *a, const float *b, float weight)
{
	const float sign = dot4(a, b) < 0.0f ? -1.0f : 1.0f;
	float q[4], length = 0.0f;
	for (uint32_t k = 0; k < 4; k++) {
		q[k] = (1.0f - weight) * a[k] + weight * sign * b[k];
		length += q[k] * q[k];
	}
	length = length > 0.0f ? 1.0f / sqrtf(length) : 0.0f;
	for (uint32_t k = 0; k < 4; k++)
		out[k] = q[k] * length;
}

static void bone_blend(XsdkAvatarBonePose *out, const XsdkAvatarBonePose *a, const XsdkAvatarBonePose *b, float weight)
{
	XsdkAvatarBonePose result;
	for (uint32_t k = 0; k < 3; k++) {
		result.translation[k] = a->translation[k] + weight * (b->translation[k] - a->translation[k]);
		result.scale[k] = a->scale[k] + weight * (b->scale[k] - a->scale[k]);
	}
	quaternion_blend(result.rotation, a->rotation, b->rotation, weight);
	*out = result;
}

void xsdk_avatar_pose_blend(XsdkAvatarBonePose *out, const XsdkAvatarBonePose *a, const XsdkAvatarBonePose *b,
	float weight, uint32_t bone_count)
{
	for (uint32_t bone = 0; bone < bone_count; bone++)
		bone_blend(&out[bone], &a[bone], &b[bone], weight);
}

int xsdk_avatar_animation_decode(XsdkAvatarAnimation *animation, const void *strb, uint32_t size_bytes)
{
	memset(animation, 0, sizeof *animation);
	const uint8_t *block;
	uint32_t bytes;
	const int result = avatar_find_block(strb, size_bytes, BLOCK_ANIMATION, &block, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	const uint64_t data_bit = (uint64_t)TABLE_AT * 8 + (uint64_t)TABLE_SLOTS * CHANNEL_BITS;
	if (bytes < data_bit / 8)
		return XSDK_AVATAR_ERROR_FORMAT;
	// Header: u32 frames, f32 frames per second, u32 bones, u32 the prop's own bones, ... (SPECS.md, section 4.5).
	const uint32_t frames = avatar_le32(block);
	const float fps = avatar_float(avatar_le32(block + 4));
	const uint32_t bones = avatar_le32(block + 8);
	if (bones != XSDK_AVATAR_BONES)
		return XSDK_AVATAR_ERROR_UNSUPPORTED;
	if (!frames || !(fps > 0.0f))
		return XSDK_AVATAR_ERROR_FORMAT;
	// The channel table lists the bones backwards: entry t is bone bones - 1 - t, then its translation, rotation
	// and scale channels.
	Channel channels[XSDK_AVATAR_BONES][3];
	uint32_t frame_bits = 0;
	for (uint32_t t = 0; t < 3 * bones; t++) {
		const uint64_t at = (uint64_t)TABLE_AT * 8 + (uint64_t)t * CHANNEL_BITS;
		if (stream_bits(block, at, 32) != CHANNEL_STEP)
			return XSDK_AVATAR_ERROR_FORMAT;
		Channel *c = &channels[bones - 1 - t / 3][t % 3];
		c->step = avatar_float(CHANNEL_STEP);
		for (uint32_t a = 0; a < 3; a++) {
			c->min[a] = avatar_float(stream_bits(block, at + 32 + 32 * a, 32));
			c->width[a] = stream_bits(block, at + 128 + 6 * a, 6);
			if (c->width[a] > 32)
				return XSDK_AVATAR_ERROR_FORMAT;
			frame_bits += c->width[a];
		}
	}
	if (data_bit + (uint64_t)frame_bits * frames > (uint64_t)bytes * 8)
		return XSDK_AVATAR_ERROR_FORMAT;
	animation->poses = malloc(sizeof(XsdkAvatarBonePose) * frames * bones);
	if (!animation->poses)
		return XSDK_AVATAR_ERROR_MEMORY;
	animation->frame_count = frames;
	animation->frames_per_second = fps;
	animation->bone_count = bones;
	// The frames: per frame, bones 0 on, per bone translation, rotation, scale, each animated axis width bits.
	// Values are min + q * step * k; a rotation is a quaternion's logarithm (axis times half the angle).
	uint64_t at = data_bit;
	for (uint32_t f = 0; f < frames; f++)
		for (uint32_t b = 0; b < bones; b++) {
			float values[3][3];
			for (uint32_t kind = 0; kind < 3; kind++) {
				const Channel *c = &channels[b][kind];
				for (uint32_t a = 0; a < 3; a++) {
					values[kind][a] = c->min[a] + (float)stream_bits(block, at, c->width[a]) * c->step * axis_scale[a];
					at += c->width[a];
				}
			}
			XsdkAvatarBonePose *pose = &animation->poses[f * bones + b];
			memcpy(pose->translation, values[0], sizeof pose->translation);
			xsdk_avatar_quaternion_from_log(pose->rotation, values[1]);
			memcpy(pose->scale, values[2], sizeof pose->scale);
			// The data flips a rotation's sign (q and -q are the same rotation) now and then: keep each frame's on
			// the side of the one before.
			if (f > 0 && dot4(pose->rotation, animation->poses[(f - 1) * bones + b].rotation) < 0.0f)
				for (uint32_t k = 0; k < 4; k++)
					pose->rotation[k] = -pose->rotation[k];
		}
	return XSDK_AVATAR_OK;
}

void xsdk_avatar_animation_free(XsdkAvatarAnimation *animation)
{
	free(animation->poses);
	memset(animation, 0, sizeof *animation);
}

float xsdk_avatar_animation_length(const XsdkAvatarAnimation *animation)
{
	return animation->frame_count > 1 ? (float)(animation->frame_count - 1) / animation->frames_per_second : 0.0f;
}

void xsdk_avatar_animation_sample(
	const XsdkAvatarAnimation *animation, float seconds, bool loop, XsdkAvatarBonePose *pose)
{
	const uint32_t bones = animation->bone_count;
	const float last = (float)(animation->frame_count - 1);
	float frame = seconds * animation->frames_per_second;
	if (loop && last > 0.0f) {
		frame = fmodf(frame, last);
		if (frame < 0.0f)
			frame += last;
	} else if (frame < 0.0f) {
		frame = 0.0f;
	} else if (frame > last) {
		frame = last;
	}
	uint32_t first = (uint32_t)frame;
	if (first >= animation->frame_count - 1)
		first = animation->frame_count > 1 ? animation->frame_count - 2 : 0;
	const uint32_t second = animation->frame_count > 1 ? first + 1 : first;
	const float weight = animation->frame_count > 1 ? frame - (float)first : 0.0f;
	xsdk_avatar_pose_blend(pose, &animation->poses[first * bones], &animation->poses[second * bones], weight, bones);
}
