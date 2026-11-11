// Hello avatar: the signed-in player's avatar (or a stock one), standing; Y waves. BACK exits. Log: game:\hello.log.
// In C++, calling the library's C API (avatar.h) directly: the avatar held in a std::unique_ptr that closes it, and
// released by hand after the last fence, since an avatar may only be closed once the GPU is past the last frame
// that drew it.

#include <cmath>
#include <ctime>
#include <memory>

#include "xsdk/avatar.h"
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

double seconds_now()
{
	timespec now;
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

// Closes an avatar; the title resets its pointer only after a fence past the last frame that drew it.
struct AvatarClose {
	void operator()(XsdkAvatar *avatar) const
	{
		xsdk_avatar_close(avatar);
	}
};
using AvatarPtr = std::unique_ptr<XsdkAvatar, AvatarClose>;

// The first signed-in player's avatar, or a stock one; empty if neither loads.
AvatarPtr load_avatar()
{
	XsdkAvatar *avatar;
	for (uint32_t user = 0; user < 4; user++)
		if (xsdk_avatar_load_profile(&avatar, user) == XSDK_AVATAR_OK)
			return AvatarPtr(avatar);
	if (xsdk_avatar_load_stock(&avatar, stock_parts, std::size(stock_parts)) == XSDK_AVATAR_OK)
		return AvatarPtr(avatar);
	return nullptr;
}

} // namespace

int main()
{
	uint32_t width = 0, height = 0;
	if (!NT_SUCCESS(xsdk_gpu_open()) || !NT_SUCCESS(xsdk_present_open(XSDK_DISPLAY_720P, 2, 1, &width, &height)))
		return 1;
	AvatarPtr avatar = load_avatar();
	if (!avatar) {
		xsdk_log_write("hello: no avatar");
		return 1;
	}
	xsdk_avatar_play(avatar.get(), XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
	XsdkAvatarInfo info;
	xsdk_avatar_info(avatar.get(), &info);

	XsdkDepthStencil depth_setting{};
	depth_setting.depth_test = true;
	depth_setting.depth_write = true;
	depth_setting.depth_compare = XSDK_COMPARE_LESS_EQUAL;
	XsdkDepthStencilState depth;
	xsdk_gpu_depth_stencil_state(&depth, &depth_setting);
	xsdk_gpu_set_depth_clear(1.0f, 0);
	// A camera 3.5 m in front of the avatar, looking at its middle; 0.6 radians (34 degrees) high, 0.1 to 20 m.
	const float middle = 0.52f * info.height;
	const float eye[3] = { 0.0f, middle, 3.5f }, target[3] = { 0.0f, middle, 0.0f };
	float vp[16];
	xsdk_avatar_camera(vp, eye, target, 0.6f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 20.0f);

	uint16_t previous_buttons = 0;
	double last = seconds_now();
	for (;;) {
		INPUT_STATE input;
		const uint16_t buttons = XamInputGetState(0, 0, &input) == ERROR_SUCCESS ? input.gamepad.buttons : 0;
		if (buttons & INPUT_GAMEPAD_BACK)
			break;
		if ((buttons & INPUT_GAMEPAD_Y) && !(previous_buttons & INPUT_GAMEPAD_Y)) {
			xsdk_avatar_play(avatar.get(), XSDK_AVATAR_ANIMATION_WAVE, XSDK_AVATAR_ONCE);
			xsdk_avatar_queue(avatar.get(), XSDK_AVATAR_ANIMATION_STAND, XSDK_AVATAR_LOOP);
		}
		previous_buttons = buttons;
		const double now = seconds_now();
		xsdk_avatar_update(avatar.get(), static_cast<float>(now - last));
		last = now;

		uint32_t address;
		if (!NT_SUCCESS(xsdk_present_begin(&address, 1000)))
			break;
		xsdk_gpu_set_target(width, height);
		xsdk_gpu_set_depth_stencil(&depth);
		xsdk_avatar_draw(avatar.get(), vp);
		xsdk_gpu_resolve(address, width, height, clear_argb);
		xsdk_present_end();
	}
	xsdk_present_close();
	(void)xsdk_gpu_wait(xsdk_gpu_fence(), 2000);
	avatar.reset(); // past the fence: now it may close
	xsdk_avatar_shutdown();
	(void)xsdk_gpu_close();
	return 0;
}
