// Store: walk your avatar round the Avatar Thrift Store among NPCs, and emote from a wheel. The walk is the library's
// (xsdk_avatar_animation_load_walk), and so are the run and the jump (_load_run, _load_jump); the store (scene.c), its
// NPCs (npcs.c) and the wheel (wheel.c) are this sample's.
//
// Left stick: walk (run while the right trigger is held). A: jump. Right stick: turn the camera. LB held: the emote
// wheel; right stick picks, letting go of LB plays it. Right stick click: 2 / 4 / 1 samples a pixel. Both triggers:
// screenshot. BACK: exit. Log: game:\store.log.

#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "mesh.h"
#include "npcs.h"
#include "scene.h"
#include "screenshot.h"
#include "wheel.h"
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
	CLEAR_ARGB = 0xFF101014,
	USERS = 4,
	REPORT_FRAMES = 300,
};

static const float pi = 3.14159265f;
static const float turn_rate = 9.0f, avatar_radius = 0.28f, shadow_radius = 0.38f;
// The stick pushed all the way: walking, and running while the right trigger is held (metres a second).
static const float walk_top_speed = 1.4f, run_top_speed = 2.6f;
static const float camera_distance = 3.0f, camera_height = 1.0f, fade_seconds = 0.25f;
static const float talk_seconds = 3.5f, answer_reach = 2.5f;

static const XsdkAvatarStockPart stock_parts[] = { { "MaleBody", 0xFFDCAD8F }, { "Head", 0xFFDCAD8F },
	{ "High and Tight", 0xFF4D331F }, { "Punk Jacket and Tee", 0xFF3359BF }, { "Black Skinny Fit Jeans", 0xFF26262E },
	{ "Brown Formal Shoes", 0xFF663D1F }, { "Happy Eyes", 0xFF4A3020 }, { "Studious Eyebrows", 0xFF4D331F },
	{ "Smirk Mouth", 0xFFB56157 } };

// The wheel's emotes: an animation (male, female; NULL keeps standing), the face while it plays, and whether it talks.
typedef struct {
	const char *name, *male, *female;
	XsdkAvatarFace face;
	bool talks;
} Emote;

static const Emote emotes[] = {
	{ "WAVE", XSDK_AVATAR_ANIMATION_WAVE, XSDK_AVATAR_ANIMATION_WAVE, { XSDK_AVATAR_EYE_HAPPY, XSDK_AVATAR_EYE_HAPPY, 0, 0, XSDK_AVATAR_MOUTH_HAPPY }, false },
	{ "CLAP", XSDK_AVATAR_ANIMATION_CLAP, XSDK_AVATAR_ANIMATION_CLAP, { XSDK_AVATAR_EYE_HAPPY, XSDK_AVATAR_EYE_HAPPY, 0, 0, XSDK_AVATAR_MOUTH_HAPPY }, false },
	{ "CHEER", XSDK_AVATAR_ANIMATION_CELEBRATE, XSDK_AVATAR_ANIMATION_CELEBRATE,
		{ XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_MOUTH_LAUGHING }, false },
	{ "LAUGH", "Animation Male Laugh", "Animation Female Laugh",
		{ XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYE_LAUGHING, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_MOUTH_LAUGHING }, false },
	{ "TALK", NULL, NULL, { 0, 0, 0, 0, 0 }, true },
	{ "ANGRY", "Animation Male Angry", "Animation Female Angry", { XSDK_AVATAR_EYE_ANGRY, XSDK_AVATAR_EYE_ANGRY, XSDK_AVATAR_EYEBROW_ANGRY, XSDK_AVATAR_EYEBROW_ANGRY, XSDK_AVATAR_MOUTH_ANGRY },
		false },
	{ "SAD", "Animation Male Sad Cry", "Animation Female Sad Cry", { XSDK_AVATAR_EYE_SAD, XSDK_AVATAR_EYE_SAD, XSDK_AVATAR_EYEBROW_SAD, XSDK_AVATAR_EYEBROW_SAD, XSDK_AVATAR_MOUTH_SAD }, false },
	{ "CONFUSED", "Animation Male Confused", "Animation Female Confused",
		{ XSDK_AVATAR_EYE_CONFUSED, XSDK_AVATAR_EYE_CONFUSED, XSDK_AVATAR_EYEBROW_CONFUSED, XSDK_AVATAR_EYEBROW_CONFUSED, XSDK_AVATAR_MOUTH_CONFUSED }, false },
	{ "SHOCKED", "Animation Male Shocked Surprised", "Animation Female Shocked Surprised",
		{ XSDK_AVATAR_EYE_SHOCKED, XSDK_AVATAR_EYE_SHOCKED, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_EYEBROW_RAISED, XSDK_AVATAR_MOUTH_SHOCKED }, false },
	{ "YAWN", "Animation Male Yawn", "Animation Female Yawn", { XSDK_AVATAR_EYE_YAWNING, XSDK_AVATAR_EYE_YAWNING, 0, 0, XSDK_AVATAR_MOUTH_AI }, false },
};
enum { EMOTES = sizeof emotes / sizeof emotes[0] };
static const XsdkAvatarFace neutral = { 0, 0, 0, 0, 0 };

// Animations of the player's own (made in Blender, converted by tools/anim_convert.py): every .xaan file in the
// Animations folder beside the title, up to MAX_CUSTOM, on the wheel after the stock emotes, named after their files.
enum {
	MAX_CUSTOM = 4,
	NAME_LETTERS = 9,
};
static const char custom_folder[] = "Animations";
static XsdkAvatarAnimation customs[MAX_CUSTOM];
static char custom_names[MAX_CUSTOM][NAME_LETTERS + 1];
static uint32_t custom_count;

// A file's name as the wheel's font can write it: capital letters, 0 and spaces, NAME_LETTERS at most.
static void wheel_name(char *out, const char *file)
{
	uint32_t n = 0;
	for (const char *c = file; *c && *c != '.' && n < NAME_LETTERS; c++) {
		char ch = *c >= 'a' && *c <= 'z' ? (char)(*c - 'a' + 'A') : *c;
		out[n++] = (ch >= 'A' && ch <= 'Z') || ch == '0' ? ch : ' ';
	}
	out[n] = '\0';
}

static void load_customs(const XsdkAvatar *avatar)
{
	DIR *dir = opendir(custom_folder);
	if (!dir) {
		xsdk_log_write("store: no %s folder: no animations of your own", custom_folder);
		return;
	}
	const struct dirent *entry;
	while ((entry = readdir(dir)) && custom_count < MAX_CUSTOM) {
		const size_t length = strlen(entry->d_name);
		if (length < 5 || strcasecmp(entry->d_name + length - 5, ".xaan") != 0)
			continue;
		char path[300];
		snprintf(path, sizeof path, "%s/%s", custom_folder, entry->d_name);
		FILE *file = fopen(path, "rb");
		if (!file)
			continue;
		(void)fseek(file, 0, SEEK_END);
		const long size = ftell(file);
		(void)fseek(file, 0, SEEK_SET);
		void *bytes = size > 0 ? malloc((size_t)size) : NULL;
		const bool read = bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size;
		(void)fclose(file);
		// Made in Blender on the rig: its root turns about the rig's pivot, moved onto this avatar's (SPECS.md, 4.8).
		const int result = read ? xsdk_avatar_animation_load(&customs[custom_count], bytes, (uint32_t)size,
									  xsdk_avatar_skeleton(avatar))
								: XSDK_AVATAR_ERROR_READ;
		free(bytes);
		xsdk_log_write("store: %s: %d, %u frames, %.2f s", path, result, (unsigned)customs[custom_count].frame_count,
			(double)xsdk_avatar_animation_length(&customs[custom_count]));
		if (result == XSDK_AVATAR_OK)
			wheel_name(custom_names[custom_count++], entry->d_name);
	}
	(void)closedir(dir);
}

enum { STANDING, WALKING, RUNNING, EMOTING, JUMPING };

static double seconds_now(void)
{
	struct timespec now;
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static float stick(int16_t value)
{
	const float v = (float)value / 32768.0f;
	return fabsf(v) < 0.2f ? 0.0f : (v - (v > 0 ? 0.2f : -0.2f)) / 0.8f;
}

static XsdkAvatar *load_avatar(void)
{
	XsdkAvatar *avatar;
	for (uint32_t user = 0; user < USERS; user++)
		if (xsdk_avatar_load_profile(&avatar, user) == XSDK_AVATAR_OK)
			return avatar;
	return xsdk_avatar_load_stock(&avatar, stock_parts, sizeof stock_parts / sizeof stock_parts[0]) == XSDK_AVATAR_OK
		? avatar
		: NULL;
}

// The library's own walk: the clip for the avatar's body, its root moved onto the avatar's skeleton; speed gets how
// fast its steps go, for the pace.
static bool make_walk(XsdkAvatar *avatar, XsdkAvatarAnimation *walk, float *speed)
{
	XsdkAvatarInfo info;
	xsdk_avatar_info(avatar, &info);
	const int result = xsdk_avatar_animation_load_walk(walk, info.gender, xsdk_avatar_skeleton(avatar), speed);
	xsdk_log_write("store: walk for the %s body: %d, %.3f m/s", info.gender == XSDK_AVATAR_FEMALE ? "female" : "male",
		result, (double)*speed);
	return result == XSDK_AVATAR_OK && *speed > 0.0f;
}

// A camera at eye looking at target: column-major view-projection, depth 0 to 1, and its right, up, forward axes.
static void look(float vp[16], float axes[3][3], const float eye[3], const float target[3], float aspect)
{
	const float focal = 1.0f / tanf(0.5f * 0.95f), near = 0.05f, far = 40.0f, k = far / (far - near);
	float f[3] = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
	const float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	for (uint32_t i = 0; i < 3; i++)
		f[i] /= fl;
	float r[3] = { -f[2], 0.0f, f[0] };
	const float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
	r[0] /= rl;
	r[2] /= rl;
	const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
	const float *rows[4] = { r, u, f, f };
	const float scale[4] = { focal / aspect, focal, k, 1.0f };
	for (uint32_t row = 0; row < 4; row++) {
		const float *a = rows[row];
		for (uint32_t col = 0; col < 3; col++)
			vp[4 * col + row] = a[col] * scale[row];
		const float along = -(a[0] * eye[0] + a[1] * eye[1] + a[2] * eye[2]);
		vp[12 + row] = row == 2 ? (along - near) * k : along * scale[row];
	}
	memcpy(axes[0], r, sizeof r);
	memcpy(axes[1], u, sizeof u);
	memcpy(axes[2], f, sizeof f);
}

static void light_constants(float out[MESH_LIGHT_VECTORS][4], const XsdkAvatarLight *light, const float axes[3][3])
{
	memset(out, 0, sizeof(float) * 4 * MESH_LIGHT_VECTORS);
	const float *d = light->direction;
	const float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	for (uint32_t k = 0; k < 3; k++) {
		out[0][k] = d[k] / length;
		out[1][k] = light->color[k];
		out[2][k] = light->sky[k];
		out[3][k] = light->ground[k];
		out[4][k] = axes[0][k];
		out[5][k] = axes[1][k];
		out[6][k] = axes[2][k];
	}
	out[0][3] = light->wrap;
	out[1][3] = light->highlight;
}

static uint32_t band_rows(uint32_t width, uint32_t height, uint32_t samples)
{
	uint32_t rows = (height + 31) / 32 * 32;
	while (rows > 32 && 2 * xsdk_gpu_target_sample_tiles(width, rows, samples) > XSDK_EDRAM_TILES)
		rows -= 32;
	return rows;
}

static float turn_towards(float heading, float target, float step)
{
	float d = target - heading;
	while (d > pi)
		d -= 2.0f * pi;
	while (d < -pi)
		d += 2.0f * pi;
	return heading + (fabsf(d) <= step ? d : (d > 0 ? step : -step));
}

int main(void)
{
	uint32_t width = 0, height = 0;
	NTSTATUS status = xsdk_gpu_open();
	if (NT_SUCCESS(status))
		status = xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, SWAP_INTERVAL, &width, &height);
	if (!NT_SUCCESS(status) || !mesh_open()) {
		xsdk_log_write("store: GPU setup failed: 0x%08X", (unsigned)status);
		return 1;
	}
	// A warm light from above the front, as under the store's spots; the avatars and the store share it.
	const XsdkAvatarLight light = { .direction = { 0.25f, 0.9f, 0.45f }, .color = { 0.85f, 0.78f, 0.66f },
		.sky = { 0.42f, 0.40f, 0.38f }, .ground = { 0.24f, 0.19f, 0.15f }, .wrap = 0.4f, .highlight = 0.0f };
	xsdk_avatar_set_light(&light);
	XsdkAvatar *avatar = load_avatar();
	static XsdkAvatarAnimation walk;
	float walk_speed = 1.0f;
	if (!avatar || !make_walk(avatar, &walk, &walk_speed))
		return 1;
	XsdkAvatarInfo info;
	xsdk_avatar_info(avatar, &info);
	static XsdkAvatarAnimation jump;
	const int jump_result = xsdk_avatar_animation_load_jump(&jump, info.gender, xsdk_avatar_skeleton(avatar));
	const bool can_jump = jump_result == XSDK_AVATAR_OK;
	static XsdkAvatarAnimation run;
	float run_speed = 0.0f;
	const int run_result = xsdk_avatar_animation_load_run(&run, info.gender, xsdk_avatar_skeleton(avatar), &run_speed);
	const bool can_run = run_result == XSDK_AVATAR_OK && run_speed > 0.0f;
	xsdk_log_write("store: run for the %s body: %d, %.3f m/s", info.gender == XSDK_AVATAR_FEMALE ? "female" : "male",
		run_result, (double)run_speed);
	xsdk_log_write("store: jump for the %s body: %d", info.gender == XSDK_AVATAR_FEMALE ? "female" : "male", jump_result);
	xsdk_log_write("store: %u NPCs", (unsigned)npcs_load());
	scene_build();
	load_customs(avatar);
	const char *names[EMOTES + MAX_CUSTOM];
	for (uint32_t i = 0; i < EMOTES; i++)
		names[i] = emotes[i].name;
	for (uint32_t i = 0; i < custom_count; i++)
		names[EMOTES + i] = custom_names[i];
	wheel_build(width, height, names, EMOTES + custom_count);
	if (!mesh_upload())
		return 1;
	XsdkAvatarPlayer *player = xsdk_avatar_player(avatar);
	player->fade_seconds = fade_seconds;
	(void)xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
	static XsdkTexture shot;
	const bool can_shoot = screenshot_init(&shot, width, height);
	xsdk_gpu_set_depth_clear(1.0f, 0);

	float x = 0.0f, z = 1.0f, heading = pi, camera_yaw = 0.0f, camera_pitch = 0.4f, talk_left = 0.0f, clock = 0.0f;
	uint32_t state = STANDING, samples = 2, frames = 0;
	int picked = -1;
	uint16_t previous_buttons = 0;
	bool triggers_held = false;
	double last_time = seconds_now(), cpu_seconds = 0.0;
	for (uint32_t frame = 0;; frame++) {
		INPUT_STATE input;
		uint16_t buttons = 0;
		float lx = 0, ly = 0, rx = 0, ry = 0;
		bool triggers = false, run_held = false;
		if (XamInputGetState(0, 0, &input) == ERROR_SUCCESS) {
			buttons = input.gamepad.buttons;
			triggers = input.gamepad.left_trigger > 128 && input.gamepad.right_trigger > 128;
			run_held = input.gamepad.right_trigger > 64;
			lx = stick(input.gamepad.thumb_lx);
			ly = stick(input.gamepad.thumb_ly);
			rx = stick(input.gamepad.thumb_rx);
			ry = stick(input.gamepad.thumb_ry);
		}
		const uint16_t pressed = buttons & (uint16_t)~previous_buttons;
		const uint16_t released = previous_buttons & (uint16_t)~buttons;
		previous_buttons = buttons;
		if (buttons & INPUT_GAMEPAD_BACK)
			break;
		if (pressed & INPUT_GAMEPAD_RIGHT_THUMB) {
			samples = samples == 4 ? 1 : samples * 2;
			xsdk_log_write("store: %u samples a pixel", (unsigned)samples);
		}
		const double now = seconds_now();
		float dt = (float)(now - last_time);
		last_time = now;
		if (dt < 0.0f || dt > 0.1f)
			dt = 1.0f / 60.0f;
		clock += dt;
		// The wheel: open while LB is held, the right stick picking instead of turning the camera.
		const bool wheel_open = (buttons & INPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		if (wheel_open) {
			const int p = wheel_pick(rx, ry);
			if (p >= 0)
				picked = p;
		} else {
			camera_yaw -= 0.045f * rx;
			camera_pitch -= 0.03f * ry;
			camera_pitch = camera_pitch < 0.05f ? 0.05f : camera_pitch > 1.1f ? 1.1f : camera_pitch;
		}
		if ((released & INPUT_GAMEPAD_LEFT_SHOULDER) && picked >= EMOTES) {
			// One of the player's own: once, then back to standing, with a neutral face.
			player->speed = 1.0f;
			xsdk_avatar_player_play(player, &customs[picked - EMOTES], XSDK_AVATAR_ONCE);
			(void)xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
			xsdk_avatar_set_face(avatar, &neutral);
			state = EMOTING;
			xsdk_log_write("store: %s", custom_names[picked - EMOTES]);
		} else if ((released & INPUT_GAMEPAD_LEFT_SHOULDER) && picked >= 0) {
			const Emote *e = &emotes[picked];
			const char *animation = info.gender == XSDK_AVATAR_FEMALE ? e->female : e->male;
			player->speed = 1.0f;
			if (animation && xsdk_avatar_play(avatar, animation, XSDK_AVATAR_ONCE) == XSDK_AVATAR_OK)
				(void)xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
			else
				(void)xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
			xsdk_avatar_set_face(avatar, &e->face);
			if (e->talks) {
				talk_left = talk_seconds;
				npcs_answer(npcs_nearest(x, z, answer_reach));
			}
			state = EMOTING;
			xsdk_log_write("store: %s", e->name);
		}
		if (!wheel_open)
			picked = -1;
		// A: the library's jump, in place, once, then back to standing (or walking, if the stick is held by then).
		if ((pressed & INPUT_GAMEPAD_A) && !wheel_open && can_jump && state != JUMPING) {
			player->speed = 1.0f;
			xsdk_avatar_player_play(player, &jump, XSDK_AVATAR_ONCE);
			(void)xsdk_avatar_queue(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
			xsdk_avatar_set_face(avatar, &neutral);
			talk_left = 0.0f;
			state = JUMPING;
		}
		const double cpu_start = seconds_now();

		const float forward[2] = { -sinf(camera_yaw), -cosf(camera_yaw) };
		const float right[2] = { cosf(camera_yaw), -sinf(camera_yaw) };
		float dx = right[0] * lx + forward[0] * ly, dz = right[1] * lx + forward[1] * ly;
		float push = sqrtf(dx * dx + dz * dz);
		if (push > 1.0f) {
			dx /= push;
			dz /= push;
			push = 1.0f;
		}
		if (state == JUMPING) {
			// In place: the feet stay where they stood until the jump has landed and its standing has begun.
			if (player->current.loop)
				state = STANDING;
		} else if (push > 0.0f) {
			// Walking, or running while the right trigger is held: each clip played at the pace its feet cover the
			// ground at.
			const bool running = can_run && run_held;
			const float speed = push * (running ? run_top_speed : walk_top_speed);
			heading = turn_towards(heading, atan2f(dx, dz), turn_rate * dt);
			x += dx / push * speed * dt;
			z += dz / push * speed * dt;
			scene_collide(&x, &z, avatar_radius);
			npcs_collide(&x, &z, avatar_radius);
			if (running ? state != RUNNING : state != WALKING) {
				xsdk_avatar_player_play(player, running ? &run : &walk, XSDK_AVATAR_LOOP);
				xsdk_avatar_set_face(avatar, &neutral);
				talk_left = 0.0f;
				state = running ? RUNNING : WALKING;
			}
			const float pace = speed / (running ? run_speed : walk_speed);
			player->speed = pace < 0.6f ? 0.6f : pace > 2.0f ? 2.0f : pace;
		} else if (state == WALKING || state == RUNNING) {
			player->speed = 1.0f;
			(void)xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
			state = STANDING;
		}
		talk_left -= dt;
		xsdk_avatar_set_talking(avatar, talk_left > 0.0f ? 0.35f + 0.65f * fabsf(sinf(7.0f * clock)) : 0.0f);
		if (state == EMOTING && player->current.loop && talk_left <= 0.0f) {
			xsdk_avatar_set_face(avatar, &neutral);
			state = STANDING;
		}
		const float c = cosf(heading), s = sinf(heading);
		const float world[16] = { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, x, 0, z, 1 };
		xsdk_avatar_set_transform(avatar, world);
		xsdk_avatar_update(avatar, dt);
		npcs_update(dt, x, z);

		const float target[3] = { x, camera_height, z };
		float eye[3] = { x + camera_distance * sinf(camera_yaw) * cosf(camera_pitch),
			camera_height + camera_distance * sinf(camera_pitch),
			z + camera_distance * cosf(camera_yaw) * cosf(camera_pitch) };
		eye[0] = fmaxf(-STORE_HALF_X + 0.15f, fminf(STORE_HALF_X - 0.15f, eye[0]));
		eye[1] = fminf(STORE_HEIGHT - 0.15f, eye[1]);
		eye[2] = fmaxf(-STORE_HALF_Z + 0.15f, fminf(STORE_HALF_Z - 0.15f, eye[2]));
		float vp[16], axes[3][3], lighting[MESH_LIGHT_VECTORS][4];
		look(vp, axes, eye, target, (float)width / (float)height);
		light_constants(lighting, &light, (const float (*)[3])axes);
		const bool shoot = can_shoot && triggers && !triggers_held;
		triggers_held = triggers;
		cpu_seconds += seconds_now() - cpu_start;

		uint32_t address;
		if (!NT_SUCCESS(xsdk_present_begin(&address, BEGIN_TIMEOUT_MS)))
			break;
		const double draw_start = seconds_now();
		const uint32_t rows = band_rows(width, height, samples);
		// Every band's colour after a whole band's depth: xsdk's own layout (depth, then colour) follows each
		// target's size, so the last, shorter band's colour would lie where the others keep depth, and its clear
		// (CLEAR_ARGB, read as depth about 0.996, some 10 m away) would hide what is farther in their rows (test run 6).
		const uint32_t color_tile = xsdk_gpu_target_sample_tiles(width, rows, samples);
		for (uint32_t top = 0; top < height; top += rows) {
			const uint32_t band = height - top < rows ? height - top : rows;
			xsdk_gpu_set_target_samples_at(width, band, samples, color_tile, 0);
			xsdk_gpu_set_viewport(0.0f, -(float)top, (float)width, (float)height, 0.0f, 1.0f);
			mesh_use_opaque();
			scene_draw(vp, (const float (*)[4])lighting);
			mesh_draw_shadow(vp, x, z, shadow_radius);
			npcs_draw_shadows(vp);
			xsdk_avatar_draw(avatar, vp);
			npcs_draw(vp);
			scene_draw_glow(vp, (const float (*)[4])lighting, clock);
			if (wheel_open)
				wheel_draw(picked);
			else
				wheel_draw_hint();
			if (shoot) {
				xsdk_gpu_set_resolve_clears(false, false);
				xsdk_gpu_resolve(shot.address + top * shot.pitch * 4, width, band, CLEAR_ARGB);
				xsdk_gpu_set_resolve_clears(true, true);
			}
			xsdk_gpu_resolve(address + top * width * 4, width, band, CLEAR_ARGB);
		}
		cpu_seconds += seconds_now() - draw_start;
		xsdk_present_end();
		if (shoot) {
			char path[96];
			if (screenshot_save(&shot, path, sizeof path))
				xsdk_log_write("store: screenshot %s", path);
		}
		if (++frames == REPORT_FRAMES) {
			xsdk_log_write("store: %u samples, CPU %.0f us a frame", (unsigned)samples, cpu_seconds * 1e6 / frames);
			frames = 0;
			cpu_seconds = 0.0;
		}
	}
	xsdk_present_close();
	(void)xsdk_gpu_wait(xsdk_gpu_fence(), FENCE_TIMEOUT_MS);
	xsdk_avatar_close(avatar);
	npcs_close();
	xsdk_avatar_animation_free(&walk);
	xsdk_avatar_animation_free(&jump);
	xsdk_avatar_animation_free(&run);
	for (uint32_t i = 0; i < custom_count; i++)
		xsdk_avatar_animation_free(&customs[i]);
	xsdk_avatar_shutdown();
	mesh_close();
	(void)xsdk_gpu_close();
	return 0;
}
