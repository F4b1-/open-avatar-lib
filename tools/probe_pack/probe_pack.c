// Copies the system's avatar asset pack off the console, where FTP can't open it (SPECS.md, section 4.3).
//
// 1. Opens the package FFFE07DF00000002 on the system extended partition, sharing everything, and copies it to
//    game:\FFFE07DF00000002.
// 2. If that fails, lists the kernel's object directories ("\", "\Device", "\??") with each symbolic link's target,
//    and tries "<entry>\AvatarAssetPack.toc" under each entry, in case XAM has the package mounted; the first that
//    opens is copied to game:\AvatarAssetPack.toc.
//
// Everything goes to the log (game:\probe_pack.log). A step is logged before its call, so a crash shows where.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xsdk/kernel.h"
#include "xsdk/log.h"

// The original Xbox kernel's (nxdk lib/xboxkrnl/xboxkrnl.h:1300-1304, 2842-2964); unconfirmed on the Xbox 360.
typedef struct {
	ANSI_STRING name;
	uint32_t type;
} OBJECT_DIRECTORY_INFORMATION;

NTSTATUS NtOpenDirectoryObject(HANDLE *directory, const OBJECT_ATTRIBUTES *attributes);
NTSTATUS NtQueryDirectoryObject(HANDLE directory, void *buffer, uint32_t length_bytes, uint32_t restart_scan,
	uint32_t *context, uint32_t *returned_bytes);
NTSTATUS NtOpenSymbolicLinkObject(HANDLE *link, const OBJECT_ATTRIBUTES *attributes);
NTSTATUS NtQuerySymbolicLinkObject(HANDLE link, ANSI_STRING *target, uint32_t *returned_bytes);

enum {
	// NT's values, which xsdk's kernel.h doesn't list.
	FILE_SHARE_DELETE = 0x00000004,
	STATUS_NO_MORE_ENTRIES = (int32_t)0x8000001A,
	MAX_ENTRIES = 256,
	NAME_BYTES = 128,
	PATH_BYTES = 320,
	COPY_CHUNK_BYTES = 64 * 1024,
};

static const char *const package_folders[] = {
	// dash.xex names its own path \SEP\20449700\dash.xex (its XEX header, references/SysExt).
	"\\SEP\\Content\\0000000000000000\\FFFE07DF\\00008000",
	"\\Device\\Harddisk0\\SystemExtPartition\\Content\\0000000000000000\\FFFE07DF\\00008000",
};
static const char package_name[] = "FFFE07DF00000002";
static const char pack_file_name[] = "AvatarAssetPack.toc";
static const char *const object_directories[] = { "\\", "\\Device", "\\??" };

typedef struct {
	char path[PATH_BYTES];
} Entry;

static Entry entries[MAX_ENTRIES];
static uint32_t entry_count;
static uint8_t copy_buffer[COPY_CHUNK_BYTES];
static uint8_t query_buffer[1024];

static ANSI_STRING ansi(const char *text)
{
	const uint16_t length = (uint16_t)strlen(text);
	return (ANSI_STRING){ .length = length, .maximum_length = (uint16_t)(length + 1), .pointer = text };
}

static NTSTATUS open_file(HANDLE *handle, HANDLE root, const char *path, uint32_t access, uint32_t options)
{
	const ANSI_STRING name = ansi(path);
	const OBJECT_ATTRIBUTES attributes = { .root_directory = root, .name = &name };
	IO_STATUS_BLOCK io_status;
	return NtCreateFile(handle, access | SYNCHRONIZE, &attributes, &io_status, NULL, FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | options);
}

// Lists a folder's files with their sizes.
static void list_folder(const char *path)
{
	HANDLE folder;
	NTSTATUS status = open_file(&folder, 0, path, GENERIC_READ, FILE_DIRECTORY_FILE);
	xsdk_log_write("list %s: 0x%08X", path, (unsigned)status);
	if (!NT_SUCCESS(status))
		return;
	for (uint32_t restart = 1;; restart = 0) {
		IO_STATUS_BLOCK io_status;
		FILE_DIRECTORY_INFORMATION *info = (FILE_DIRECTORY_INFORMATION *)query_buffer;
		memset(query_buffer, 0, sizeof query_buffer);
		status = NtQueryDirectoryFile(folder, 0, NULL, NULL, &io_status, info, sizeof query_buffer, NULL, restart);
		if (!NT_SUCCESS(status))
			break;
		for (;;) {
			const uint32_t length = info->file_name_length < NAME_BYTES ? info->file_name_length : NAME_BYTES;
			xsdk_log_write("  %-40.*s %10llu bytes, attributes 0x%X", (int)length, info->file_name,
				(unsigned long long)info->end_of_file, (unsigned)info->attributes);
			if (!info->next_entry_offset)
				break;
			info = (FILE_DIRECTORY_INFORMATION *)((uint8_t *)info + info->next_entry_offset);
		}
	}
	(void)NtClose(folder);
}

// Copies an open file to game:\<out_name>.
static NTSTATUS copy_to_game(HANDLE source, const char *out_name)
{
	IO_STATUS_BLOCK io_status;
	FILE_NETWORK_OPEN_INFORMATION info;
	NTSTATUS status =
		NtQueryInformationFile(source, &io_status, &info, sizeof info, FILE_NETWORK_OPEN_INFORMATION_CLASS);
	if (!NT_SUCCESS(status)) {
		xsdk_log_write("copy: size query failed: 0x%08X", (unsigned)status);
		return status;
	}
	const uint64_t size = info.end_of_file;
	char out_path[PATH_BYTES];
	(void)snprintf(out_path, sizeof out_path, "game:\\%s", out_name);
	const ANSI_STRING name = ansi(out_path);
	const OBJECT_ATTRIBUTES attributes = { .root_directory = OB_DOS_DEVICES, .name = &name };
	HANDLE out;
	status = NtCreateFile(&out, GENERIC_WRITE | SYNCHRONIZE, &attributes, &io_status, NULL, FILE_ATTRIBUTE_NORMAL, 0,
		FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (!NT_SUCCESS(status)) {
		xsdk_log_write("copy: creating %s failed: 0x%08X", out_path, (unsigned)status);
		return status;
	}
	uint64_t done = 0;
	while (done < size) {
		const uint32_t chunk = size - done < COPY_CHUNK_BYTES ? (uint32_t)(size - done) : COPY_CHUNK_BYTES;
		const uint64_t offset = done;
		status = NtReadFile(source, 0, NULL, NULL, &io_status, copy_buffer, chunk, &offset);
		if (!NT_SUCCESS(status) || io_status.information != chunk) {
			xsdk_log_write("copy: read at %llu failed: 0x%08X, %u bytes", (unsigned long long)offset,
				(unsigned)status, (unsigned)io_status.information);
			break;
		}
		status = NtWriteFile(out, 0, NULL, NULL, &io_status, copy_buffer, chunk, &offset);
		if (!NT_SUCCESS(status)) {
			xsdk_log_write("copy: write at %llu failed: 0x%08X", (unsigned long long)offset, (unsigned)status);
			break;
		}
		done += chunk;
	}
	(void)NtClose(out);
	xsdk_log_write("copy: %llu of %llu bytes to %s", (unsigned long long)done, (unsigned long long)size, out_path);
	return done == size ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

// Step 1: the package itself.
static bool copy_package(void)
{
	for (size_t i = 0; i < sizeof package_folders / sizeof package_folders[0]; i++) {
		list_folder(package_folders[i]);
		char path[PATH_BYTES];
		(void)snprintf(path, sizeof path, "%s\\%s", package_folders[i], package_name);
		HANDLE file;
		const NTSTATUS status = open_file(&file, 0, path, GENERIC_READ, FILE_NON_DIRECTORY_FILE);
		xsdk_log_write("open %s: 0x%08X", path, (unsigned)status);
		if (!NT_SUCCESS(status))
			continue;
		const NTSTATUS copied = copy_to_game(file, package_name);
		(void)NtClose(file);
		if (NT_SUCCESS(copied))
			return true;
	}
	return false;
}

static void log_link_target(HANDLE directory, const char *name)
{
	const ANSI_STRING link_name = ansi(name);
	const OBJECT_ATTRIBUTES attributes = { .root_directory = directory, .name = &link_name };
	HANDLE link;
	NTSTATUS status = NtOpenSymbolicLinkObject(&link, &attributes);
	if (!NT_SUCCESS(status)) {
		xsdk_log_write("    not a link: 0x%08X", (unsigned)status);
		return;
	}
	static char target_text[PATH_BYTES];
	memset(target_text, 0, sizeof target_text);
	ANSI_STRING target = { .length = 0, .maximum_length = sizeof target_text - 1, .pointer = target_text };
	uint32_t returned = 0;
	status = NtQuerySymbolicLinkObject(link, &target, &returned);
	if (NT_SUCCESS(status))
		xsdk_log_write("    -> %.*s", (int)(target.length < PATH_BYTES - 1 ? target.length : 0), target_text);
	else
		xsdk_log_write("    link query failed: 0x%08X", (unsigned)status);
	(void)NtClose(link);
}

// Step 2a: lists an object directory, with link targets, and remembers its entries.
static void list_object_directory(const char *path)
{
	const ANSI_STRING name = ansi(path);
	const OBJECT_ATTRIBUTES attributes = { .root_directory = 0, .name = &name };
	HANDLE directory;
	xsdk_log_write("objects %s: opening", path);
	NTSTATUS status = NtOpenDirectoryObject(&directory, &attributes);
	xsdk_log_write("objects %s: 0x%08X", path, (unsigned)status);
	if (!NT_SUCCESS(status))
		return;
	uint32_t context = 0;
	for (uint32_t n = 0; n < MAX_ENTRIES; n++) {
		memset(query_buffer, 0, sizeof query_buffer);
		uint32_t returned = 0;
		status = NtQueryDirectoryObject(directory, query_buffer, sizeof query_buffer, n == 0, &context, &returned);
		if (status == STATUS_NO_MORE_ENTRIES)
			break;
		if (!NT_SUCCESS(status)) {
			xsdk_log_write("  query failed: 0x%08X", (unsigned)status);
			break;
		}
		const OBJECT_DIRECTORY_INFORMATION *info = (const OBJECT_DIRECTORY_INFORMATION *)query_buffer;
		const uintptr_t text = (uintptr_t)info->name.pointer;
		const bool in_buffer = text >= (uintptr_t)query_buffer && text + info->name.length <=
			(uintptr_t)query_buffer + sizeof query_buffer;
		if (!in_buffer || info->name.length == 0 || info->name.length >= NAME_BYTES) {
			const uint32_t *words = (const uint32_t *)query_buffer;
			xsdk_log_write("  unexpected entry: %08X %08X %08X %08X, returned %u", (unsigned)words[0],
				(unsigned)words[1], (unsigned)words[2], (unsigned)words[3], (unsigned)returned);
			break;
		}
		char entry_name[NAME_BYTES];
		memcpy(entry_name, info->name.pointer, info->name.length);
		entry_name[info->name.length] = '\0';
		const uint32_t type = info->type;
		xsdk_log_write("  %-32s type %08X", entry_name, (unsigned)type);
		log_link_target(directory, entry_name);
		if (entry_count < MAX_ENTRIES) {
			Entry *entry = &entries[entry_count++];
			(void)snprintf(entry->path, sizeof entry->path, "%s%s%s", path, strcmp(path, "\\") ? "\\" : "",
				entry_name);
		}
	}
	(void)NtClose(directory);
}

// Step 2b: tries the pack's file name under every entry found.
static bool copy_from_mount(void)
{
	for (uint32_t i = 0; i < entry_count; i++) {
		char path[PATH_BYTES];
		(void)snprintf(path, sizeof path, "%s\\%s", entries[i].path, pack_file_name);
		HANDLE file;
		const NTSTATUS status = open_file(&file, 0, path, GENERIC_READ, FILE_NON_DIRECTORY_FILE);
		if (status != STATUS_OBJECT_NAME_NOT_FOUND && status != STATUS_OBJECT_PATH_NOT_FOUND)
			xsdk_log_write("open %s: 0x%08X", path, (unsigned)status);
		if (!NT_SUCCESS(status))
			continue;
		const NTSTATUS copied = copy_to_game(file, pack_file_name);
		(void)NtClose(file);
		if (NT_SUCCESS(copied))
			return true;
	}
	return false;
}

int main(void)
{
	xsdk_log_write("probe_pack: step 1, the package");
	const bool got_package = copy_package();
	xsdk_log_write("probe_pack: step 1 %s", got_package ? "copied the package" : "failed");

	// Listed even after step 1 works: the library has to find the pack the same way at run time.
	xsdk_log_write("probe_pack: step 2, object directories");
	for (size_t i = 0; i < sizeof object_directories / sizeof object_directories[0]; i++)
		list_object_directory(object_directories[i]);
	xsdk_log_write("probe_pack: %u entries; trying %s under each", (unsigned)entry_count, pack_file_name);
	const bool got_toc = copy_from_mount();
	xsdk_log_write("probe_pack: step 2 %s", got_toc ? "copied the .toc from a mount" : "found no mount");

	xsdk_log_write("probe_pack: done");
	return 0;
}
