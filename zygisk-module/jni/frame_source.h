#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FRAME_SOURCE_MAGIC  0xAB7C5801U
#define FRAME_RING_SLOTS    3

typedef struct {
    uint32_t magic;
    volatile uint32_t write_slot;
    uint32_t master_width;
    uint32_t master_height;
    uint32_t slot_stride;
    uint8_t  _pad[44];
} FrameSourceHeader;

typedef struct {
    uint8_t  *y_plane;
    uint8_t  *uv_plane;
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;
} FrameData;

#ifdef __cplusplus
extern "C" {
#endif

int  frame_source_init(int ashmem_fd);
void frame_source_destroy(void);
bool frame_source_ready(void);
bool frame_source_get(FrameData *out);

#ifdef __cplusplus
}
#endif
