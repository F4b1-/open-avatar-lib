#ifndef XSDK_AVATAR_HPP
#define XSDK_AVATAR_HPP

// The avatar layer for C++23 (SPECS.md, section 1.3): xsdk::Avatar owns an XsdkAvatar and closes it when destroyed.
// It adds no behaviour of its own; avatar.h documents each call. Destroy an avatar only after a fence past the last
// frame that drew it, as xsdk_avatar_close says.

#include <cstdint>
#include <expected>
#include <span>
#include <utility>

#include "xsdk/avatar.h"

namespace xsdk
{

class Avatar {
public:
	// Loads return the avatar, or an XSDK_AVATAR_ERROR_*.
	static std::expected<Avatar, int> load_profile(uint32_t user_index)
	{
		XsdkAvatar *avatar;
		return made(xsdk_avatar_load_profile(&avatar, user_index), avatar);
	}

	static std::expected<Avatar, int> load_description(std::span<const uint8_t> bytes)
	{
		XsdkAvatar *avatar;
		return made(xsdk_avatar_load_description(&avatar, bytes.data(), static_cast<uint32_t>(bytes.size())), avatar);
	}

	static std::expected<Avatar, int> load_stock(std::span<const XsdkAvatarStockPart> parts)
	{
		XsdkAvatar *avatar;
		return made(xsdk_avatar_load_stock(&avatar, parts.data(), static_cast<uint32_t>(parts.size())), avatar);
	}

	Avatar(Avatar &&other) noexcept : avatar_(std::exchange(other.avatar_, nullptr))
	{
	}

	Avatar &operator=(Avatar &&other) noexcept
	{
		if (this != &other) {
			xsdk_avatar_close(avatar_);
			avatar_ = std::exchange(other.avatar_, nullptr);
		}
		return *this;
	}

	Avatar(const Avatar &) = delete;
	Avatar &operator=(const Avatar &) = delete;

	~Avatar()
	{
		xsdk_avatar_close(avatar_);
	}

	XsdkAvatarInfo info() const
	{
		XsdkAvatarInfo info;
		xsdk_avatar_info(avatar_, &info);
		return info;
	}

	int play(const char *animation, uint32_t mode = XSDK_AVATAR_LOOP)
	{
		return xsdk_avatar_play(avatar_, animation, mode);
	}

	int queue(const char *animation, uint32_t mode = XSDK_AVATAR_LOOP)
	{
		return xsdk_avatar_queue(avatar_, animation, mode);
	}

	bool finished() const
	{
		return xsdk_avatar_finished(avatar_);
	}

	XsdkAvatarPlayer &player()
	{
		return *xsdk_avatar_player(avatar_);
	}

	void update(float seconds)
	{
		xsdk_avatar_update(avatar_, seconds);
	}

	void set_face(const XsdkAvatarFace &face)
	{
		xsdk_avatar_set_face(avatar_, &face);
	}

	void set_blinking(bool blinking)
	{
		xsdk_avatar_set_blinking(avatar_, blinking);
	}

	void set_talking(float level)
	{
		xsdk_avatar_set_talking(avatar_, level);
	}

	void set_transform(const float (&world)[16])
	{
		xsdk_avatar_set_transform(avatar_, world);
	}

	void draw(const float (&view_projection)[16]) const
	{
		xsdk_avatar_draw(avatar_, view_projection);
	}

	void draw_pose(std::span<const XsdkAvatarBonePose, XSDK_AVATAR_BONES> pose, const float (&view_projection)[16]) const
	{
		xsdk_avatar_draw_pose(avatar_, pose.data(), view_projection);
	}

	const XsdkAvatarSkeleton &skeleton() const
	{
		return *xsdk_avatar_skeleton(avatar_);
	}

	XsdkAvatar *get() const
	{
		return avatar_;
	}

private:
	explicit Avatar(XsdkAvatar *avatar) : avatar_(avatar)
	{
	}

	static std::expected<Avatar, int> made(int result, XsdkAvatar *avatar)
	{
		if (result != XSDK_AVATAR_OK)
			return std::unexpected(result);
		return Avatar(avatar);
	}

	XsdkAvatar *avatar_;
};

} // namespace xsdk

#endif
