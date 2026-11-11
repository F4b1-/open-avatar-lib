// Store: walk your avatar round the Avatar Thrift Store among NPCs, and emote from a wheel. The walk, run and jump
// are the library's (xsdk_avatar_locomotion_*), the camera follows (follow_camera.c), the frame is drawn in bands
// (bands.c); the store (scene.c), its NPCs (npcs.c), the wheel (wheel.c) and its emotes (emotes.c) are this
// sample's own.
//
// Left stick: walk (run while the right trigger is held). A: jump. Right stick: turn the camera. LB held: the emote
// wheel; right stick picks, letting go of LB plays it. Right stick click: 2 / 4 / 1 samples a pixel. RB: the avatars
// drawn directly or recorded into a command buffer (the log gives CPU and GPU time a frame). Both triggers:
// screenshot. BACK: exit. Log: game:\store.log.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "bands.h"
#include "emotes.h"
#include "follow_camera.h"
#include "gpu_frames.h"
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
	TRIGGER_PULLED = 128, // both triggers past this: a screenshot
	TRIGGER_RUN = 64,     // the right trigger past this: running
};

static const float pi = 3.14159265f;
static const float shadow_radius = 0.38f, fade_seconds = 0.25f;
static const float talk_seconds = 3.5f, answer_reach = 2.5f;
static const float stick_dead_zone = 0.2f;
static const float max_frame_seconds = 0.1f; // a longer frame (a load, a hitch) counts as a 60th of a second

static const XsdkAvatarStockPart stock_parts[] = { { "MaleBody", 0xFFDCAD8F }, { "Head", 0xFFDCAD8F },
	{ "High and Tight", 0xFF4D331F }, { "Punk Jacket and Tee", 0xFF3359BF }, { "Black Skinny Fit Jeans", 0xFF26262E },
	{ "Brown Formal Shoes", 0xFF663D1F }, { "Happy Eyes", 0xFF4A3020 }, { "Studious Eyebrows", 0xFF4D331F },
	{ "Smirk Mouth", 0xFFB56157 } };

// A warm light from above the front, as under the store's spots; the avatars and the store share it.
static const XsdkAvatarLight light = { .direction = { 0.25f, 0.9f, 0.45f }, .color = { 0.85f, 0.78f, 0.66f },
	.sky = { 0.42f, 0.40f, 0.38f }, .ground = { 0.24f, 0.19f, 0.15f }, .wrap = 0.4f, .highlight = 0.0f };

static double seconds_now(void)
{
	struct timespec now;
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// A stick axis, -1 to 1, with a dead zone in the middle and the rest stretched to fill the range.
static float stick(int16_t value)
{
	const float v = (float)value / 32768.0f;
	if (fabsf(v) < stick_dead_zone)
		return 0.0f;
	return (v - (v > 0 ? stick_dead_zone : -stick_dead_zone)) / (1.0f - stick_dead_zone);
}

// The controller this frame: its buttons (and those just pressed and let go), sticks and triggers.
typedef struct {
	uint16_t buttons, pressed, released;
	float lx, ly, rx, ry;
	bool triggers; // both pulled
	bool run;      // the right trigger held
} Pad;

static Pad read_pad(uint16_t *previous)
{
	Pad pad = { 0 };
	INPUT_STATE input;
	if (XamInputGetState(0, 0, &input) == ERROR_SUCCESS) {
		pad.buttons = input.gamepad.buttons;
		pad.triggers = input.gamepad.left_trigger > TRIGGER_PULLED && input.gamepad.right_trigger > TRIGGER_PULLED;
		pad.run = input.gamepad.right_trigger > TRIGGER_RUN;
		pad.lx = stick(input.gamepad.thumb_lx);
		pad.ly = stick(input.gamepad.thumb_ly);
		pad.rx = stick(input.gamepad.thumb_rx);
		pad.ry = stick(input.gamepad.thumb_ry);
	}
	pad.pressed = pad.buttons & (uint16_t)~*previous;
	pad.released = *previous & (uint16_t)~pad.buttons;
	*previous = pad.buttons;
	return pad;
}

// The player's own avatar, from the first profile signed in, else a stock one.
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

// What the player bumps into: the store and the NPCs.
static void collide(void *context, float *x, float *z, float radius)
{
	(void)context;
	scene_collide(x, z, radius);
	npcs_collide(x, z, radius);
}

// The game: the player's avatar and how it moves, the camera, the wheel and talking.
typedef struct {
	XsdkAvatar *avatar;
	uint32_t gender;
	XsdkAvatarLocomotion me;
	FollowCamera camera;
	bool wheel_open;
	int picked;      // the wheel's segment the stick points at, -1 for none
	float talk_left; // seconds of talking left
	float clock;     // seconds since starting
} Game;

// The wheel while LB is held, the right stick picking instead of turning the camera; letting go plays the emote.
static void update_wheel(Game *g, const Pad *pad)
{
	g->wheel_open = (pad->buttons & INPUT_GAMEPAD_LEFT_SHOULDER) != 0;
	if (g->wheel_open) {
		const int p = wheel_pick(pad->rx, pad->ry);
		if (p >= 0)
			g->picked = p;
	} else {
		follow_camera_turn(&g->camera, pad->rx, pad->ry);
	}
	if ((pad->released & INPUT_GAMEPAD_LEFT_SHOULDER) && g->picked >= 0) {
		if (emotes_play(g->avatar, g->gender, (uint32_t)g->picked)) {
			g->talk_left = talk_seconds;
			npcs_answer(npcs_nearest(g->me.x, g->me.z, answer_reach));
		}
		g->me.state = XSDK_AVATAR_BUSY;
	}
	if (!g->wheel_open)
		g->picked = -1;
}

// The player's avatar this frame: a jump on A, moving where the left stick points, talking, back to a neutral face
// when an emote has ended; then the avatar and the NPCs moved on.
static void update_player(Game *g, const Pad *pad, float dt)
{
	float direction[2];
	follow_camera_direction(&g->camera, pad->lx, pad->ly, direction);
	const bool jump = (pad->pressed & INPUT_GAMEPAD_A) && !g->wheel_open;
	if (xsdk_avatar_locomotion_drive(
			g->avatar, &g->me, direction[0], direction[1], pad->run, jump, dt, collide, NULL)) {
		xsdk_avatar_set_face(g->avatar, &emotes_neutral);
		g->talk_left = 0.0f;
	}
	g->talk_left -= dt;
	xsdk_avatar_set_talking(g->avatar, g->talk_left > 0.0f ? 0.35f + 0.65f * fabsf(sinf(7.0f * g->clock)) : 0.0f);
	if (g->me.state == XSDK_AVATAR_BUSY && xsdk_avatar_player(g->avatar)->current.loop && g->talk_left <= 0.0f) {
		xsdk_avatar_set_face(g->avatar, &emotes_neutral);
		g->me.state = XSDK_AVATAR_STANDING;
	}
	xsdk_avatar_update(g->avatar, dt);
	npcs_update(dt, g->me.x, g->me.z);
}

// What every band draws: the store, the shadows, the avatars (drawn, or their recording run again), what glows, and
// the wheel over it all.
typedef struct {
	const Game *game;
	float vp[16];
	float lighting[MESH_LIGHT_VECTORS][4];
	const XsdkCommandBuffer *recording; // the avatars' draws recorded ahead of the frame, or NULL: drawn directly
} Frame;

// The avatars: the player's and the NPCs.
static void draw_avatars(void *context)
{
	const Frame *f = context;
	xsdk_avatar_draw(f->game->avatar, f->vp);
	npcs_draw(f->vp);
}

static void draw_band(void *context)
{
	const Frame *f = context;
	const Game *g = f->game;
	mesh_use_opaque();
	scene_draw(f->vp, (const float (*)[4])f->lighting);
	mesh_draw_shadow(f->vp, g->me.x, g->me.z, shadow_radius);
	npcs_draw_shadows(f->vp);
	if (f->recording)
		xsdk_gpu_execute(f->recording);
	else
		draw_avatars(context);
	scene_draw_glow(f->vp, (const float (*)[4])f->lighting, g->clock);
	if (g->wheel_open)
		wheel_draw(g->picked);
	else
		wheel_draw_hint();
}

// Everything before the loop: the GPU, the player's avatar and its moves, the NPCs, the store and the wheel.
static bool start(Game *g, uint32_t *width, uint32_t *height)
{
	NTSTATUS status = xsdk_gpu_open();
	if (NT_SUCCESS(status))
		status = xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, SWAP_INTERVAL, width, height);
	if (!NT_SUCCESS(status) || !mesh_open()) {
		xsdk_log_write("store: GPU setup failed: 0x%08X", (unsigned)status);
		return false;
	}
	xsdk_avatar_set_light(&light);
	g->avatar = load_avatar();
	if (!g->avatar || xsdk_avatar_locomotion_load(&g->me, g->avatar) != XSDK_AVATAR_OK)
		return false;
	g->me.z = 1.0f;
	g->me.heading = pi; // facing into the store
	XsdkAvatarInfo info;
	xsdk_avatar_info(g->avatar, &info);
	g->gender = info.gender;
	g->camera = (FollowCamera){ .yaw = 0.0f, .pitch = 0.4f, .distance = 3.0f, .height = 1.0f,
		.room = { STORE_HALF_X, STORE_HEIGHT, STORE_HALF_Z } };
	g->picked = -1;
	xsdk_log_write("store: %u NPCs", (unsigned)npcs_load());
	scene_build();
	(void)emotes_load(g->avatar);
	const char *names[EMOTES_MAX];
	const uint32_t count = emotes_names(names);
	wheel_build(*width, *height, names, count);
	if (!mesh_upload())
		return false;
	xsdk_avatar_player(g->avatar)->fade_seconds = fade_seconds;
	(void)xsdk_avatar_play(g->avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
	xsdk_gpu_set_depth_clear(1.0f, 0);
	return true;
}

int main(void)
{
	static Game game;
	uint32_t width = 0, height = 0;
	if (!start(&game, &width, &height))
		return 1;
	static XsdkTexture shot;
	const bool can_shoot = screenshot_init(&shot, width, height);
	static GpuFrames gpu;
	if (!gpu_frames_open(&gpu))
		return 1;
	uint32_t samples = 2, frames = 0;
	uint16_t previous_buttons = 0;
	bool triggers_held = false, recorded = false;
	double last_time = seconds_now(), cpu_seconds = 0.0;
	static Frame frame = { .game = &game };
	for (;;) {
		const Pad pad = read_pad(&previous_buttons);
		if (pad.buttons & INPUT_GAMEPAD_BACK)
			break;
		if (pad.pressed & INPUT_GAMEPAD_RIGHT_THUMB) {
			samples = samples == 4 ? 1 : samples * 2;
			xsdk_log_write("store: %u samples a pixel", (unsigned)samples);
		}
		if ((pad.pressed & INPUT_GAMEPAD_RIGHT_SHOULDER) && gpu.can_record) {
			recorded = !recorded;
			xsdk_log_write("store: avatars %s", recorded ? "recorded into a command buffer" : "drawn directly");
		}
		gpu_frames_next(&gpu);
		const double now = seconds_now();
		float dt = (float)(now - last_time);
		last_time = now;
		if (dt < 0.0f || dt > max_frame_seconds)
			dt = 1.0f / 60.0f;
		game.clock += dt;
		update_wheel(&game, &pad);
		const double cpu_start = seconds_now();
		update_player(&game, &pad, dt);
		float axes[3][3];
		follow_camera_view(&game.camera, game.me.x, game.me.z, (float)width / (float)height, frame.vp, axes);
		mesh_lighting(frame.lighting, &light, (const float (*)[3])axes);
		// RB: the avatars' draws recorded ahead of the frame, then run in every band.
		frame.recording = NULL;
		if (recorded)
			frame.recording = gpu_frames_record(&gpu, draw_avatars, &frame);
		const bool shoot = can_shoot && pad.triggers && !triggers_held;
		triggers_held = pad.triggers;
		cpu_seconds += seconds_now() - cpu_start;

		uint32_t address;
		if (!NT_SUCCESS(xsdk_present_begin(&address, BEGIN_TIMEOUT_MS)))
			break;
		const double draw_start = seconds_now();
		gpu_frames_clock_before(&gpu);
		bands_draw(width, height, samples, address, shoot ? &shot : NULL, CLEAR_ARGB, draw_band, &frame);
		gpu_frames_clock_after(&gpu);
		cpu_seconds += seconds_now() - draw_start;
		xsdk_present_end();
		gpu_frames_fence(&gpu, !shoot);
		if (shoot) {
			char path[96];
			if (screenshot_save(&shot, path, sizeof path))
				xsdk_log_write("store: screenshot %s", path);
		}
		if (++frames == REPORT_FRAMES) {
			xsdk_log_write("store: %u samples, avatars %s (%u dwords): CPU %.0f us, GPU %.0f us a frame",
				(unsigned)samples, frame.recording ? "recorded" : "direct", frame.recording ? (unsigned)gpu.recorded : 0u,
				cpu_seconds * 1e6 / frames, gpu_frames_take_us(&gpu));
			frames = 0;
			cpu_seconds = 0.0;
		}
	}
	xsdk_present_close();
	(void)xsdk_gpu_wait(xsdk_gpu_fence(), FENCE_TIMEOUT_MS);
	gpu_frames_close(&gpu);
	xsdk_avatar_close(game.avatar);
	npcs_close();
	xsdk_avatar_locomotion_free(&game.me);
	emotes_free();
	xsdk_avatar_shutdown();
	mesh_close();
	(void)xsdk_gpu_close();
	return 0;
}
