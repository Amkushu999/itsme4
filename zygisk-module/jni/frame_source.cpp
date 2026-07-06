// frame_source.cpp — Ashmem ring buffer consumer (cameraserver side).
//
// frame_source_init() is called by ipc_socket.cpp when the app sends
// the Ashmem fd via SCM_RIGHTS.  The hook reads frames from the ring
// buffer in processCaptureResult via frame_source_get().

#include "frame_source.h"

#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define TAG "amkush/frame_source"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static void             *g_map_base  = MAP_FAILED;
static size_t            g_map_size  = 0;
static FrameSourceHeader *g_hdr      = nullptr;
static uint8_t           *g_slots    = nullptr;
static pthread_mutex_t   g_lock      = PTHREAD_MUTEX_INITIALIZER;

int frame_source_init(int ashmem_fd) {
    if (ashmem_fd < 0) {
        LOGE("Invalid ashmem_fd=%d", ashmem_fd);
        return -1;
    }

    off_t total = lseek(ashmem_fd, 0, SEEK_END);
    lseek(ashmem_fd, 0, SEEK_SET);
    if (total <= 0) {
        LOGE("ashmem_fd has zero or unknown size");
        close(ashmem_fd);
        return -1;
    }

    void *base = mmap(nullptr, (size_t)total, PROT_READ, MAP_SHARED, ashmem_fd, 0);
    close(ashmem_fd);

    if (base == MAP_FAILED) {
        LOGE("mmap failed: %s", strerror(errno));
        return -1;
    }

    FrameSourceHeader *hdr = (FrameSourceHeader *)base;
    if (hdr->magic != FRAME_SOURCE_MAGIC) {
        LOGE("Bad magic 0x%08X (expected 0x%08X)", hdr->magic, FRAME_SOURCE_MAGIC);
        munmap(base, (size_t)total);
        return -1;
    }

    pthread_mutex_lock(&g_lock);

    if (g_map_base != MAP_FAILED) {
        munmap(g_map_base, g_map_size);
    }

    g_map_base = base;
    g_map_size = (size_t)total;
    g_hdr      = hdr;
    g_slots    = (uint8_t *)base + sizeof(FrameSourceHeader);

    pthread_mutex_unlock(&g_lock);

    LOGI("frame_source mapped: %ux%u stride=%u total_bytes=%zu",
         hdr->master_width, hdr->master_height, hdr->slot_stride, g_map_size);
    return 0;
}

void frame_source_destroy(void) {
    pthread_mutex_lock(&g_lock);
    if (g_map_base != MAP_FAILED) {
        munmap(g_map_base, g_map_size);
        g_map_base = MAP_FAILED;
        g_map_size = 0;
        g_hdr      = nullptr;
        g_slots    = nullptr;
    }
    pthread_mutex_unlock(&g_lock);
}

bool frame_source_ready(void) {
    return g_map_base != MAP_FAILED && g_hdr != nullptr;
}

bool frame_source_get(FrameData *out) {
    if (!out || !frame_source_ready()) return false;

    uint32_t ws     = __atomic_load_n(&g_hdr->write_slot, __ATOMIC_ACQUIRE) % FRAME_RING_SLOTS;
    uint32_t w      = g_hdr->master_width;
    uint32_t h      = g_hdr->master_height;
    uint32_t stride = g_hdr->slot_stride;
    size_t   slot_sz = (size_t)stride * h * 3 / 2;
    uint8_t *slot   = g_slots + ws * slot_sz;

    out->y_plane  = slot;
    out->uv_plane = slot + (size_t)stride * h;
    out->width    = w;
    out->height   = h;
    out->stride   = stride;
    return true;
}
