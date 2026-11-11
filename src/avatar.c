// The avatar layer (avatar.h): an avatar loaded, posed by its own player and drawn. avatar_outfit.c finds its parts,
// avatar_render.c draws them, avatar_content.c reads the console's files.

#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "avatar_math.h"
#include "avatar_runtime.h"
#include "xsdk/log.h"

struct XsdkAvatar {
	AvatarDrawable drawable;
	XsdkAvatarSkeleton skeleton;
	XsdkAvatarPlayer player;
	float world[16];
	float skin[XSDK_AVATAR_BONES][12]; // of the pose of the last update
	XsdkAvatarInfo info;
	// The face: what the title set, blinking (a blink while blink_in is below 0) and talking.
	XsdkAvatarFace face;
	bool blinking;
	float blink_in;
	float talking;
	float talk_time; // seconds the current lip shape has shown
	uint8_t talk_shape;
	uint32_t random;
	AvatarFaceFrames frames; // of the last update
};

static const float blink_seconds = 0.15f;
static const float blink_gap_min = 2.0f, blink_gap_max = 6.0f;
static const float first_blink_min = 0.5f; // after loading, sooner than the gap between blinks
// Each load's random seed: the load's number times 2^32 / the golden ratio, which spreads them over 32 bits.
static const uint32_t seed_step = 0x9E3779B9u;
static const float talk_step_seconds = 0.1f;
static const float talk_silent = 0.05f; // levels below this are silence

// Lip-sync shapes by how open they are (the pictures, SPECS.md, section 3.5): quiet, middle and loud speech.
enum { TALK_LEVELS = 3, TALK_SHAPES = 3 };
static const uint8_t talk_shapes[TALK_LEVELS][TALK_SHAPES] = {
	{ XSDK_AVATAR_MOUTH_FV, XSDK_AVATAR_MOUTH_W, XSDK_AVATAR_MOUTH_DTH },
	{ XSDK_AVATAR_MOUTH_EE, XSDK_AVATAR_MOUTH_DTH, XSDK_AVATAR_MOUTH_O },
	{ XSDK_AVATAR_MOUTH_AI, XSDK_AVATAR_MOUTH_L, XSDK_AVATAR_MOUTH_O },
};

// xorshift32: each avatar's own, so that avatars blink at different times.
static uint32_t next_random(XsdkAvatar *avatar)
{
	uint32_t x = avatar->random;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	avatar->random = x;
	return x;
}

// low to high, from the random number's top 24 bits (as many as a float's mantissa holds).
static float random_between(XsdkAvatar *avatar, float low, float high)
{
	return low + (high - low) * (float)(next_random(avatar) >> 8) / (float)(1u << 24);
}

// A lip shape for the talking level, not the one showing.
static void pick_talk_shape(XsdkAvatar *avatar)
{
	uint32_t level = (uint32_t)(avatar->talking * TALK_LEVELS);
	if (level >= TALK_LEVELS)
		level = TALK_LEVELS - 1;
	uint8_t shape = talk_shapes[level][next_random(avatar) % TALK_SHAPES];
	if (shape == avatar->talk_shape)
		shape = talk_shapes[level][(next_random(avatar) % (TALK_SHAPES - 1) + 1) % TALK_SHAPES];
	avatar->talk_shape = shape;
	avatar->talk_time = 0.0f;
}

// The frames each half of the face shows now: the title's face, the eyes closed in a blink, the mouth talking.
static void face_now(XsdkAvatar *avatar)
{
	const XsdkAvatarFace *face = &avatar->face;
	const bool blink = avatar->blinking && avatar->blink_in < 0.0f;
	const bool talking = avatar->talking >= talk_silent;
	memset(avatar->frames, 0, sizeof avatar->frames);
	const uint8_t eyes[AVATAR_SIDES] = { face->left_eye, face->right_eye };
	const uint8_t eyebrows[AVATAR_SIDES] = { face->left_eyebrow, face->right_eyebrow };
	for (uint32_t s = 0; s < AVATAR_SIDES; s++) {
		avatar->frames[s][AVATAR_LAYER_EYES] = blink ? XSDK_AVATAR_EYE_BLINK : eyes[s];
		avatar->frames[s][AVATAR_LAYER_EYEBROWS] = eyebrows[s];
		avatar->frames[s][AVATAR_LAYER_MOUTH] = talking ? avatar->talk_shape : face->mouth;
	}
}

static void face_update(XsdkAvatar *avatar, float seconds)
{
	if (avatar->blinking) {
		avatar->blink_in -= seconds;
		if (avatar->blink_in < -blink_seconds)
			avatar->blink_in = random_between(avatar, blink_gap_min, blink_gap_max);
	}
	if (avatar->talking >= talk_silent) {
		avatar->talk_time += seconds;
		if (avatar->talk_time >= talk_step_seconds)
			pick_talk_shape(avatar);
	}
	face_now(avatar);
}

// The pose's skin matrices; the identity for every bone without a skeleton.
static void skin_of(const XsdkAvatar *avatar, const XsdkAvatarBonePose *pose, float (*skin)[12])
{
	if (avatar->skeleton.bone_count == XSDK_AVATAR_BONES) {
		xsdk_avatar_skin_matrices(&avatar->skeleton, pose, skin);
		return;
	}
	// Each matrix is three rows of four: the identity's ones at row r, column r.
	memset(skin, 0, sizeof(float) * 12 * XSDK_AVATAR_BONES);
	for (uint32_t b = 0; b < XSDK_AVATAR_BONES; b++)
		skin[b][0] = skin[b][5] = skin[b][10] = 1.0f;
}

static void pose_now(XsdkAvatar *avatar)
{
	XsdkAvatarBonePose pose[XSDK_AVATAR_BONES];
	xsdk_avatar_player_pose(&avatar->player, pose);
	skin_of(avatar, pose, avatar->skin);
}

// What the avatar keeps of its outfit once drawn: the skeleton, and the counts and sizes XsdkAvatarInfo reports.
static void keep_from_outfit(XsdkAvatar *avatar, const AvatarOutfit *outfit)
{
	avatar->skeleton = outfit->skeleton;
	avatar->info = (XsdkAvatarInfo){ .gender = outfit->gender,
		.part_count = outfit->part_count,
		.draw_count = avatar->drawable.draw_count,
		.replaced_count = outfit->replaced_count,
		.left_out_count = outfit->left_out_count,
		.height = outfit->top,
		.gpu_bytes = avatar->drawable.gpu_bytes,
		.cpu_bytes = (uint32_t)sizeof *avatar + avatar->drawable.cpu_bytes };
}

// A loaded avatar's start: at the origin, nothing playing, its own random numbers, blinking soon.
static void start(XsdkAvatar *avatar)
{
	memcpy(avatar->world, avatar_identity, sizeof avatar_identity);
	xsdk_avatar_player_init(&avatar->player);
	pose_now(avatar);
	static uint32_t loads;
	avatar->random = seed_step * ++loads;
	avatar->blinking = true;
	avatar->blink_in = random_between(avatar, first_blink_min, blink_gap_max);
	face_now(avatar);
}

static int load(XsdkAvatar **out, const XsdkAvatarDescription *d)
{
	*out = NULL;
	int result = avatar_render_open();
	if (result != XSDK_AVATAR_OK)
		return result;
	AvatarOutfit *outfit = avatar_malloc(sizeof *outfit, "an avatar");
	XsdkAvatar *avatar = avatar_calloc(1, sizeof *avatar, "an avatar");
	if (!outfit || !avatar) {
		result = XSDK_AVATAR_ERROR_MEMORY;
		goto done;
	}
	result = avatar_outfit_load(outfit, d);
	if (result == XSDK_AVATAR_OK)
		result = avatar_render_build(&avatar->drawable, outfit);
	if (result != XSDK_AVATAR_OK) {
		xsdk_log_write("avatar: load failed: %d", result);
		avatar_outfit_free(outfit);
		goto done;
	}
	keep_from_outfit(avatar, outfit);
	avatar_outfit_free(outfit);
	start(avatar);
	xsdk_log_write("avatar: loaded: %u parts, %u draws, %u replaced, %u left out, %u KB GPU, %u KB CPU",
		(unsigned)avatar->info.part_count, (unsigned)avatar->info.draw_count, (unsigned)avatar->info.replaced_count,
		(unsigned)avatar->info.left_out_count, (unsigned)(avatar->info.gpu_bytes / 1024),
		(unsigned)(avatar->info.cpu_bytes / 1024));
	*out = avatar;
	avatar = NULL;
done:
	free(outfit);
	free(avatar);
	return result;
}

int xsdk_avatar_load_profile(XsdkAvatar **avatar, uint32_t user_index)
{
	*avatar = NULL;
	static uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
	const int result = avatar_content_read_profile(user_index, bytes);
	return result == XSDK_AVATAR_OK ? xsdk_avatar_load_description(avatar, bytes, sizeof bytes) : result;
}

int xsdk_avatar_load_description(XsdkAvatar **avatar, const void *bytes, uint32_t size_bytes)
{
	*avatar = NULL;
	XsdkAvatarDescription d;
	const int result = xsdk_avatar_description_parse(&d, bytes, size_bytes);
	return result == XSDK_AVATAR_OK ? load(avatar, &d) : result;
}

int xsdk_avatar_load_stock(XsdkAvatar **avatar, const XsdkAvatarStockPart *parts, uint32_t count)
{
	*avatar = NULL;
	XsdkAvatarDescription d;
	const int result = avatar_outfit_describe_stock(&d, parts, count);
	return result == XSDK_AVATAR_OK ? load(avatar, &d) : result;
}

void xsdk_avatar_info(const XsdkAvatar *avatar, XsdkAvatarInfo *info)
{
	*info = avatar->info;
}

void xsdk_avatar_close(XsdkAvatar *avatar)
{
	if (!avatar)
		return;
	avatar_render_free(&avatar->drawable);
	free(avatar);
}

int xsdk_avatar_play(XsdkAvatar *avatar, const char *name, uint32_t mode)
{
	const XsdkAvatarAnimation *animation;
	const int result = xsdk_avatar_stock_animation(name, &animation);
	if (result == XSDK_AVATAR_OK)
		xsdk_avatar_player_play(&avatar->player, animation, mode);
	return result;
}

int xsdk_avatar_queue(XsdkAvatar *avatar, const char *name, uint32_t mode)
{
	const XsdkAvatarAnimation *animation;
	const int result = xsdk_avatar_stock_animation(name, &animation);
	if (result == XSDK_AVATAR_OK)
		xsdk_avatar_player_queue(&avatar->player, animation, mode);
	return result;
}

bool xsdk_avatar_finished(const XsdkAvatar *avatar)
{
	return xsdk_avatar_player_finished(&avatar->player);
}

XsdkAvatarPlayer *xsdk_avatar_player(XsdkAvatar *avatar)
{
	return &avatar->player;
}

void xsdk_avatar_update(XsdkAvatar *avatar, float seconds)
{
	xsdk_avatar_player_update(&avatar->player, seconds);
	pose_now(avatar);
	face_update(avatar, seconds);
}

void xsdk_avatar_set_face(XsdkAvatar *avatar, const XsdkAvatarFace *face)
{
	avatar->face = *face;
	face_now(avatar);
}

void xsdk_avatar_set_blinking(XsdkAvatar *avatar, bool blinking)
{
	avatar->blinking = blinking;
	if (avatar->blink_in < 0.0f)
		avatar->blink_in = random_between(avatar, blink_gap_min, blink_gap_max);
	face_now(avatar);
}

void xsdk_avatar_set_talking(XsdkAvatar *avatar, float level)
{
	const bool was_talking = avatar->talking >= talk_silent;
	avatar->talking = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
	if (!was_talking && avatar->talking >= talk_silent)
		pick_talk_shape(avatar);
	face_now(avatar);
}

void xsdk_avatar_set_transform(XsdkAvatar *avatar, const float world[16])
{
	memcpy(avatar->world, world, sizeof avatar->world);
}

int xsdk_avatar_locomotion_load(XsdkAvatarLocomotion *locomotion, const XsdkAvatar *avatar)
{
	memset(locomotion, 0, sizeof *locomotion);
	const XsdkAvatarAnimation *stand;
	int result = xsdk_avatar_stock_animation(XSDK_AVATAR_ANIMATION_STAND, &stand);
	if (result == XSDK_AVATAR_OK)
		result = xsdk_avatar_locomotion_init(locomotion, avatar->info.gender, &avatar->skeleton, stand);
	xsdk_log_write("avatar: locomotion: %d, walk %.3f m/s, run %.3f m/s%s", result, (double)locomotion->walk_speed,
		(double)locomotion->run_speed, locomotion->can_jump ? ", jump" : "");
	return result;
}

bool xsdk_avatar_locomotion_drive(XsdkAvatar *avatar, XsdkAvatarLocomotion *locomotion, float dx, float dz, bool run,
	bool jump, float seconds, XsdkAvatarCollide collide, void *context)
{
	const bool jumped = jump && xsdk_avatar_locomotion_jump(locomotion, &avatar->player);
	const bool moved =
		xsdk_avatar_locomotion_update(locomotion, &avatar->player, dx, dz, run, seconds, collide, context);
	xsdk_avatar_placement(avatar->world, locomotion->x, 0.0f, locomotion->z, locomotion->heading);
	return jumped || moved;
}

void xsdk_avatar_draw(const XsdkAvatar *avatar, const float view_projection[16])
{
	float mvp[16];
	avatar_matrix_multiply(mvp, view_projection, avatar->world);
	avatar_render_draw(
		&avatar->drawable, view_projection, mvp, avatar->world, (const float (*)[12])avatar->skin, &avatar->frames);
}

void xsdk_avatar_draw_pose(const XsdkAvatar *avatar, const XsdkAvatarBonePose *pose, const float view_projection[16])
{
	float skin[XSDK_AVATAR_BONES][12];
	skin_of(avatar, pose, skin);
	float mvp[16];
	avatar_matrix_multiply(mvp, view_projection, avatar->world);
	avatar_render_draw(
		&avatar->drawable, view_projection, mvp, avatar->world, (const float (*)[12])skin, &avatar->frames);
}

void xsdk_avatar_draw_shadow(const XsdkAvatar *avatar, const float view_projection[16])
{
	// The world matrix's translation: where the avatar's feet are.
	avatar_render_shadow(view_projection, avatar->world[12], avatar->world[13], avatar->world[14]);
}

const XsdkAvatarSkeleton *xsdk_avatar_skeleton(const XsdkAvatar *avatar)
{
	return &avatar->skeleton;
}

void xsdk_avatar_shutdown(void)
{
	avatar_content_close();
	avatar_render_close();
}
