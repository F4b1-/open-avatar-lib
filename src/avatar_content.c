// The console's avatar files (avatar_runtime.h): the stock pack (SPECS.md, section 4.3), item packages (section 4),
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
	MAX_ANIMATIONS = 64, // 60 in the pack (SPECS.md, section 4.1)
};

static const char pack_path[] = "\\SEP\\Content\\0000000000000000\\FFFE07DF\\00008000\\FFFE07DF00000002";
// Where shared item packages are: the hard drive first (Avatar Thrift Store installs there), then USB.
static const char *const item_folders[] = {
	"\\Device\\Harddisk0\\Partition1\\Content\\0000000000000000",
	"\\Device\\Mass0\\Content\\0000000000000000",
	"\\Device\\Mass1\\Content\\0000000000000000",
};

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
// Fills the 1000-byte description synchronously when overlapped is NULL (SPECS.md, section 4.6).
uint32_t XamAvatarGetManifestLocalUser(uint32_t user_index, void *description, void *overlapped);

static HANDLE pack_file;
static XsdkAvatarPack *pack;
// The pack's animation records, and those decoded so far (frame_count 0 until then).
static uint32_t animation_count;
static uint32_t animation_records[MAX_ANIMATIONS];
static XsdkAvatarAnimation animations[MAX_ANIMATIONS];

static int read_handle(void *context, uint64_t offset, void *buffer, uint32_t length)
{
	IO_STATUS_BLOCK io_status;
	const NTSTATUS status = NtReadFile(*(HANDLE *)context, 0, NULL, NULL, &io_status, buffer, length, &offset);
	return NT_SUCCESS(status) && io_status.information == length ? 0 : 1;
}

// Opens for reading, sharing everything: the system holds the pack open (SPECS.md, section 4.3).
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

int avatar_content_pack(const XsdkAvatarPack **out)
{
	if (pack) {
		*out = pack;
		return XSDK_AVATAR_OK;
	}
	const NTSTATUS status = open_read(&pack_file, pack_path);
	if (!NT_SUCCESS(status)) {
		xsdk_log_write("avatar: can't open the stock pack %s: 0x%08X", pack_path, (unsigned)status);
		return XSDK_AVATAR_ERROR_READ;
	}
	const int result = xsdk_avatar_pack_open(&pack, read_handle, &pack_file);
	if (result != XSDK_AVATAR_OK) {
		xsdk_log_write("avatar: can't read the stock pack: %d", result);
		(void)NtClose(pack_file);
		return result;
	}
	animation_count = 0;
	for (uint32_t i = 0; i < xsdk_avatar_pack_count(pack) && animation_count < MAX_ANIMATIONS; i++)
		if (xsdk_avatar_pack_mask(pack, i) == XSDK_AVATAR_SLOT_ANIMATION)
			animation_records[animation_count++] = i;
	xsdk_log_write("avatar: stock pack of %u records, %u animations", (unsigned)xsdk_avatar_pack_count(pack),
		(unsigned)animation_count);
	*out = pack;
	return XSDK_AVATAR_OK;
}

int avatar_content_read_stock(uint32_t record, void **strb, uint32_t *size_bytes)
{
	*strb = NULL;
	*size_bytes = xsdk_avatar_pack_asset_size(pack, record);
	if (!*size_bytes) {
		xsdk_log_write("avatar: stock record %u (%s) has no asset", (unsigned)record, xsdk_avatar_pack_name(pack, record));
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

int avatar_content_read_item(const uint8_t *id, void **strb, uint32_t *size_bytes)
{
	*strb = NULL;
	char name[33];
	uint32_t title;
	xsdk_avatar_id_package_name(id, name, &title);
	for (size_t f = 0; f < sizeof item_folders / sizeof item_folders[0]; f++) {
		char path[160];
		snprintf(path, sizeof path, "%s\\%08X\\00009000\\%s", item_folders[f], (unsigned)title, name);
		HANDLE handle;
		if (!NT_SUCCESS(open_read(&handle, path)))
			continue;
		const int result = xsdk_avatar_package_read_file(read_handle, &handle, "asset_v2.bin", strb, size_bytes);
		(void)NtClose(handle);
		if (result != XSDK_AVATAR_OK) {
			xsdk_log_write("avatar: %s: asset_v2.bin can't be read (%d)", path, result);
			return result;
		}
		xsdk_log_write("avatar: item %s from %s", name, path);
		return XSDK_AVATAR_OK;
	}
	xsdk_log_write("avatar: item %s (title %08X): not on this console", name, (unsigned)title);
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}

int avatar_content_read_profile(uint32_t user_index, uint8_t bytes[XSDK_AVATAR_DESCRIPTION_BYTES])
{
	SigninInfo info;
	memset(&info, 0, sizeof info);
	if (XamUserGetSigninInfo(user_index, 1, &info) != 0 || info.signin_state == 0)
		return XSDK_AVATAR_ERROR_NOT_FOUND;
	const uint32_t result = XamAvatarGetManifestLocalUser(user_index, bytes, NULL);
	xsdk_log_write("avatar: user %u (%.16s): description 0x%08X", (unsigned)user_index, info.name, (unsigned)result);
	return result == 0 ? XSDK_AVATAR_OK : XSDK_AVATAR_ERROR_NOT_FOUND;
}

void avatar_content_close(void)
{
	for (uint32_t a = 0; a < animation_count; a++)
		xsdk_avatar_animation_free(&animations[a]);
	memset(animations, 0, sizeof animations);
	animation_count = 0;
	if (!pack)
		return;
	xsdk_avatar_pack_close(pack);
	(void)NtClose(pack_file);
	pack = NULL;
}

uint32_t xsdk_avatar_stock_animation_count(void)
{
	const XsdkAvatarPack *p;
	return avatar_content_pack(&p) == XSDK_AVATAR_OK ? animation_count : 0;
}

const char *xsdk_avatar_stock_animation_name(uint32_t index)
{
	return index < xsdk_avatar_stock_animation_count() ? xsdk_avatar_pack_name(pack, animation_records[index]) : "";
}

int xsdk_avatar_stock_animation(const char *name, const XsdkAvatarAnimation **animation)
{
	const XsdkAvatarPack *p;
	int result = avatar_content_pack(&p);
	if (result != XSDK_AVATAR_OK)
		return result;
	for (uint32_t a = 0; a < animation_count; a++) {
		if (strcmp(xsdk_avatar_pack_name(pack, animation_records[a]), name) != 0)
			continue;
		if (!animations[a].frame_count) {
			uint32_t size;
			void *strb;
			result = avatar_content_read_stock(animation_records[a], &strb, &size);
			if (result != XSDK_AVATAR_OK)
				return result;
			result = xsdk_avatar_animation_decode(&animations[a], strb, size);
			free(strb);
			if (result != XSDK_AVATAR_OK) {
				xsdk_log_write("avatar: animation %s: %d", name, result);
				return result;
			}
			xsdk_log_write("avatar: animation %s: %u frames, %.2f s", name, (unsigned)animations[a].frame_count,
				(double)xsdk_avatar_animation_length(&animations[a]));
		}
		*animation = &animations[a];
		return XSDK_AVATAR_OK;
	}
	xsdk_log_write("avatar: no stock animation named %s", name);
	return XSDK_AVATAR_ERROR_NOT_FOUND;
}
