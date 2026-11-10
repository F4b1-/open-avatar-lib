// Animations (SPECS.md, section 4.5): the animation block decoded into bone poses, the library's own animation file
// loaded, animations sampled at a time, and poses blended. Builds for the PC.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "avatar_math.h"
#include "xsdk/avatar.h"

// ---- The animation block (STRB block 1), little-endian ----
//
// A header of 12 words (u32 frames, f32 frames a second, u32 bones, u32 the prop's own bones, ...), then a channel
// table with room for 72 bones, then the frames as one bit stream. Each bone has three channels, translation,
// rotation and scale, each a quantised group (AvatarQuantised: three axes as min + q * step * k). A rotation is a
// quaternion's logarithm: the axis times half the angle.
enum {
	HEADER_FRAMES_AT = 0,
	HEADER_FPS_AT = 4,
	HEADER_BONES_AT = 8,
	TABLE_AT = 0x30,          // the channel table, after the header's 12 words
	TABLE_BONES = 72,         // every table has room for 72 bones; unused slots are zero bits
	CHANNELS_PER_BONE = 3,    // translation, rotation, scale
};

// Where the frames start: after the whole table, used slots or not.
static const uint64_t FRAMES_AT_BIT =
	(uint64_t)TABLE_AT * 8 + (uint64_t)TABLE_BONES * CHANNELS_PER_BONE * AVATAR_QUANTISED_BITS;

enum { TRANSLATION, ROTATION, SCALE };

typedef struct {
	uint32_t frames;
	float fps;
	uint32_t bones;
	AvatarQuantised channels[XSDK_AVATAR_BONES][CHANNELS_PER_BONE]; // by bone, then TRANSLATION, ROTATION, SCALE
	uint32_t frame_bits;                                    // one frame's bits: every channel's widths
} AnimationLayout;

static int read_header(const uint8_t *block, uint32_t bytes, AnimationLayout *layout)
{
	if (bytes < FRAMES_AT_BIT / 8)
		return XSDK_AVATAR_ERROR_FORMAT;
	layout->frames = avatar_le32(block + HEADER_FRAMES_AT);
	layout->fps = avatar_float(avatar_le32(block + HEADER_FPS_AT));
	layout->bones = avatar_le32(block + HEADER_BONES_AT);
	if (layout->bones != XSDK_AVATAR_BONES)
		return XSDK_AVATAR_ERROR_UNSUPPORTED; // a prop's own animation (block 1 of its package) isn't read here
	if (!layout->frames || !(layout->fps > 0.0f))
		return XSDK_AVATAR_ERROR_FORMAT;
	return XSDK_AVATAR_OK;
}

// The channel table lists the bones backwards: entry t is bone bones - 1 - t / 3, channel t % 3. Every entry's step
// is the same float (AVATAR_QUANTISATION_STEP_BITS); anything else means the table isn't understood.
static int read_channel_table(const uint8_t *block, AnimationLayout *layout)
{
	layout->frame_bits = 0;
	for (uint32_t t = 0; t < CHANNELS_PER_BONE * layout->bones; t++) {
		const uint64_t at = (uint64_t)TABLE_AT * 8 + (uint64_t)t * AVATAR_QUANTISED_BITS;
		if (avatar_stream_bits(block, at + AVATAR_QUANTISED_STEP_AT, 32) != AVATAR_QUANTISATION_STEP_BITS)
			return XSDK_AVATAR_ERROR_FORMAT;
		AvatarQuantised *c = &layout->channels[layout->bones - 1 - t / CHANNELS_PER_BONE][t % CHANNELS_PER_BONE];
		const int result = avatar_read_quantised(block, at, c);
		if (result != XSDK_AVATAR_OK)
			return result;
		layout->frame_bits += avatar_quantised_bits(c);
	}
	return XSDK_AVATAR_OK;
}

static bool frames_fit(const AnimationLayout *layout, uint32_t bytes)
{
	return FRAMES_AT_BIT + (uint64_t)layout->frame_bits * layout->frames <= (uint64_t)bytes * 8;
}

// One bone's pose in one frame, from the bit stream at *at (moved past it): each channel's axes dequantised, the
// rotation's logarithm turned into a quaternion.
static void decode_bone_pose(const uint8_t *block, uint64_t *at, const AvatarQuantised channels[CHANNELS_PER_BONE],
	XsdkAvatarBonePose *pose)
{
	float values[CHANNELS_PER_BONE][3];
	for (uint32_t kind = 0; kind < CHANNELS_PER_BONE; kind++)
		avatar_dequantise(block, at, &channels[kind], values[kind]);
	memcpy(pose->translation, values[TRANSLATION], sizeof pose->translation);
	xsdk_avatar_quaternion_from_log(pose->rotation, values[ROTATION]);
	memcpy(pose->scale, values[SCALE], sizeof pose->scale);
}

// Frame f's poses: bones 0 on. The data flips a rotation's sign (q and -q are the same rotation) now and then, so
// each is kept on the side of the frame before.
static void decode_frame(const uint8_t *block, uint64_t *at, const AnimationLayout *layout, XsdkAvatarAnimation *out,
	uint32_t f)
{
	XsdkAvatarBonePose *poses = &out->poses[f * layout->bones];
	for (uint32_t b = 0; b < layout->bones; b++) {
		decode_bone_pose(block, at, layout->channels[b], &poses[b]);
		if (f > 0)
			avatar_quaternion_keep_side(poses[b].rotation, poses[(int64_t)b - layout->bones].rotation);
	}
}

int xsdk_avatar_animation_decode(XsdkAvatarAnimation *animation, const void *strb, uint32_t size_bytes)
{
	memset(animation, 0, sizeof *animation);
	const uint8_t *block;
	uint32_t bytes;
	int result = avatar_find_block(strb, size_bytes, AVATAR_BLOCK_ANIMATION, &block, &bytes);
	AnimationLayout layout;
	if (result == XSDK_AVATAR_OK)
		result = read_header(block, bytes, &layout);
	if (result == XSDK_AVATAR_OK)
		result = read_channel_table(block, &layout);
	if (result == XSDK_AVATAR_OK && !frames_fit(&layout, bytes))
		result = XSDK_AVATAR_ERROR_FORMAT;
	if (result != XSDK_AVATAR_OK)
		return result;
	animation->poses = avatar_malloc(sizeof(XsdkAvatarBonePose) * layout.frames * layout.bones, "an animation");
	if (!animation->poses)
		return XSDK_AVATAR_ERROR_MEMORY;
	animation->frame_count = layout.frames;
	animation->frames_per_second = layout.fps;
	animation->bone_count = layout.bones;
	uint64_t at = FRAMES_AT_BIT;
	for (uint32_t f = 0; f < layout.frames; f++)
		decode_frame(block, &at, &layout, animation, f);
	return XSDK_AVATAR_OK;
}

// ---- The library's own animation file (SPECS.md, section 4.8), little-endian ----
//
// "XAAN", u32 version 1, u32 bones, u32 frames, f32 frames a second, f32 x 3 the rig's root pivot; then per frame
// f32 x 3 the root's move from that pivot and per bone f32 x 4 its rotation (x, y, z, w).
enum {
	FILE_VERSION = 1,
	FILE_HEADER_BYTES = 4 + 4 + 4 + 4 + 4 + 12,
	FILE_FRAME_MOVE_BYTES = 12,
	FILE_BONE_ROTATION_BYTES = 16,
};

typedef struct {
	uint32_t bones;
	uint32_t frames;
	float fps;
	float pivot[3]; // the rig's root, which the root's moves are from
} FileHeader;

static int read_file_header(const void *bytes, uint32_t size_bytes, FileHeader *h)
{
	AvatarReader r = avatar_reader(bytes, size_bytes);
	const uint8_t *magic = avatar_read_bytes(&r, 4);
	const uint32_t version = avatar_read_le32(&r);
	h->bones = avatar_read_le32(&r);
	h->frames = avatar_read_le32(&r);
	h->fps = avatar_read_float_le(&r);
	for (uint32_t k = 0; k < 3; k++)
		h->pivot[k] = avatar_read_float_le(&r);
	if (r.failed || memcmp(magic, "XAAN", 4) != 0)
		return XSDK_AVATAR_ERROR_FORMAT;
	if (version != FILE_VERSION)
		return XSDK_AVATAR_ERROR_UNSUPPORTED;
	const uint64_t frame_bytes = FILE_FRAME_MOVE_BYTES + FILE_BONE_ROTATION_BYTES * (uint64_t)h->bones;
	if (h->bones != XSDK_AVATAR_BONES || h->frames < 1 || !(h->fps > 0.0f) ||
		FILE_HEADER_BYTES + frame_bytes * h->frames > size_bytes)
		return XSDK_AVATAR_ERROR_FORMAT;
	return XSDK_AVATAR_OK;
}

// A frame's root move, made relative to the playing skeleton's root instead of the rig's: a rotation about another
// point moves the root by (R - 1)(ours - theirs) more (SPECS.md, section 4.7), so shift = ours - theirs.
static void move_root_onto_skeleton(XsdkAvatarBonePose *root, const float move[3], const float shift[3])
{
	float turned[3];
	avatar_quaternion_rotate(root->rotation, shift, turned);
	for (uint32_t k = 0; k < 3; k++)
		root->translation[k] = move[k] + turned[k] - shift[k];
}

int xsdk_avatar_animation_load(
	XsdkAvatarAnimation *animation, const void *bytes, uint32_t size_bytes, const XsdkAvatarSkeleton *skeleton)
{
	memset(animation, 0, sizeof *animation);
	FileHeader h;
	const int result = read_file_header(bytes, size_bytes, &h);
	if (result != XSDK_AVATAR_OK)
		return result;
	animation->poses = avatar_malloc(sizeof(XsdkAvatarBonePose) * h.bones * h.frames, "an animation");
	if (!animation->poses)
		return XSDK_AVATAR_ERROR_MEMORY;
	animation->frame_count = h.frames;
	animation->frames_per_second = h.fps;
	animation->bone_count = h.bones;
	float shift[3] = { 0, 0, 0 };
	if (skeleton && skeleton->bone_count == XSDK_AVATAR_BONES)
		for (uint32_t k = 0; k < 3; k++)
			shift[k] = skeleton->positions[XSDK_AVATAR_BONE_ROOT][k] - h.pivot[k];
	AvatarReader r = avatar_reader(bytes, size_bytes); // the sizes are checked: no read below fails
	avatar_reader_seek(&r, FILE_HEADER_BYTES);
	for (uint32_t f = 0; f < h.frames; f++) {
		XsdkAvatarBonePose *pose = animation->poses + (size_t)f * h.bones;
		xsdk_avatar_pose_reset(pose, h.bones);
		float move[3];
		for (uint32_t k = 0; k < 3; k++)
			move[k] = avatar_read_float_le(&r);
		for (uint32_t i = 0; i < h.bones; i++) {
			for (uint32_t k = 0; k < 4; k++)
				pose[i].rotation[k] = avatar_read_float_le(&r);
			if (f)
				avatar_quaternion_keep_side(pose[i].rotation, pose[(int64_t)i - h.bones].rotation);
		}
		move_root_onto_skeleton(&pose[XSDK_AVATAR_BONE_ROOT], move, shift);
	}
	return XSDK_AVATAR_OK;
}

void xsdk_avatar_animation_free(XsdkAvatarAnimation *animation)
{
	free(animation->poses);
	memset(animation, 0, sizeof *animation);
}

// ---- Playing ----

static void bone_blend(XsdkAvatarBonePose *out, const XsdkAvatarBonePose *a, const XsdkAvatarBonePose *b, float weight)
{
	XsdkAvatarBonePose result;
	for (uint32_t k = 0; k < 3; k++) {
		result.translation[k] = a->translation[k] + weight * (b->translation[k] - a->translation[k]);
		result.scale[k] = a->scale[k] + weight * (b->scale[k] - a->scale[k]);
	}
	avatar_quaternion_blend(result.rotation, a->rotation, b->rotation, weight);
	*out = result;
}

void xsdk_avatar_pose_blend(XsdkAvatarBonePose *out, const XsdkAvatarBonePose *a, const XsdkAvatarBonePose *b,
	float weight, uint32_t bone_count)
{
	for (uint32_t bone = 0; bone < bone_count; bone++)
		bone_blend(&out[bone], &a[bone], &b[bone], weight);
}

float xsdk_avatar_animation_length(const XsdkAvatarAnimation *animation)
{
	return animation->frame_count > 1 ? (float)(animation->frame_count - 1) / animation->frames_per_second : 0.0f;
}

// The frame (with its fraction) that seconds falls on: wrapped into the length if loop, else held at either end.
static float frame_at(const XsdkAvatarAnimation *animation, float seconds, bool loop)
{
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
	return frame;
}

void xsdk_avatar_animation_sample(
	const XsdkAvatarAnimation *animation, float seconds, bool loop, XsdkAvatarBonePose *pose)
{
	const uint32_t bones = animation->bone_count;
	const float frame = frame_at(animation, seconds, loop);
	// The two frames around it; with one frame, that frame twice.
	uint32_t first = (uint32_t)frame;
	if (first >= animation->frame_count - 1)
		first = animation->frame_count > 1 ? animation->frame_count - 2 : 0;
	const uint32_t second = animation->frame_count > 1 ? first + 1 : first;
	const float weight = animation->frame_count > 1 ? frame - (float)first : 0.0f;
	xsdk_avatar_pose_blend(pose, &animation->poses[first * bones], &animation->poses[second * bones], weight, bones);
}
