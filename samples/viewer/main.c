// Avatar viewer: several avatars in one scene through the avatar layer (xsdk/avatar.h): the signed-in profile's,
// NPCs from description files in the Avatars folder beside the title, and two stock ones, side by side, each with
// its own animation. Draws them directly or recorded into a command buffer, and logs what they cost.
//
// D-pad left/right: which avatar the camera looks at. Right stick: turn. Left stick up/down: zoom. Y: it waves,
// B: it claps (once, then back to standing). A: everyone celebrates. X: animations / hand-set pose / bind pose.
// D-pad up/down: its face (expressions, looking around). LB held: it talks. START: blinking on / off. Right stick
// click: 4 / 1 / 2 samples a pixel (anti-aliasing, drawn in bands).
// RB: drawn directly / recorded into a command buffer. Both triggers: screenshot (game:\Screenshots). BACK: exit.
// Log: game:\viewer.log.

#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "screenshot.h"
#include "xsdk/avatar.h"
#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"

enum {
	BUFFERS = 2,
	SWAP_INTERVAL = 1,
	BEGIN_TIMEOUT_MS = 1000,
	FENCE_TIMEOUT_MS = 2000,
	CLEAR_ARGB = 0xFF303848,
	MAX_AVATARS = 8,
	USERS = 4,
	SLOTS = 2,                   // frames in flight: command buffers and clock words, one each
	RECORD_DWORDS = 256 * 1024,  // a command buffer: room for MAX_AVATARS avatars, measured at about 8,000 each
	REPORT_FRAMES = 300,         // a cost report every five seconds at 60 frames a second
};

static const char npc_folder[] = "Avatars";
static const float spacing = 0.9f; // metres between avatars

// What X cycles through.
enum { MODE_ANIMATED, MODE_HAND_SET, MODE_BIND, MODES };
static const char *const mode_names[MODES] = { "animations", "hand-set pose", "bind pose" };

static const XsdkAvatarStockPart male_parts[] = {
	{ "MaleBody", 0xFFDCAD8F },
	{ "Head", 0xFFDCAD8F },
	{ "High and Tight", 0xFF4D331F },
	{ "Punk Jacket and Tee", 0xFF3359BF },
	{ "Black Skinny Fit Jeans", 0xFF26262E },
	{ "Brown Formal Shoes", 0xFF663D1F },
};

static const XsdkAvatarStockPart female_parts[] = {
	{ "FemaleBody", 0xFFDCAD8F },
	{ "Head", 0xFFDCAD8F },
	{ "Low Ponytail", 0xFF8C5226 },
	{ "Hacking Jacket", 0xFF8C3340 },
	{ "Denim Skirt", 0xFF405999 },
};

// Faces d-pad up/down steps through: left eye, right eye, left eyebrow, right eyebrow, mouth. Looking to its own left
// takes the left eye looking out and the right eye looking in.
typedef struct {
	const char *name;
	XsdkAvatarFace face;
} NamedFace;

static const NamedFace faces[] = {
	{ "neutral", { 0, 0, 0, 0, 0 } },
	{ "sad",
		{ XSDK_AVATAR_EYE_SAD, XSDK_AVATAR_EYE_SAD, XSDK_AVATAR_EYEBROW_SAD, XSDK_AVATAR_EYEBROW_SAD,
			XSDK_AVATAR_MOUTH_SAD } },
	{ "angry",
		{ XSDK_AVATAR_EYE_ANGRY, XSDK_AVATAR_EYE_ANGRY, XSDK_AVATAR_EYEBROW_ANGRY, XSDK_AVATAR_EYEBROW_ANGRY,
			XSDK_AVATAR_MOUTH_ANGRY } },
	{ "confused",
		{ XSDK_AVATAR_EYE_CONFUSED, XSDK_AVATAR_EYE_CONFUSED, XSDK_AVATAR_EYEBROW_CONFUSED,
			XSDK_AVATAR_EYEBROW_CONFUSED, XSDK_AVATAR_MOUTH_CONFUSED } },
	{ "laughing",
		{ XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_EYEBROW_RAISED,
			XSDK_AVATAR_MOUTH_LAUGHING } },
	{ "shocked",
		{ XSDK_AVATAR_EYE_SHOCKED, XSDK_AVATAR_EYE_SHOCKED, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_EYEBROW_RAISED,
			XSDK_AVATAR_MOUTH_SHOCKED } },
	{ "happy", { XSDK_AVATAR_EYE_HAPPY, XSDK_AVATAR_EYE_HAPPY, 0, 0, XSDK_AVATAR_MOUTH_HAPPY } },
	{ "yawning", { XSDK_AVATAR_EYE_YAWNING, XSDK_AVATAR_EYE_YAWNING, 0, 0, XSDK_AVATAR_MOUTH_AI } },
	{ "sleeping", { XSDK_AVATAR_EYE_SLEEPING, XSDK_AVATAR_EYE_SLEEPING, 0, 0, 0 } },
	{ "looking to its left", { XSDK_AVATAR_EYE_LOOK_OUT, XSDK_AVATAR_EYE_LOOK_IN, 0, 0, 0 } },
	{ "looking to its right", { XSDK_AVATAR_EYE_LOOK_IN, XSDK_AVATAR_EYE_LOOK_OUT, 0, 0, 0 } },
	{ "looking up", { XSDK_AVATAR_EYE_LOOK_UP, XSDK_AVATAR_EYE_LOOK_UP, 0, 0, 0 } },
	{ "looking down", { XSDK_AVATAR_EYE_LOOK_DOWN, XSDK_AVATAR_EYE_LOOK_DOWN, 0, 0, 0 } },
	{ "one eyebrow raised", { 0, 0, XSDK_AVATAR_EYEBROW_RAISED, 0, 0 } },
};
enum { FACES = sizeof faces / sizeof faces[0] };

typedef struct {
	XsdkAvatar *avatar;
	char title[40];
	uint32_t face;
	float x;
	float height;
} SceneAvatar;

static SceneAvatar avatars[MAX_AVATARS];
static uint32_t avatar_count;

static double seconds_now(void)
{
	struct timespec now;
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Logs what a load gave; keeps the avatar if it loaded.
static void add_avatar(int result, XsdkAvatar *avatar, const char *title, double load_seconds)
{
	if (result != XSDK_AVATAR_OK) {
		xsdk_log_write("viewer: %s: not loaded (%d)", title, result);
		return;
	}
	XsdkAvatarInfo info;
	xsdk_avatar_info(avatar, &info);
	xsdk_log_write("viewer: %s: %u parts, %u draws, %u items replaced, %u left out, %u KB GPU, %u KB CPU, %.1f ms",
		title, (unsigned)info.part_count, (unsigned)info.draw_count, (unsigned)info.replaced_count,
		(unsigned)info.left_out_count, (unsigned)(info.gpu_bytes / 1024), (unsigned)(info.cpu_bytes / 1024),
		load_seconds * 1000.0);
	SceneAvatar *s = &avatars[avatar_count++];
	s->avatar = avatar;
	snprintf(s->title, sizeof s->title, "%s", title);
	s->height = info.height;
}

static void load_profile(void)
{
	for (uint32_t user = 0; user < USERS; user++) {
		XsdkAvatar *avatar;
		const double start = seconds_now();
		const int result = xsdk_avatar_load_profile(&avatar, user);
		if (result == XSDK_AVATAR_ERROR_NOT_FOUND)
			continue;
		add_avatar(result, avatar, "your avatar", seconds_now() - start);
		return;
	}
	xsdk_log_write("viewer: no signed-in profile with an avatar");
}

// NPCs: every .bin file of XSDK_AVATAR_DESCRIPTION_BYTES in the Avatars folder (saved on a console, or exported
// by a web editor such as 360sona).
static void load_npcs(void)
{
	DIR *dir = opendir(npc_folder);
	if (!dir) {
		xsdk_log_write("viewer: no %s folder: no NPCs", npc_folder);
		return;
	}
	const struct dirent *entry;
	while ((entry = readdir(dir)) && avatar_count < MAX_AVATARS - 2) {
		const size_t length = strlen(entry->d_name);
		if (length < 4 || strcasecmp(entry->d_name + length - 4, ".bin") != 0)
			continue;
		char path[300];
		snprintf(path, sizeof path, "%s/%s", npc_folder, entry->d_name);
		static uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES];
		FILE *file = fopen(path, "rb");
		const size_t read = file ? fread(bytes, 1, sizeof bytes, file) : 0;
		if (file)
			(void)fclose(file);
		XsdkAvatar *avatar;
		const double start = seconds_now();
		const int result = xsdk_avatar_load_description(&avatar, bytes, (uint32_t)read);
		add_avatar(result, avatar, entry->d_name, seconds_now() - start);
	}
	(void)closedir(dir);
}

static void load_stock(const char *title, const XsdkAvatarStockPart *parts, uint32_t count)
{
	XsdkAvatar *avatar;
	const double start = seconds_now();
	const int result = xsdk_avatar_load_stock(&avatar, parts, count);
	add_avatar(result, avatar, title, seconds_now() - start);
}

// Side by side along x, facing the camera, each standing in its own rhythm.
static void place_avatars(void)
{
	for (uint32_t a = 0; a < avatar_count; a++) {
		avatars[a].x = ((float)a - 0.5f * (float)(avatar_count - 1)) * spacing;
		float world[16];
		xsdk_avatar_placement(world, avatars[a].x, 0.0f, 0.0f, 0.0f);
		xsdk_avatar_set_transform(avatars[a].avatar, world);
		(void)xsdk_avatar_play(avatars[a].avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
		xsdk_avatar_update(avatars[a].avatar, 0.7f * (float)a);
	}
}

// A gesture once, then back to standing.
static void gesture(XsdkAvatar *avatar, const char *animation)
{
	(void)xsdk_avatar_play(avatar, animation, XSDK_AVATAR_ONCE);
	(void)xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
}

// The camera orbiting target at yaw, distance metres away: a column-major view-projection with depth 0 to 1.
static void camera(float vp[16], const float target[3], float yaw, float distance, float aspect)
{
	const float eye[3] = { target[0] + distance * sinf(yaw), target[1], target[2] + distance * cosf(yaw) };
	xsdk_avatar_camera(vp, eye, target, 0.6f, aspect, 0.1f, 30.0f); // 34 degrees vertical field of view
}

// a times b: the rotation b, then a.
static void quaternion_multiply(float out[4], const float a[4], const float b[4])
{
	out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

// A rotation about a world axis (the avatar's bones keep the world axes: x to its left, y up, z forward), then
// optionally a second one before it.
static void turn(XsdkAvatarBonePose *pose, uint32_t bone, char axis, float angle, char first_axis, float first_angle)
{
	float a[4], b[4];
	xsdk_avatar_quaternion_from_axis(a, axis == 'x', axis == 'y', axis == 'z', angle);
	if (!first_axis) {
		memcpy(pose[bone].rotation, a, sizeof a);
		return;
	}
	xsdk_avatar_quaternion_from_axis(b, first_axis == 'x', first_axis == 'y', first_axis == 'z', first_angle);
	quaternion_multiply(pose[bone].rotation, a, b);
}

// The hand-set pose of milestone 6, posed by the title (xsdk_avatar_draw_pose): the left arm down with the fingers
// curled, the right arm raised, its forearm up and waving (seconds), the head turned and tipped, the left leg lifted.
static void hand_set_pose(XsdkAvatarBonePose *pose, float seconds)
{
	xsdk_avatar_pose_reset(pose, XSDK_AVATAR_BONES);
	turn(pose, XSDK_AVATAR_BONE_SHOULDER_LEFT, 'z', -1.25f, 0, 0.0f);
	turn(pose, XSDK_AVATAR_BONE_ELBOW_LEFT, 'y', -0.4f, 0, 0.0f);
	static const uint8_t fingers_left[] = { 37, 38, 39, 40, 51, 52, 53, 54, 61, 62, 63, 64 };
	for (uint32_t k = 0; k < sizeof fingers_left; k++)
		turn(pose, fingers_left[k], 'z', -0.5f, 0, 0.0f);
	// The upper arm twisted a quarter turn, so that the elbow's hinge lifts the forearm, then raised a little.
	turn(pose, XSDK_AVATAR_BONE_SHOULDER_RIGHT, 'z', -0.3f, 'x', -1.5708f);
	turn(pose, XSDK_AVATAR_BONE_ELBOW_RIGHT, 'y', 1.3f + 0.35f * sinf(5.0f * seconds), 0, 0.0f);
	turn(pose, XSDK_AVATAR_BONE_HEAD, 'y', 0.4f, 'x', 0.15f);
	turn(pose, XSDK_AVATAR_BONE_HIP_LEFT, 'x', -0.45f, 0, 0.0f);
	turn(pose, XSDK_AVATAR_BONE_KNEE_LEFT, 'x', 0.8f, 0, 0.0f);
}

// Every avatar's draws, in the mode's pose.
static void draw_avatars(const float vp[16], uint32_t mode, float seconds)
{
	static XsdkAvatarBonePose pose[XSDK_AVATAR_BONES];
	if (mode == MODE_HAND_SET)
		hand_set_pose(pose, seconds);
	else if (mode == MODE_BIND)
		xsdk_avatar_pose_reset(pose, XSDK_AVATAR_BONES);
	for (uint32_t a = 0; a < avatar_count; a++) {
		if (mode == MODE_ANIMATED)
			xsdk_avatar_draw(avatars[a].avatar, vp);
		else
			xsdk_avatar_draw_pose(avatars[a].avatar, pose, vp);
	}
}

// The rows of the screen one band takes at samples a pixel: as many whole 32-row strips (a resolve's unit) as eDRAM
// holds with colour and depth. 720p takes one band at 1 sample, two at 2, three at 4.
static uint32_t band_rows(uint32_t width, uint32_t height, uint32_t samples)
{
	uint32_t rows = (height + 31) / 32 * 32;
	while (rows > 32 && 2 * xsdk_gpu_target_sample_tiles(width, rows, samples) > XSDK_EDRAM_TILES)
		rows -= 32;
	return rows;
}

// One frame's drawing.
typedef struct {
	uint32_t width, height, samples;
	const XsdkDepthStencilState *depth;
	uint32_t address;                   // the front buffer
	const XsdkTexture *shot;            // or NULL: this frame isn't saved
	const XsdkCommandBuffer *recording; // or NULL: draw directly
	const float *vp;
	uint32_t mode;
	float seconds;
} Frame;

// The frame in bands: each a target of the samples, its viewport moved up by the rows above it so that the camera's
// projection stays the whole screen's, the avatars drawn (or their recording run again), and the band resolved,
// its samples averaged, into its rows of the screen and of the screenshot. Returns the number of bands.
static uint32_t draw_bands(const Frame *f)
{
	const uint32_t rows = band_rows(f->width, f->height, f->samples);
	// Every band's colour after a whole band's depth, so that the last, shorter band's colour never lies where the
	// others keep depth (xsdk's own layout follows each target's size; the store's test run 6).
	const uint32_t color_tile = xsdk_gpu_target_sample_tiles(f->width, rows, f->samples);
	uint32_t bands = 0;
	for (uint32_t top = 0; top < f->height; top += rows, bands++) {
		const uint32_t band = f->height - top < rows ? f->height - top : rows;
		xsdk_gpu_set_target_samples_at(f->width, band, f->samples, color_tile, 0);
		xsdk_gpu_set_viewport(0.0f, -(float)top, (float)f->width, (float)f->height, 0.0f, 1.0f);
		xsdk_gpu_set_depth_stencil(f->depth);
		if (f->recording)
			xsdk_gpu_execute(f->recording);
		else
			draw_avatars(f->vp, f->mode, f->seconds);
		if (f->shot) {
			// Copied into the screenshot first without clearing, then into the screen.
			xsdk_gpu_set_resolve_clears(false, false);
			xsdk_gpu_resolve(f->shot->address + top * f->shot->pitch * 4, f->width, band, CLEAR_ARGB);
			xsdk_gpu_set_resolve_clears(true, true);
		}
		xsdk_gpu_resolve(f->address + top * f->width * 4, f->width, band, CLEAR_ARGB);
	}
	return bands;
}

static float stick(int16_t value)
{
	const float v = (float)value / 32768.0f;
	return fabsf(v) < 0.2f ? 0.0f : v;
}

// The cost of drawing the avatars, summed over a report's frames.
typedef struct {
	uint32_t frames;
	double cpu_seconds;  // update and draw (or record) calls
	uint64_t gpu_ticks;  // between the clock words around the frame's bands: their draws and resolves
	uint32_t gpu_frames; // frames whose clock words came back
	uint64_t dwords;     // command stream, all of it
	uint32_t recorded_dwords;
	uint32_t bands;
} Costs;

static void report(Costs *costs, bool recorded, uint32_t samples)
{
	if (!costs->frames)
		return;
	const double per_frame_us = costs->cpu_seconds * 1e6 / costs->frames;
	// The GPU clock runs at 498.75 a microsecond (xsdk/gpu.h).
	const double gpu_us = costs->gpu_frames ? (double)costs->gpu_ticks / 498.75 / costs->gpu_frames : 0.0;
	xsdk_log_write("viewer: %u avatars %s, %u samples in %u bands: CPU %.0f us a frame (%.0f each), GPU %.0f us "
				   "(draws and resolves), %u dwords a frame, %u recorded",
		(unsigned)avatar_count, recorded ? "recorded" : "direct", (unsigned)samples, (unsigned)costs->bands,
		per_frame_us, per_frame_us / avatar_count, gpu_us, (unsigned)(costs->dwords / costs->frames),
		(unsigned)costs->recorded_dwords);
	memset(costs, 0, sizeof *costs);
}

int main(void)
{
	uint32_t width = 0, height = 0;
	NTSTATUS status = xsdk_gpu_open();
	if (NT_SUCCESS(status))
		status = xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, SWAP_INTERVAL, &width, &height);
	if (!NT_SUCCESS(status)) {
		xsdk_log_write("viewer: GPU setup failed: 0x%08X", (unsigned)status);
		return 1;
	}
	load_profile();
	load_npcs();
	load_stock("stock male", male_parts, sizeof male_parts / sizeof male_parts[0]);
	load_stock("stock female", female_parts, sizeof female_parts / sizeof female_parts[0]);
	if (!avatar_count)
		return 1;
	place_avatars();
	xsdk_log_write("viewer: %u stock animations, display %ux%u", (unsigned)xsdk_avatar_stock_animation_count(),
		(unsigned)width, (unsigned)height);

	static XsdkTexture shot;
	const bool can_shoot = screenshot_init(&shot, width, height);
	static XsdkCommandBuffer buffers[SLOTS];
	bool can_record = true;
	for (uint32_t b = 0; b < SLOTS; b++)
		can_record = NT_SUCCESS(xsdk_gpu_command_buffer_create(&buffers[b], RECORD_DWORDS)) && can_record;
	// Per slot: the GPU clock before and after the avatars' draws, and the fence after them.
	volatile uint32_t *clock_words = xsdk_gpu_alloc(SLOTS * 2 * XSDK_GPU_CLOCK_WORDS * 4);
	uint32_t slot_fences[SLOTS] = { 0 };
	bool slot_timed[SLOTS] = { false };

	const XsdkDepthStencil depth_setting = {
		.depth_test = true, .depth_write = true, .depth_compare = XSDK_COMPARE_LESS_EQUAL
	};
	XsdkDepthStencilState depth;
	xsdk_gpu_depth_stencil_state(&depth, &depth_setting);
	xsdk_gpu_set_depth_clear(1.0f, 0);

	uint32_t which = 0, mode = MODE_ANIMATED;
	bool recorded = false, blinking = true;
	uint32_t samples = 4;
	float yaw = 0.0f, distance = 4.2f, seconds = 0.0f;
	uint16_t previous_buttons = 0;
	bool triggers_held = false;
	double last_time = seconds_now();
	Costs costs = { 0 };
	XsdkGpuStats stats;
	xsdk_gpu_stats(&stats);
	uint64_t last_dwords = stats.dwords;
	for (uint32_t frame = 0;; frame++) {
		INPUT_STATE input;
		uint16_t buttons = 0;
		bool triggers = false;
		if (XamInputGetState(0, 0, &input) == ERROR_SUCCESS) {
			buttons = input.gamepad.buttons;
			triggers = input.gamepad.left_trigger > 128 && input.gamepad.right_trigger > 128;
			yaw += 0.05f * stick(input.gamepad.thumb_rx);
			distance -= 0.05f * stick(input.gamepad.thumb_ly);
			distance = distance < 1.0f ? 1.0f : distance > 12.0f ? 12.0f : distance;
		}
		const uint16_t pressed = buttons & (uint16_t)~previous_buttons;
		previous_buttons = buttons;
		if (buttons & INPUT_GAMEPAD_BACK)
			break;
		if (pressed & INPUT_GAMEPAD_DPAD_RIGHT)
			which = (which + 1) % avatar_count;
		if (pressed & INPUT_GAMEPAD_DPAD_LEFT)
			which = (which + avatar_count - 1) % avatar_count;
		if (pressed & (INPUT_GAMEPAD_DPAD_LEFT | INPUT_GAMEPAD_DPAD_RIGHT))
			xsdk_log_write("viewer: looking at %s", avatars[which].title);
		if (pressed & INPUT_GAMEPAD_Y)
			gesture(avatars[which].avatar, XSDK_AVATAR_ANIMATION_WAVE);
		if (pressed & INPUT_GAMEPAD_B)
			gesture(avatars[which].avatar, XSDK_AVATAR_ANIMATION_CLAP);
		if (pressed & INPUT_GAMEPAD_A)
			for (uint32_t a = 0; a < avatar_count; a++)
				gesture(avatars[a].avatar, XSDK_AVATAR_ANIMATION_CELEBRATE);
		if (pressed & INPUT_GAMEPAD_X) {
			mode = (mode + 1) % MODES;
			xsdk_log_write("viewer: %s", mode_names[mode]);
		}
		if (pressed & (INPUT_GAMEPAD_DPAD_UP | INPUT_GAMEPAD_DPAD_DOWN)) {
			SceneAvatar *s = &avatars[which];
			s->face = (s->face + (pressed & INPUT_GAMEPAD_DPAD_DOWN ? 1 : FACES - 1)) % FACES;
			xsdk_avatar_set_face(s->avatar, &faces[s->face].face);
			xsdk_log_write("viewer: %s: %s", s->title, faces[s->face].name);
		}
		if (pressed & INPUT_GAMEPAD_START) {
			blinking = !blinking;
			for (uint32_t a = 0; a < avatar_count; a++)
				xsdk_avatar_set_blinking(avatars[a].avatar, blinking);
			xsdk_log_write("viewer: blinking %s", blinking ? "on" : "off");
		}
		// LB held: the avatar looked at talks, its level rising and falling like syllables.
		const bool talking = (buttons & INPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		for (uint32_t a = 0; a < avatar_count; a++)
			xsdk_avatar_set_talking(
				avatars[a].avatar, talking && a == which ? 0.35f + 0.65f * fabsf(sinf(7.0f * seconds)) : 0.0f);
		if (pressed & INPUT_GAMEPAD_RIGHT_THUMB) {
			report(&costs, recorded, samples);
			samples = samples == 4 ? 1 : samples * 2;
			xsdk_log_write("viewer: %u samples a pixel", (unsigned)samples);
		}
		if ((pressed & INPUT_GAMEPAD_RIGHT_SHOULDER) && can_record) {
			report(&costs, recorded, samples);
			recorded = !recorded;
			xsdk_log_write("viewer: drawing %s", recorded ? "recorded into a command buffer" : "directly");
		}

		// The time since the last frame, within reason.
		const double now = seconds_now();
		float dt = (float)(now - last_time);
		last_time = now;
		if (dt < 0.0f || dt > 0.1f)
			dt = 1.0f / 60.0f;
		seconds += dt;

		float vp[16];
		const float target[3] = { avatars[which].x, avatars[which].height * 0.52f, 0.0f };
		camera(vp, target, yaw, distance, (float)width / (float)height);

		// The avatars' updates and draws, timed on the CPU: recorded ahead of the frame, or drawn inside it below.
		const uint32_t slot = frame % SLOTS;
		if (slot_fences[slot] && !NT_SUCCESS(xsdk_gpu_wait(slot_fences[slot], FENCE_TIMEOUT_MS)))
			xsdk_log_write("viewer: frame %u: slot %u's fence didn't land", (unsigned)frame, (unsigned)slot);
		if (slot_timed[slot]) {
			const volatile uint32_t *words = clock_words + slot * 2 * XSDK_GPU_CLOCK_WORDS;
			costs.gpu_ticks += xsdk_gpu_clock_value(words + XSDK_GPU_CLOCK_WORDS) - xsdk_gpu_clock_value(words);
			costs.gpu_frames++;
			slot_timed[slot] = false;
		}
		const double cpu_start = seconds_now();
		for (uint32_t a = 0; a < avatar_count; a++)
			xsdk_avatar_update(avatars[a].avatar, dt);
		bool use_recording = false;
		if (recorded) {
			xsdk_gpu_record_begin(&buffers[slot]);
			draw_avatars(vp, mode, seconds);
			use_recording = NT_SUCCESS(xsdk_gpu_record_end());
			if (!use_recording)
				xsdk_log_write("viewer: frame %u: the recording outgrew its buffer", (unsigned)frame);
			costs.recorded_dwords = buffers[slot].count;
		}
		double cpu_seconds = seconds_now() - cpu_start;
		// Both triggers pulled: this frame also goes into the screenshot texture, as Avatar Thrift Store's shortcut.
		const bool shoot = can_shoot && triggers && !triggers_held;
		triggers_held = triggers;

		uint32_t address;
		if (!NT_SUCCESS(xsdk_present_begin(&address, BEGIN_TIMEOUT_MS))) {
			xsdk_log_write("viewer: frame %u: no front buffer came free", (unsigned)frame);
			break;
		}
		volatile uint32_t *words = clock_words + slot * 2 * XSDK_GPU_CLOCK_WORDS;
		xsdk_gpu_write_clock(xsdk_gpu_address(words), true);
		const double draw_start = seconds_now();
		const Frame drawn = { .width = width,
			.height = height,
			.samples = samples,
			.depth = &depth,
			.address = address,
			.shot = shoot ? &shot : NULL,
			.recording = use_recording ? &buffers[slot] : NULL,
			.vp = vp,
			.mode = mode,
			.seconds = seconds };
		costs.bands = draw_bands(&drawn);
		cpu_seconds += seconds_now() - draw_start;
		xsdk_gpu_write_clock(xsdk_gpu_address(words + XSDK_GPU_CLOCK_WORDS), true);
		xsdk_present_end();
		slot_fences[slot] = xsdk_gpu_fence();
		slot_timed[slot] = !shoot;
		if (shoot) {
			char path[96];
			if (screenshot_save(&shot, path, sizeof path))
				xsdk_log_write("viewer: screenshot %s", path);
			else
				xsdk_log_write("viewer: screenshot failed");
		}

		costs.frames++;
		costs.cpu_seconds += cpu_seconds;
		xsdk_gpu_stats(&stats);
		costs.dwords += stats.dwords - last_dwords;
		last_dwords = stats.dwords;
		if (costs.frames == REPORT_FRAMES)
			report(&costs, use_recording, samples);
		if (frame == 0)
			xsdk_log_write("viewer: first frame queued");
	}
	xsdk_present_close();
	(void)xsdk_gpu_wait(xsdk_gpu_fence(), FENCE_TIMEOUT_MS);
	for (uint32_t a = 0; a < avatar_count; a++)
		xsdk_avatar_close(avatars[a].avatar);
	xsdk_avatar_shutdown();
	for (uint32_t b = 0; b < SLOTS; b++)
		xsdk_gpu_command_buffer_free(&buffers[b]);
	xsdk_gpu_free(clock_words);
	xsdk_log_write("viewer: done, GPU close 0x%08X", (unsigned)xsdk_gpu_close());
	return 0;
}
