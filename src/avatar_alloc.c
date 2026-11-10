// The library's heap allocations (avatar_internal.h): malloc, calloc and realloc, logging what ran out, and a hook
// that makes one allocation fail on purpose, for tests/alloc_check. Builds for the PC.

#include <stdio.h>
#include <stdlib.h>

#include "avatar_internal.h"

#ifdef __XSDK__
#include "xsdk/log.h"
#define LOG(...) xsdk_log_write(__VA_ARGS__)
#else
#define LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

uint32_t avatar_alloc_fail_at;

// Whether this allocation is the one a test asked to fail.
static bool failing(void)
{
	if (avatar_alloc_fail_at == 0)
		return false;
	return --avatar_alloc_fail_at == 0;
}

static void *logged(void *pointer, size_t bytes, const char *what)
{
	if (!pointer)
		LOG("avatar: out of memory: %s, %lu bytes", what, (unsigned long)bytes);
	return pointer;
}

void *avatar_malloc(size_t bytes, const char *what)
{
	return logged(failing() ? NULL : malloc(bytes), bytes, what);
}

void *avatar_calloc(size_t count, size_t size, const char *what)
{
	return logged(failing() ? NULL : calloc(count, size), count * size, what);
}

void *avatar_realloc(void *pointer, size_t bytes, const char *what)
{
	return logged(failing() ? NULL : realloc(pointer, bytes), bytes, what);
}
