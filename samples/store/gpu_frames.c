// The GPU's frames: recordings and time (gpu_frames.h).

#include "gpu_frames.h"

#include <string.h>

#include "xsdk/kernel.h"
#include "xsdk/log.h"

enum {
	RECORD_DWORDS = 256 * 1024, // a command buffer: room for ten avatars, measured at about 8,000 dwords each
	FENCE_TIMEOUT_MS = 2000,
};

static const double clock_ticks_per_us = 498.75; // the GPU clock's rate (xsdk/gpu.h)

static volatile uint32_t *slot_words(const GpuFrames *g)
{
	return g->clock_words + g->slot * 2 * XSDK_GPU_CLOCK_WORDS;
}

bool gpu_frames_open(GpuFrames *g)
{
	memset(g, 0, sizeof *g);
	g->can_record = true;
	for (uint32_t s = 0; s < GPU_FRAME_SLOTS; s++)
		g->can_record = NT_SUCCESS(xsdk_gpu_command_buffer_create(&g->buffers[s], RECORD_DWORDS)) && g->can_record;
	g->clock_words = xsdk_gpu_alloc(GPU_FRAME_SLOTS * 2 * XSDK_GPU_CLOCK_WORDS * 4);
	return g->clock_words != NULL;
}

void gpu_frames_close(GpuFrames *g)
{
	for (uint32_t s = 0; s < GPU_FRAME_SLOTS; s++)
		xsdk_gpu_command_buffer_free(&g->buffers[s]);
	if (g->clock_words)
		xsdk_gpu_free(g->clock_words);
	memset(g, 0, sizeof *g);
}

void gpu_frames_next(GpuFrames *g)
{
	g->slot = (g->slot + 1) % GPU_FRAME_SLOTS;
	if (g->fences[g->slot] && !NT_SUCCESS(xsdk_gpu_wait(g->fences[g->slot], FENCE_TIMEOUT_MS)))
		xsdk_log_write("store: slot %u's fence didn't land", (unsigned)g->slot);
	if (g->timed[g->slot]) {
		const volatile uint32_t *words = slot_words(g);
		g->ticks += xsdk_gpu_clock_value(words + XSDK_GPU_CLOCK_WORDS) - xsdk_gpu_clock_value(words);
		g->frames++;
		g->timed[g->slot] = false;
	}
}

const XsdkCommandBuffer *gpu_frames_record(GpuFrames *g, void (*draw)(void *), void *context)
{
	if (!g->can_record)
		return NULL;
	XsdkCommandBuffer *buffer = &g->buffers[g->slot];
	xsdk_gpu_record_begin(buffer);
	draw(context);
	if (!NT_SUCCESS(xsdk_gpu_record_end())) {
		xsdk_log_write("store: the recording outgrew its buffer");
		return NULL;
	}
	g->recorded = buffer->count;
	return buffer;
}

void gpu_frames_clock_before(GpuFrames *g)
{
	xsdk_gpu_write_clock(xsdk_gpu_address(slot_words(g)), true);
}

void gpu_frames_clock_after(GpuFrames *g)
{
	xsdk_gpu_write_clock(xsdk_gpu_address(slot_words(g) + XSDK_GPU_CLOCK_WORDS), true);
}

void gpu_frames_fence(GpuFrames *g, bool timed)
{
	g->fences[g->slot] = xsdk_gpu_fence();
	g->timed[g->slot] = timed;
}

double gpu_frames_take_us(GpuFrames *g)
{
	const double us = g->frames ? (double)g->ticks / clock_ticks_per_us / g->frames : 0.0;
	g->ticks = 0;
	g->frames = 0;
	return us;
}
