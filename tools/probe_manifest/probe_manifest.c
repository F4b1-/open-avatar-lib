// Reads each signed-in profile's avatar description (1000 bytes; SPECS.md, section 5.1) two ways and logs it:
// 1. XamAvatarGetManifestLocalUser(user, buffer, NULL);
// 2. XamUserReadProfileSettings for XPROFILE_GAMERCARD_AVATAR_INFO_1 (0x63E80044), with title 0 and FFFE07D1.
// Each result that comes back is also written to game:\manifest_u<user>_<method>.bin. Reads only.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xsdk/kernel.h"
#include "xsdk/log.h"

enum {
	USERS = 4,
	MANIFEST_BYTES = 1000,
	SETTING_AVATAR_INFO_1 = 0x63E80044, // xenia:src/xenia/kernel/xam/user_settings.h:279
	DASHBOARD_TITLE = 0xFFFE07D1,
	SETTINGS_BUFFER_BYTES = 4096,
};

// XUSER_SIGNIN_INFO (xenia:src/xenia/kernel/xam/xam_user.cc), as Avatar Thrift Store declares it.
typedef struct {
	uint64_t xuid;
	uint32_t flags;
	uint32_t signin_state; // 0 = not signed in
	uint32_t guest_number;
	uint32_t sponsor_user_index;
	char name[16];
} SigninInfo;

uint32_t XamUserGetSigninInfo(uint32_t user_index, uint32_t flags, SigninInfo *info);
// xenia:src/xenia/kernel/xam/xam_avatar.cc:49-111; synchronous with no overlapped.
uint32_t XamAvatarGetManifestLocalUser(uint32_t user_index, void *manifest, void *overlapped);
// xenia:src/xenia/kernel/xam/xam_user.cc:334-342
uint32_t XamUserReadProfileSettings(uint32_t title_id, uint32_t user_index, uint32_t xuid_count, const uint64_t *xuids,
	uint32_t setting_count, const uint32_t *setting_ids, uint32_t *buffer_bytes, void *buffer, void *overlapped);

static uint8_t manifest[MANIFEST_BYTES];
static uint8_t settings[SETTINGS_BUFFER_BYTES];

static void save(const char *name, const void *data, uint32_t bytes)
{
	char path[64];
	snprintf(path, sizeof path, "game:\\%s", name);
	const ANSI_STRING string = { .length = (uint16_t)strlen(path),
		.maximum_length = (uint16_t)(strlen(path) + 1),
		.pointer = path };
	const OBJECT_ATTRIBUTES attributes = { .root_directory = OB_DOS_DEVICES, .name = &string };
	IO_STATUS_BLOCK io_status;
	HANDLE file;
	NTSTATUS status = NtCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE, &attributes, &io_status, NULL,
		FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (NT_SUCCESS(status)) {
		status = NtWriteFile(file, 0, NULL, NULL, &io_status, data, bytes, NULL);
		(void)NtClose(file);
	}
	xsdk_log_write("  saved %s: 0x%08X", path, (unsigned)status);
}

static void hex_lines(const char *label, const uint8_t *data, uint32_t from, uint32_t bytes)
{
	char line[3 * 32 + 1];
	for (uint32_t o = from; o < from + bytes; o += 32) {
		uint32_t n = 0;
		for (uint32_t i = o; i < o + 32 && i < from + bytes; i++)
			n += (uint32_t)snprintf(line + n, sizeof line - n, "%02X ", data[i]);
		xsdk_log_write("  %s +%03X: %s", label, (unsigned)o, line);
	}
}

// The fields All-Stars Avatar's avatar_desc.py found (SPECS.md, section 5.1), as hex to decode on the PC.
static void log_manifest(const uint8_t *m)
{
	uint32_t a, b;
	memcpy(&a, m + 4, 4);
	memcpy(&b, m + 8, 4);
	xsdk_log_write("  word 0 %08X, floats at 4 and 8: %08X %08X", (unsigned)(m[0] << 24 | m[1] << 16 | m[2] << 8 | m[3]),
		(unsigned)a, (unsigned)b);
	hex_lines("features", m, 0x00C, 15 * 16);
	hex_lines("colours", m, 0x0FC, 9 * 4);
	hex_lines("items", m, 0x120, 15 * 32);
	hex_lines("items B", m, 0x300, 4 * 32);
	hex_lines("rest", m, 0x380, MANIFEST_BYTES - 0x380);
}

static void by_manifest_call(uint32_t user)
{
	memset(manifest, 0xCD, sizeof manifest);
	xsdk_log_write("user %u: XamAvatarGetManifestLocalUser", (unsigned)user);
	const uint32_t result = XamAvatarGetManifestLocalUser(user, manifest, NULL);
	xsdk_log_write("user %u: XamAvatarGetManifestLocalUser returned 0x%08X", (unsigned)user, (unsigned)result);
	bool changed = false;
	for (uint32_t i = 0; i < sizeof manifest; i++)
		changed |= manifest[i] != 0xCD;
	if (!changed) {
		xsdk_log_write("  the buffer was left untouched");
		return;
	}
	log_manifest(manifest);
	char name[32];
	snprintf(name, sizeof name, "manifest_u%u_avatar.bin", (unsigned)user);
	save(name, manifest, sizeof manifest);
}

static void by_profile_setting(uint32_t user, uint32_t title)
{
	memset(settings, 0, sizeof settings);
	const uint32_t id = SETTING_AVATAR_INFO_1;
	uint32_t bytes = sizeof settings;
	xsdk_log_write("user %u: XamUserReadProfileSettings, title %08X", (unsigned)user, (unsigned)title);
	const uint32_t result = XamUserReadProfileSettings(title, user, 0, NULL, 1, &id, &bytes, settings, NULL);
	xsdk_log_write("user %u: XamUserReadProfileSettings returned 0x%08X, size %u", (unsigned)user, (unsigned)result,
		(unsigned)bytes);
	if (result != 0)
		return;
	// The header and the setting record first, then the data they point to (xenia: X_USER_READ_PROFILE_SETTINGS).
	hex_lines("header", settings, 0, 64);
	char name[40];
	snprintf(name, sizeof name, "manifest_u%u_setting_%08X.bin", (unsigned)user, (unsigned)title);
	save(name, settings, bytes < sizeof settings ? bytes : sizeof settings);
}

int main(void)
{
	xsdk_log_write("probe_manifest: the signed-in profiles' avatar descriptions");
	uint32_t signed_in = 0;
	for (uint32_t user = 0; user < USERS; user++) {
		SigninInfo info;
		memset(&info, 0, sizeof info);
		const uint32_t result = XamUserGetSigninInfo(user, 1, &info);
		if (result != 0 || info.signin_state == 0) {
			xsdk_log_write("user %u: not signed in (0x%08X)", (unsigned)user, (unsigned)result);
			continue;
		}
		signed_in++;
		xsdk_log_write("user %u: %.16s, state %u", (unsigned)user, info.name, (unsigned)info.signin_state);
		by_manifest_call(user);
		by_profile_setting(user, 0);
		by_profile_setting(user, DASHBOARD_TITLE);
	}
	if (!signed_in)
		xsdk_log_write("probe_manifest: nobody is signed in; sign a profile in and run it again");
	xsdk_log_write("probe_manifest: done");
	return 0;
}
