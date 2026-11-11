#ifndef STORE_GPU_FRAMES_H
#define STORE_GPU_FRAMES_H

// What the GPU does with each frame, kept per frame in flight (a slot each): the avatars' draws recorded into a
// command buffer ahead of the frame (RB in the store: avatar.h promises that drawing works inside a recording), and
// the GPU's time for the frame's bands, read from clock words written before and after them once the frame's fence
// has landed. Moved from the viewer sample when it was retired (milestone 12).

#include <stdbool.h>
#include <stdint.h>

#include "xsdk/gpu.h"

enum {
	GPU_FRAME_SLOTS = 2,
};

typedef struct {
	XsdkCommandBuffer buffers[GPU_FRAME_SLOTS];
	bool can_record;
	volatile uint32_t *clock_words; // per slot: the clock before and after the bands
	uint32_t fences[GPU_FRAME_SLOTS];
	bool timed[GPU_FRAME_SLOTS];
	uint32_t slot;
	uint64_t ticks;    // GPU time of the frames read back so far
	uint32_t frames;   // how many
	uint32_t recorded; // dwords in the last recording
} GpuFrames;

// After xsdk_gpu_open. False if the clock words can't be had (a recording that can't be made only turns RB off).
bool gpu_frames_open(GpuFrames *g);
// After a fence past the last frame.
void gpu_frames_close(GpuFrames *g);
// The next frame's slot, once the GPU is past the frame that last used it; that frame's GPU time added up.
void gpu_frames_next(GpuFrames *g);
// Records draw(context) into the slot's command buffer; NULL if it can't or the recording outgrew it.
const XsdkCommandBuffer *gpu_frames_record(GpuFrames *g, void (*draw)(void *), void *context);
// Around the frame's bands, after xsdk_present_begin: the clock before; then after them the clock again, and the
// frame's fence after xsdk_present_end (timed false leaves this frame out of the time: a screenshot's, say).
void gpu_frames_clock_before(GpuFrames *g);
void gpu_frames_clock_after(GpuFrames *g);
void gpu_frames_fence(GpuFrames *g, bool timed);
// The GPU's microseconds a frame since the last call, and starts counting again.
double gpu_frames_take_us(GpuFrames *g);

#endif
