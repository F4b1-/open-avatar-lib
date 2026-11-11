// Hello avatar world: your avatar (the first signed-in profile's, or a stock one), walking where the left stick
// points, running while the right trigger is held, jumping on A. BACK exits. Log: game:\hello_avatar_world.log.
//
// The library does the avatar: loading it, its walk, run and jump (xsdk_avatar_locomotion_*), its shadow and its
// drawing. The title does what every xsdk title does: the GPU, the screen, the controller and the frame.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "xsdk/avatar.h"
#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"

static const uint32_t clear_argb = 0xFF6E8296;   // a plain grey-blue: the "world"
static const float stage_radius = 3.0f;           // metres the avatar can walk from the middle
static const float stick_dead_zone = 0.2f;

// Who to show if no one is signed in: stock parts by name, each with its colour.
static const XsdkAvatarStockPart stock_parts[] = {
	{ "MaleBody", 0xFFDCAD8F },
	{ "Head", 0xFFDCAD8F },
	{ "High and Tight", 0xFF4D331F },
	{ "Punk Jacket and Tee", 0xFF3359BF },
	{ "Black Skinny Fit Jeans", 0xFF26262E },
	{ "Brown Formal Shoes", 0xFF663D1F },
};

static double seconds_now(void)
{
	struct timespec now;
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// A stick axis, -1 to 1, with a dead zone in the middle.
static float stick(int16_t value)
{
	const float v = (float)value / 32768.0f;
	return fabsf(v) < stick_dead_zone ? 0.0f : v;
}

// The first signed-in player's avatar, or a stock one.
static XsdkAvatar *load_avatar(void)
{
	XsdkAvatar *avatar;
	for (uint32_t user = 0; user < 4; user++)
		if (xsdk_avatar_load_profile(&avatar, user) == XSDK_AVATAR_OK)
			return avatar;
	if (xsdk_avatar_load_stock(&avatar, stock_parts, sizeof stock_parts / sizeof stock_parts[0]) == XSDK_AVATAR_OK)
		return avatar;
	return NULL;
}

// The title's collision (XsdkAvatarCollide): here, only the edge of a round stage the avatar stays on.
static void stay_on_stage(void *context, float *x, float *z, float radius)
{
	(void)context;
	const float reach = stage_radius - radius, from_middle = sqrtf(*x * *x + *z * *z);
	if (from_middle > reach) {
		*x *= reach / from_middle;
		*z *= reach / from_middle;
	}
}

int main(void)
{
	uint32_t width = 0, height = 0;
	if (!NT_SUCCESS(xsdk_gpu_open()) || !NT_SUCCESS(xsdk_present_open(XSDK_DISPLAY_720P, 2, 1, &width, &height)))
		return 1;
	XsdkAvatar *avatar = load_avatar();
	XsdkAvatarLocomotion me;
	if (!avatar || xsdk_avatar_locomotion_load(&me, avatar) != XSDK_AVATAR_OK) {
		xsdk_log_write("hello_avatar_world: no avatar");
		return 1;
	}
	(void)xsdk_avatar_play(avatar, XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);

	// Depth on, so the avatar hides itself properly.
	XsdkDepthStencil depth_setting = { .depth_test = true, .depth_write = true,
		.depth_compare = XSDK_COMPARE_LESS_EQUAL };
	XsdkDepthStencilState depth;
	xsdk_gpu_depth_stencil_state(&depth, &depth_setting);
	xsdk_gpu_set_depth_clear(1.0f, 0);
	// A fixed camera in front of the stage and above it, looking down at its middle: 0.7 radians high, 0.1 to 30 m.
	const float eye[3] = { 0.0f, 3.5f, 7.0f }, target[3] = { 0.0f, 0.6f, 0.0f };
	float vp[16];
	xsdk_avatar_camera(vp, eye, target, 0.7f, (float)width / (float)height, 0.1f, 30.0f);

	uint16_t previous_buttons = 0;
	double last = seconds_now();
	for (;;) {
		INPUT_STATE input;
		uint16_t buttons = 0;
		float lx = 0.0f, ly = 0.0f;
		bool run = false;
		if (XamInputGetState(0, 0, &input) == ERROR_SUCCESS) {
			buttons = input.gamepad.buttons;
			lx = stick(input.gamepad.thumb_lx);
			ly = stick(input.gamepad.thumb_ly);
			run = input.gamepad.right_trigger > 64;
		}
		if (buttons & INPUT_GAMEPAD_BACK)
			break;
		const bool jump = (buttons & INPUT_GAMEPAD_A) && !(previous_buttons & INPUT_GAMEPAD_A);
		previous_buttons = buttons;
		const double now = seconds_now();
		const float dt = fminf((float)(now - last), 0.1f);
		last = now;

		// The camera looks along -z: the stick's right is +x, its up is away from the camera (-z).
		xsdk_avatar_locomotion_drive(avatar, &me, lx, -ly, run, jump, dt, stay_on_stage, NULL);
		xsdk_avatar_update(avatar, dt);

		uint32_t address;
		if (!NT_SUCCESS(xsdk_present_begin(&address, 1000)))
			break;
		xsdk_gpu_set_target(width, height);
		xsdk_gpu_set_depth_stencil(&depth);
		xsdk_avatar_draw_shadow(avatar, vp);
		xsdk_avatar_draw(avatar, vp);
		xsdk_gpu_resolve(address, width, height, clear_argb);
		xsdk_present_end();
	}
	xsdk_present_close();
	// An avatar and its animations are freed only once the GPU is past the last frame that drew them.
	(void)xsdk_gpu_wait(xsdk_gpu_fence(), 2000);
	xsdk_avatar_close(avatar);
	xsdk_avatar_locomotion_free(&me);
	xsdk_avatar_shutdown();
	(void)xsdk_gpu_close();
	return 0;
}
