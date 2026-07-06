// stream_map.cpp — Stream identity map with PROTECTED flag stripping.
//
// Built during configureStreams and consulted in processCaptureResult.
// Each camera3_stream_t pointer is classified by format/size into a role
// that determines whether and how we inject synthetic frames.

#include "stream_map.h"
#include "include/camera3_compat.h"

#include <android/log.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#define TAG "amkush/stream_map"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)

#define MAX_STREAMS 16

typedef struct {
    const camera3_stream_t *ptr;
    StreamRole              role;
} StreamEntry;

static StreamEntry      g_map[MAX_STREAMS];
static uint32_t         g_count = 0;
static pthread_rwlock_t g_lock  = PTHREAD_RWLOCK_INITIALIZER;

static StreamRole classify_stream(const camera3_stream_t *s, uint32_t impl_idx) {
    int      fmt = s->format;
    uint32_t w   = s->width;

    if (fmt == HAL_PIXEL_FORMAT_RAW16)     return STREAM_ROLE_RAW;
    if (fmt == HAL_PIXEL_FORMAT_YCBCR_P010) return STREAM_ROLE_HDR;

    if (fmt == HAL_PIXEL_FORMAT_BLOB) {
        return (w > 2000) ? STREAM_ROLE_SNAPSHOT : STREAM_ROLE_ML_THUMB;
    }
    if (fmt == HAL_PIXEL_FORMAT_YCBCR_420_888) {
        return (w <= 640) ? STREAM_ROLE_YUV_ANALYSIS : STREAM_ROLE_PREVIEW;
    }
    if (fmt == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        return (impl_idx == 0) ? STREAM_ROLE_PREVIEW : STREAM_ROLE_VIDEO;
    }
    return STREAM_ROLE_UNKNOWN;
}

void stream_map_rebuild(camera3_stream_configuration_t *cfg) {
    if (!cfg) return;

    pthread_rwlock_wrlock(&g_lock);

    memset(g_map, 0, sizeof(g_map));
    g_count = 0;

    uint32_t impl_defined_idx = 0;

    for (uint32_t i = 0; i < cfg->num_streams && i < MAX_STREAMS; i++) {
        camera3_stream_t *s = cfg->streams[i];
        if (!s) continue;

        // Strip GRALLOC_USAGE_PROTECTED before buffers are allocated so the
        // gralloc HAL allocates regular (CPU-mappable) backing memory.
        if (s->usage & GRALLOC_USAGE_PROTECTED) {
            LOGW("Stripping GRALLOC_USAGE_PROTECTED from stream[%u] "
                 "fmt=0x%x %ux%u", i, s->format, s->width, s->height);
            s->usage &= ~GRALLOC_USAGE_PROTECTED;
            s->usage |=  GRALLOC_USAGE_SW_WRITE_OFTEN;
        }

        uint32_t classify_idx = (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)
                                ? impl_defined_idx++ : i;

        StreamRole role = classify_stream(s, classify_idx);

        g_map[g_count].ptr  = s;
        g_map[g_count].role = role;
        g_count++;

        LOGI("stream_map[%u] ptr=%p fmt=0x%02x %ux%u usage=0x%x → role=%d",
             i, (void *)s, s->format, s->width, s->height, s->usage, (int)role);
    }

    pthread_rwlock_unlock(&g_lock);
}

void stream_map_clear(void) {
    pthread_rwlock_wrlock(&g_lock);
    memset(g_map, 0, sizeof(g_map));
    g_count = 0;
    pthread_rwlock_unlock(&g_lock);
}

StreamRole stream_map_get_role(const camera3_stream_t *stream) {
    if (!stream) return STREAM_ROLE_UNKNOWN;

    pthread_rwlock_rdlock(&g_lock);
    StreamRole role = STREAM_ROLE_UNKNOWN;
    for (uint32_t i = 0; i < g_count; i++) {
        if (g_map[i].ptr == stream) {
            role = g_map[i].role;
            break;
        }
    }
    pthread_rwlock_unlock(&g_lock);
    return role;
}

bool stream_map_should_inject(StreamRole role, int buffer_status) {
    if (buffer_status != CAMERA3_BUFFER_STATUS_OK) return false;
    if (role == STREAM_ROLE_RAW)     return false;
    if (role == STREAM_ROLE_UNKNOWN) return false;
    return true;
}
