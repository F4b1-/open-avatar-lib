// The console's avatar files (avatar_runtime.h): the stock pack (FORMATS.md, section 4.3), item packages (section 4),
// profiles' descriptions (section 4.6), and the stock animations decoded once for every avatar.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_internal.h"
#include "avatar_runtime.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"

enum {
	FILE_SHARE_DELETE = 0x00000004,
	MAX_ANIMATIONS = 64, // 60 in the pack (FORMATS.md, section 4.1)
	// XamUserGetSigninInfo's flags: 1 and 2 each ask for one of the two XUIDs (Avatar Thrift Store's
	// app/kernel_extra.h); either does to learn whether the user is signed in.
	SIGNIN_INFO_ONE_XUID = 1,
	SIGNIN_INFO_OTHER_XUID = 2,
	SIGNIN_STATE_SIGNED_OUT = 0,
	USERS = 4,
	OWNERS = 1 + USERS, // the shared folder, and each signed-in profile's
	OWNER_CHARS = 16,   // a folder named after an XUID: 16 hex digits
	OFFLINE_XUID_TOP = 0xE, // an offline XUID's top four bits
};

static const char pack_path[] = "\\SEP\\Content\\0000000000000000\\FFFE07DF\\00008000\\FFFE07DF00000002";
// Where item packages are (FORMATS.md, section 4): <device>\Content\<owner>\<title id>\00009000\<package>, the owner
// the shared folder (all zeros) or a profile's own (its offline XUID). The devices in the order searched, the first
// copy found taken: the hard drive (Avatar Thrift Store installs there), the USB ports, the consoles' built-in memory
// units, then the memory unit slots. Every device is in the \Device listing of probe_pack.log (EVIDENCE.md, "Device
// listing"); that each keeps Content at its root is inferred from the hard drive and USB. A device a console lacks
// fails to open and the search goes on.
static const char *const item_devices[] = {
	"\\Device\\Harddisk0\\Partition1",
	"\\Device\\Mass0",
	"\\Device\\Mass1",
	"\\Device\\Mass2",                 // the third USB port (Avatar Thrift Store's "USB 3")
	"\\Device\\BuiltInMuUsb\\Storage", // 4 GB slims' flash; "Storage" not yet confirmed
	"\\Device\\BuiltInMuSfc",          // 256 and 512 MB Arcades' NAND
	"\\Device\\Mu0",                   // memory unit slot A
	"\\Device\\Mu1",                   // and B
};
static const char shared_owner[] = "0000000000000000";
static const char item_content_type[] = "00009000";
static const char item_asset_file[] = "asset_v2.bin"; // an item package's STRB file (FORMATS.md, section 3)

// xenia:src/xenia/kernel/xam/xam_user.cc (XUSER_SIGNIN_INFO), as Avatar Thrift Store declares it.
typedef struct {
	uint64_t xuid;
	uint32_t flags;
	uint32_t signin_state;
	uint32_t guest_number;
	uint32_t sponsor_user_index;
	char name[16];
} SigninInfo;

uint32_t XamUserGetSigninInfo(uint32_t user_index, uint32_t flags, SigninInfo *info);
// Fills the 1000-byte description synchronously when overlapped is NULL (FORMATS.md, section 4.6).
uint32_t XamAvatarGetManifestLocalUser(uint32_t user_index, void *description, void *overlapped);

// What the library keeps open between loads: the stock pack, opened on first use, and the stock animations, each
// decoded the first time it is asked for.
static struct {
	HANDLE pack_file;
	XsdkAvatarPack *pack;
	uint32_t animation_count;
	uint32_t animation_records[MAX_ANIMATIONS];    // the pack's animation records
	XsdkAvatarAnimation animations[MAX_ANIMATIONS]; // those decoded so far (frame_count 0 until then)
} content;

static int read_handle(void *context, uint64_t offset, void *buffer, uint32_t length)
{
	IO_STATUS_BLOCK io_status;
	const NTSTATUS status = NtReadFile(*(HANDLE *)context, 0, NULL, NULL, &io_status, buffer, length, &offset);
	return NT_SUCCESS(status) && io_status.information == length ? 0 : 1;
}

// Opens for reading, sharing everything: the system holds the pack open (FORMATS.md, section 4.3).
static NTSTATUS open_read(HANDLE *handle, const char *path)
{
	const ANSI_STRING name = { .length = (uint16_t)strlen(path),
		.maximum_length = (uint16_t)(strlen(path) + 1),
		.pointer = path };
	const OBJECT_ATTRIBUTES attributes = { .root_directory = 0, .name = &name };
	IO_STATUS_BLOCK io_status;
	return NtCreateFile(handle, GENERIC_READ | SYNCHRONIZE, &attributes, &io_status, NULL, FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
}

// The pack's animation records: those whose component mask is the animations' and nothing else.
static void find_animations(void)
{
	content.animation_count = 0;
	for (uint32_t i = 0; i < xsdk_avatar_pack_count(content.pack) && content.animation_count < MAX_ANIMATIONS; i++)
		if (xsdk_avatar_pack_mask(content.pack, i) == XSDK_AVATAR_SLOT_ANIMATION)
			content.animation_records[content.animation_count++] = i;
}

int avatar_content_pack(const XsdkAvatarPack **out)
{
	if (content.pack) {
		*out = content.pack;
		return XSDK_AVATAR_OK;
	}
	const NTSTATUS status = open_read(&content.pack_file, pack_path);
	if (!NT_SUCCESS(status)) {
		xsdk_log_write("avatar: can't open the stock pack %s: 0x%08X", pack_path, (unsigned)status);
		return XSDK_AVATAR_ERROR_READ;
	}
	const int result = xsdk_avatar_pack_open(&content.pack, read_handle, &content.pack_file);
	if (result != XSDK_AVATAR_OK) {
		xsdk_log_write("avatar: can't read the stock pack: %d", result);
		(void)NtClose(content.pack_file);
		return result;
	}
	find_animations();
	xsdk_log_write("avatar: stock pack of %u records, %u animations", (unsigned)xsdk_avatar_pack_count(content.pack),
		(unsigned)content.animation_count);
	*out = content.pack;
	return XSDK_AVATAR_OK;
}

int avatar_content_read_stock(uint32_t record, void **strb, uint32_t *size_bytes)
{
	const XsdkAvatarPack *pack = content.pack;
	*strb = NULL;
	*size_bytes = xsdk_avatar_pack_asset_size(pack, record);
	if (!*size_bytes) {
		xsdk_log_write(
			"avatar: stock record %u (%s) has no asset", (unsigned)record, xsdk_avatar_pack_name(pack, record));
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	}
	void *data = avatar_malloc(*size_bytes, "a stock asset");
	if (!data)
		return XSDK_AVATAR_ERROR_MEMORY;
	const int result = xsdk_avatar_pack_read_asset(pack, record, data);
	if (result != XSDK_AVATAR_OK) {
		xsdk_log_write("avatar: stock record %u (%s) can't be read (%d)", (unsigned)record,
			xsdk_avatar_pack_name(pack, record), result);
		free(data);
		return result;
	}
	*strb = data;
	return XSDK_AVATAR_OK;
}

// A signed-in user's offline XUID, the one their profile's folders are named after; 0 if no one is signed in there.
// XamUserGetSigninInfo's two flags each give one of a profile's two XUIDs, which one undocumented, so both are asked
// and the offline one (top four bits E) kept, as Avatar Thrift Store's app/profiles.cpp does (taking flag 1 for it
// made an activated profile miss its own folder there).
static uint64_t offline_xuid(uint32_t user)
{
	const uint32_t flags[2] = { SIGNIN_INFO_ONE_XUID, SIGNIN_INFO_OTHER_XUID };
	for (uint32_t f = 0; f < 2; f++) {
		SigninInfo info;
		memset(&info, 0, sizeof info);
		if (XamUserGetSigninInfo(user, flags[f], &info) == 0 && info.signin_state != SIGNIN_STATE_SIGNED_OUT &&
			info.xuid >> 60 == OFFLINE_XUID_TOP)
			return info.xuid;
	}
	return 0;
}

// The Content folders an item can be under: the shared one, then each signed-in profile's (no repeats). Returns how
// many.
static uint32_t item_owners(char owners[OWNERS][OWNER_CHARS + 1])
{
	snprintf(owners[0], OWNER_CHARS + 1, "%s", shared_owner);
	uint32_t count = 1;
	for (uint32_t user = 0; user < USERS; user++) {
		const uint64_t xuid = offline_xuid(user);
		if (!xuid)
			continue;
		snprintf(owners[count], OWNER_CHARS + 1, "%016llX", (unsigned long long)xuid);
		bool seen = false;
		for (uint32_t o = 1; o < count; o++)
			seen |= strcmp(owners[o], owners[count]) == 0;
		count += !seen;
	}
	return count;
}

// The item's package at path read: XSDK_AVATAR_ERROR_NOT_FOUND if there is none, else the read's result.
static int read_item_at(const char *path, void **strb, uint32_t *size_bytes)
{
	HANDLE handle;
	if (!NT_SUCCESS(open_read(&handle, path)))
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	const int result = xsdk_avatar_package_read_file(read_handle, &handle, item_asset_file, strb, size_bytes);
	(void)NtClose(handle);
	if (result != XSDK_AVATAR_OK)
		xsdk_log_write("avatar: %s: %s can't be read (%d)", path, item_asset_file, result);
	return result;
}

int avatar_content_read_item(const uint8_t *id, void **strb, uint32_t *size_bytes)
{
	*strb = NULL;
	char name[33];
	uint32_t title;
	xsdk_avatar_id_package_name(id, name, &title);
	char owners[OWNERS][OWNER_CHARS + 1];
	const uint32_t owner_count = item_owners(owners);
	for (size_t d = 0; d < sizeof item_devices / sizeof item_devices[0]; d++)
		for (uint32_t o = 0; o < owner_count; o++) {
			char path[192];
			snprintf(path, sizeof path, "%s\\Content\\%s\\%08X\\%s\\%s", item_devices[d], owners[o], (unsigned)title,
				item_content_type, name);
			const int result = read_item_at(path, strb, size_bytes);
			if (result == XSDK_AVATAR_ERROR_NOT_FOUND)
				continue;
			if (result == XSDK_AVATAR_OK)
				xsdk_log_write("avatar: item %s from %s", name, path);
			return result;
		}
	xsdk_log_write("avatar: item %s (title %08X): not on this console (%u places, %u profiles searched)", name,
		(unsigned)title, (unsigned)(sizeof item_devices / sizeof item_devices[0]), (unsigned)(owner_count - 1));
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}

int avatar_content_read_profile(uint32_t user_index, uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES])
{
	SigninInfo info;
	memset(&info, 0, sizeof info);
	if (XamUserGetSigninInfo(user_index, SIGNIN_INFO_ONE_XUID, &info) != 0 ||
		info.signin_state == SIGNIN_STATE_SIGNED_OUT)
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	const uint32_t result = XamAvatarGetManifestLocalUser(user_index, bytes, NULL);
	xsdk_log_write("avatar: user %u (%.16s): description 0x%08X", (unsigned)user_index, info.name, (unsigned)result);
	return result == 0 ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_NOT_FOUND;
}

void avatar_content_close(void)
{
	for (uint32_t a = 0; a < content.animation_count; a++)
		xsdk_avatar_animation_free(&content.animations[a]);
	if (content.pack) {
		xsdk_avatar_pack_close(content.pack);
		(void)NtClose(content.pack_file);
	}
	memset(&content, 0, sizeof content);
}

uint32_t xsdk_avatar_stock_animation_count(void)
{
	const XsdkAvatarPack *p;
	return avatar_content_pack(&p) == XSDK_AVATAR_OK ? content.animation_count : 0;
}

const char *xsdk_avatar_stock_animation_name(uint32_t index)
{
	return index < xsdk_avatar_stock_animation_count()
		? xsdk_avatar_pack_name(content.pack, content.animation_records[index])
		: "";
}

// Decodes stock animation a, the first time it is asked for.
static int decode_stock_animation(uint32_t a, const char *name)
{
	XsdkAvatarAnimation *animation = &content.animations[a];
	if (animation->frame_count)
		return XSDK_AVATAR_OK;
	uint32_t size;
	void *strb;
	int result = avatar_content_read_stock(content.animation_records[a], &strb, &size);
	if (result != XSDK_AVATAR_OK)
		return result;
	result = xsdk_avatar_animation_decode(animation, strb, size);
	free(strb);
	if (result != XSDK_AVATAR_OK) {
		xsdk_log_write("avatar: animation %s: %d", name, result);
		return result;
	}
	xsdk_log_write("avatar: animation %s: %u frames, %.2f s", name, (unsigned)animation->frame_count,
		(double)xsdk_avatar_animation_length(animation));
	return XSDK_AVATAR_OK;
}

int xsdk_avatar_stock_animation(const char *name, const XsdkAvatarAnimation **animation)
{
	const XsdkAvatarPack *p;
	int result = avatar_content_pack(&p);
	if (result != XSDK_AVATAR_OK)
		return result;
	for (uint32_t a = 0; a < content.animation_count; a++) {
		if (strcmp(xsdk_avatar_pack_name(p, content.animation_records[a]), name) != 0)
			continue;
		result = decode_stock_animation(a, name);
		if (result == XSDK_AVATAR_OK)
			*animation = &content.animations[a];
		return result;
	}
	xsdk_log_write("avatar: no stock animation named %s", name);
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}
