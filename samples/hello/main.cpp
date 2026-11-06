// Hello avatar: the signed-in player's avatar (or a stock one), standing; Y waves. BACK exits. Built with the C++
// wrapper (xsdk/avatar.hpp); the C calls it wraps are named in avatar.h. Log: game:\hello.log.

#include <cmath>
#include <ctime>
#include <optional>

#include "xsdk/avatar.hpp"
#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"

namespace
{

constexpr uint32_t clear_argb = 0xFF303848;

constexpr XsdkAvatarStockPart stock_parts[] = {
	{ "MaleBody", 0xFFDCAD8F },
	{ "Head", 0xFFDCAD8F },
	{ "High and Tight", 0xFF4D331F },
	{ "Punk Jacket and Tee", 0xFF3359BF },
	{ "Black Skinny Fit Jeans", 0xFF26262E },
	{ "Brown Formal Shoes", 0xFF663D1F },
};

// A camera 3.5 m in front of the avatar, looking at its middle: column-major, depth 0 to 1.
void camera(float vp[16], float aspect, float middle)
{
	const float focal = 1.0f / std::tan(0.3f), near = 0.1f, far = 20.0f, k = far / (far - near);
	const float distance = 3.5f;
	const float m[16] = { focal / aspect, 0, 0, 0, 0, focal, 0, 0, 0, 0, -k, -1, 0, -middle * focal,
		(distance - near) * k, distance };
	for (int i = 0; i < 16; i++)
		vp[i] = m[i];
}

double seconds_now()
{
	timespec now;
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

std::optional<xsdk::Avatar> load_avatar()
{
	for (uint32_t user = 0; user < 4; user++)
		if (auto avatar = xsdk::Avatar::load_profile(user))
			return std::move(*avatar);
	if (auto avatar = xsdk::Avatar::load_stock(stock_parts))
		return std::move(*avatar);
	return std::nullopt;
}

} // namespace

int main()
{
	uint32_t width = 0, height = 0;
	if (!NT_SUCCESS(xsdk_gpu_open()) || !NT_SUCCESS(xsdk_present_open(XSDK_DISPLAY_720P, 2, 1, &width, &height)))
		return 1;
	std::optional<xsdk::Avatar> avatar = load_avatar();
	if (!avatar) {
		xsdk_log_write("hello: no avatar");
		return 1;
	}
	avatar->play(XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);

	XsdkDepthStencil depth_setting{};
	depth_setting.depth_test = true;
	depth_setting.depth_write = true;
	depth_setting.depth_compare = XSDK_COMPARE_LESS_EQUAL;
	XsdkDepthStencilState depth;
	xsdk_gpu_depth_stencil_state(&depth, &depth_setting);
	xsdk_gpu_set_depth_clear(1.0f, 0);
	float vp[16];
	camera(vp, static_cast<float>(width) / static_cast<float>(height), 0.52f * avatar->info().height);

	uint16_t previous_buttons = 0;
	double last = seconds_now();
	for (;;) {
		INPUT_STATE input;
		const uint16_t buttons = XamInputGetState(0, 0, &input) == ERROR_SUCCESS ? input.gamepad.buttons : 0;
		if (buttons & INPUT_GAMEPAD_BACK)
			break;
		if ((buttons & INPUT_GAMEPAD_Y) && !(previous_buttons & INPUT_GAMEPAD_Y)) {
			avatar->play(XSDK_AVATAR_ANIMATION_WAVE, XSDK_AVATAR_ONCE);
			avatar->queue(XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
		}
		previous_buttons = buttons;
		const double now = seconds_now();
		avatar->update(static_cast<float>(now - last));
		last = now;

		uint32_t address;
		if (!NT_SUCCESS(xsdk_present_begin(&address, 1000)))
			break;
		xsdk_gpu_set_target(width, height);
		xsdk_gpu_set_depth_stencil(&depth);
		avatar->draw(vp);
		xsdk_gpu_resolve(address, width, height, clear_argb);
		xsdk_present_end();
	}
	xsdk_present_close();
	(void)xsdk_gpu_wait(xsdk_gpu_fence(), 2000);
	avatar.reset();
	xsdk_avatar_shutdown();
	(void)xsdk_gpu_close();
	return 0;
}
