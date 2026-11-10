// Skeletons and poses (SPECS.md, sections 4.4 and 4.5): the avatar's derived skeleton, props' skeleton blocks, and
// the skin matrices of a pose. Builds for the PC.

#include <math.h>
#include <string.h>

#include "avatar_internal.h"
#include "avatar_math.h"
#include "xsdk/avatar.h"

// A prop's skeleton block (STRB block 5), little-endian: a header, two quantised groups (positions, then
// rotations), then per bone a u8 parent and a value of each group, all one bit stream.
enum {
	BLOCK_SKELETON = 5,
	SKELETON_HEADER_BYTES = 0x20,
	SKELETON_BONE_COUNT_AT = 0x1C,
	PARENT_BITS = 8,
	GROUPS = 2,
	QUANTISED_BYTES = (AVATAR_QUANTISED_BITS + 7) / 8,
};

// The avatar's parents (SPECS.md, section 4.4): the named bones by anatomy, the unnamed ones by where their skin sits;
// the table keeps the XNA reference's rule that bones are sorted by depth.
static const uint8_t avatar_parents[XSDK_AVATAR_BONES] = {
	XSDK_AVATAR_NO_PARENT,
	0, 0, 0, 0,             // 1 BackLower, 2 HipLeft, 3 HipRight, 4 pelvis skin
	1, 2, 2, 3, 3, 1,       // 5 BackUpper, 6 KneeLeft, 7 thigh, 8 KneeRight, 9 thigh, 10 lower spine
	6, 5, 6, 5, 8, 5, 8, 5, // 11 AnkleLeft, 12 CollarLeft, 13 shin, 14 Neck, 15 AnkleRight, 16 CollarRight, 17 shin, 18 chest
	14, 12, 11, 16, 15, 14, // 19 Head, 20 ShoulderLeft, 21 ToeLeft, 22 ShoulderRight, 23 ToeRight, 24 neck skin
	20, 20, 20, 22, 22, 22, // 25 ElbowLeft, 26 upper arm, 27 twist, 28 ElbowRight, 29 upper arm, 30 twist
	25, 25, 25, 28, 28, 28, // 31 forearm twist, 32 forearm, 33 WristLeft, 34, 35, 36 WristRight
	33, 33, 33, 33, 33, 33, 33, // 37-43: index, middle, ring, small, prop, special, thumb (left)
	36, 36, 36, 36, 36, 36, 36, // 44-50: the same, right
	37, 38, 39, 40, 43,     // 51-55: second joints, left
	44, 45, 46, 47, 50,     // 56-60: second joints, right
	51, 52, 53, 54, 55,     // 61-65: third joints, left
	56, 57, 58, 59, 60,     // 66-70: third joints, right
};

void xsdk_avatar_quaternion_from_log(float quaternion[4], const float log[3])
{
	const float half = sqrtf(log[0] * log[0] + log[1] * log[1] + log[2] * log[2]);
	const float k = half > 1e-8f ? sinf(half) / half : 1.0f; // sin(x) / x tends to 1
	for (uint32_t a = 0; a < 3; a++)
		quaternion[a] = log[a] * k;
	quaternion[3] = cosf(half);
}

void xsdk_avatar_quaternion_from_axis(float quaternion[4], float x, float y, float z, float angle)
{
	const float length = sqrtf(x * x + y * y + z * z);
	if (length < 1e-8f) {
		avatar_quaternion_identity(quaternion);
		return;
	}
	const float log[3] = { x / length * 0.5f * angle, y / length * 0.5f * angle, z / length * 0.5f * angle };
	xsdk_avatar_quaternion_from_log(quaternion, log);
}

// A rigid transform or one with scale: x' = r x + t.
typedef struct {
	float r[3][3];
	float t[3];
} Transform;

static Transform compose(const Transform *a, const Transform *b)
{
	Transform out;
	for (uint32_t i = 0; i < 3; i++) {
		for (uint32_t j = 0; j < 3; j++)
			out.r[i][j] = a->r[i][0] * b->r[0][j] + a->r[i][1] * b->r[1][j] + a->r[i][2] * b->r[2][j];
		out.t[i] = a->r[i][0] * b->t[0] + a->r[i][1] * b->t[1] + a->r[i][2] * b->t[2] + a->t[i];
	}
	return out;
}

// The inverse of a rotation and translation.
static Transform inverse_rigid(const Transform *a)
{
	Transform out;
	for (uint32_t i = 0; i < 3; i++)
		for (uint32_t j = 0; j < 3; j++)
			out.r[i][j] = a->r[j][i];
	for (uint32_t i = 0; i < 3; i++)
		out.t[i] = -(out.r[i][0] * a->t[0] + out.r[i][1] * a->t[1] + out.r[i][2] * a->t[2]);
	return out;
}

static Transform rest_of(const XsdkAvatarSkeleton *skeleton, uint32_t bone)
{
	Transform out;
	avatar_quaternion_matrix(skeleton->rotations[bone], out.r);
	memcpy(out.t, skeleton->positions[bone], sizeof out.t);
	return out;
}

// Adds one vertex's evidence of where joints are: per bone, the weighted sum of the positions where its subtree's
// skin meets the rest of the body ([0-2]) and the weights' sum ([3]). Each pair of bones the vertex weights (by the
// smaller of the two weights) counts for every bone on the path between them, below their common ancestor; parents
// being numbered below their children, that path is found by stepping up from the larger index.
static void add_joint_evidence(double sums[XSDK_AVATAR_BONES][4], const uint8_t *bones, const uint8_t *weights,
	const float *position)
{
	for (uint32_t i = 0; i < 4; i++)
		for (uint32_t j = i + 1; j < 4; j++) {
			if (!weights[i] || !weights[j] || bones[i] == bones[j] || bones[i] >= XSDK_AVATAR_BONES ||
				bones[j] >= XSDK_AVATAR_BONES)
				continue;
			const double x = weights[i] < weights[j] ? weights[i] : weights[j];
			uint32_t a = bones[i], b = bones[j];
			while (a != b) {
				uint32_t *deeper = a > b ? &a : &b;
				for (uint32_t c = 0; c < 3; c++)
					sums[*deeper][c] += x * position[c];
				sums[*deeper][3] += x;
				*deeper = avatar_parents[*deeper];
			}
		}
}

int xsdk_avatar_skeleton_build(XsdkAvatarSkeleton *skeleton, const XsdkAvatarModel *body)
{
	memset(skeleton, 0, sizeof *skeleton);
	// Each bone's pivot: the weighted centre of the vertices its subtree shares with the rest of the body.
	double sums[XSDK_AVATAR_BONES][4] = { { 0 } };
	uint32_t skinned = 0;
	for (uint32_t m = 0; m < body->mesh_count; m++) {
		const XsdkAvatarMesh *mesh = &body->meshes[m];
		if (!mesh->bones)
			continue;
		skinned++;
		for (uint32_t v = 0; v < mesh->vertex_count; v++)
			add_joint_evidence(sums, mesh->bones + 4 * v, mesh->weights + 4 * v, mesh->positions + 3 * v);
	}
	if (!skinned || !sums[XSDK_AVATAR_BONE_HIP_LEFT][3] || !sums[XSDK_AVATAR_BONE_HIP_RIGHT][3])
		return XSDK_AVATAR_ERROR_FORMAT;
	skeleton->bone_count = XSDK_AVATAR_BONES;
	for (uint32_t b = 0; b < XSDK_AVATAR_BONES; b++) {
		skeleton->parents[b] = avatar_parents[b];
		avatar_quaternion_identity(skeleton->rotations[b]);
		if (sums[b][3] > 0)
			for (uint32_t c = 0; c < 3; c++)
				skeleton->positions[b][c] = (float)(sums[b][c] / sums[b][3]);
	}
	// The root midway between the hips; bones without skin (props, special) where their parent is.
	for (uint32_t c = 0; c < 3; c++)
		skeleton->positions[XSDK_AVATAR_BONE_ROOT][c] = 0.5f * (skeleton->positions[XSDK_AVATAR_BONE_HIP_LEFT][c] +
												   skeleton->positions[XSDK_AVATAR_BONE_HIP_RIGHT][c]);
	for (uint32_t b = 1; b < XSDK_AVATAR_BONES; b++)
		if (!(sums[b][3] > 0))
			memcpy(skeleton->positions[b], skeleton->positions[avatar_parents[b]], sizeof skeleton->positions[b]);
	return XSDK_AVATAR_OK;
}

int xsdk_avatar_skeleton_decode(XsdkAvatarSkeleton *skeleton, const void *strb, uint32_t size_bytes)
{
	memset(skeleton, 0, sizeof *skeleton);
	const uint8_t *block;
	uint32_t bytes;
	int result = avatar_find_block(strb, size_bytes, BLOCK_SKELETON, &block, &bytes);
	if (result != XSDK_AVATAR_OK)
		return result;
	if (bytes < SKELETON_HEADER_BYTES + GROUPS * QUANTISED_BYTES)
		return XSDK_AVATAR_ERROR_FORMAT;
	const uint32_t count = avatar_le32(block + SKELETON_BONE_COUNT_AT);
	if (!count || count > XSDK_AVATAR_MAX_BONES)
		return XSDK_AVATAR_ERROR_UNSUPPORTED;
	uint64_t at = (uint64_t)SKELETON_HEADER_BYTES * 8;
	AvatarQuantised positions, rotations;
	result = avatar_read_quantised(block, at, &positions);
	if (result == XSDK_AVATAR_OK)
		result = avatar_read_quantised(block, at + AVATAR_QUANTISED_BITS, &rotations);
	if (result != XSDK_AVATAR_OK)
		return result;
	at += GROUPS * AVATAR_QUANTISED_BITS;
	const uint32_t bone_bits = PARENT_BITS + avatar_quantised_bits(&positions) + avatar_quantised_bits(&rotations);
	if (at + (uint64_t)bone_bits * count > (uint64_t)bytes * 8)
		return XSDK_AVATAR_ERROR_FORMAT;
	// Per bone: its parent (below it; the root's none), its rest position, its rest orientation as a quaternion's
	// logarithm (SPECS.md, 4.4).
	skeleton->bone_count = count;
	for (uint32_t b = 0; b < count; b++) {
		const uint32_t parent = avatar_stream_bits(block, at, PARENT_BITS);
		at += PARENT_BITS;
		if (b == 0 ? parent != XSDK_AVATAR_NO_PARENT : parent >= b)
			return XSDK_AVATAR_ERROR_FORMAT;
		skeleton->parents[b] = (uint8_t)parent;
		float rotation_log[3];
		avatar_dequantise(block, &at, &positions, skeleton->positions[b]);
		avatar_dequantise(block, &at, &rotations, rotation_log);
		xsdk_avatar_quaternion_from_log(skeleton->rotations[b], rotation_log);
	}
	return XSDK_AVATAR_OK;
}

void xsdk_avatar_pose_reset(XsdkAvatarBonePose *pose, uint32_t bone_count)
{
	for (uint32_t b = 0; b < bone_count; b++) {
		memset(&pose[b], 0, sizeof pose[b]);
		avatar_quaternion_identity(pose[b].rotation);
		pose[b].scale[0] = pose[b].scale[1] = pose[b].scale[2] = 1.0f;
	}
}

// The bone's transform in its parent's frame: its rest transform relative to its parent's, then the pose's
// translation, rotation and scale.
static Transform local_transform(const XsdkAvatarSkeleton *skeleton, const XsdkAvatarBonePose *pose, uint32_t b)
{
	const uint32_t parent = skeleton->parents[b];
	const Transform rest = rest_of(skeleton, b);
	Transform local = rest;
	if (parent != XSDK_AVATAR_NO_PARENT) {
		const Transform parent_rest = rest_of(skeleton, parent);
		const Transform inverse = inverse_rigid(&parent_rest);
		local = compose(&inverse, &rest);
	}
	Transform change;
	avatar_quaternion_matrix(pose->rotation, change.r);
	for (uint32_t i = 0; i < 3; i++) {
		for (uint32_t j = 0; j < 3; j++)
			change.r[i][j] *= pose->scale[j];
		change.t[i] = pose->translation[i];
	}
	return compose(&local, &change);
}

void xsdk_avatar_skin_matrices(
	const XsdkAvatarSkeleton *skeleton, const XsdkAvatarBonePose *pose, float (*matrices)[12])
{
	Transform world[XSDK_AVATAR_MAX_BONES];
	for (uint32_t b = 0; b < skeleton->bone_count && b < XSDK_AVATAR_MAX_BONES; b++) {
		const uint32_t parent = skeleton->parents[b];
		const Transform local = local_transform(skeleton, &pose[b], b);
		world[b] = parent == XSDK_AVATAR_NO_PARENT ? local : compose(&world[parent], &local);
		// The skin matrix takes a rest-pose vertex to the posed bone: out of the bone's rest frame, then into its pose.
		const Transform rest = rest_of(skeleton, b);
		const Transform inverse = inverse_rigid(&rest);
		const Transform skin = compose(&world[b], &inverse);
		for (uint32_t i = 0; i < 3; i++) {
			for (uint32_t j = 0; j < 3; j++)
				matrices[b][4 * i + j] = skin.r[i][j];
			matrices[b][4 * i + 3] = skin.t[i];
		}
	}
}
