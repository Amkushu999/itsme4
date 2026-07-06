// frame_inject.cpp — Per-surface libyuv frame injection.
//
// AHardwareBuffer API is resolved at runtime via dlopen("libandroid.so") to
// avoid linker namespace errors when building as a standalone Zygisk module.
//
// libyuv is linked statically (or via the NDK prefab package).
// libjpeg-turbo is linked for BLOB/snapshot streams.

#include "frame_inject.h"
#include "include/camera3_compat.h"
#include "frame_source.h"
#include "stream_map.h"

#include <android/log.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <vector>

#include <libyuv.h>
#include <turbojpeg.h>

#define TAG "amkush/frame_inject"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// AHardwareBuffer function pointers — resolved at runtime
// ---------------------------------------------------------------------------
typedef int  (*fn_AHardwareBuffer_lock)(
        AHardwareBuffer *, uint64_t usage, int32_t fence,
        const ARect *fence_bounds, void **out_virtual_address);
typedef int  (*fn_AHardwareBuffer_lockPlanes)(
        AHardwareBuffer *, uint64_t usage, int32_t fence,
        const ARect *fence_bounds, AHardwareBuffer_Planes *out_planes);
typedef int  (*fn_AHardwareBuffer_unlock)(AHardwareBuffer *, int32_t *fence);
typedef void (*fn_AHardwareBuffer_describe)(
        const AHardwareBuffer *, AHardwareBuffer_Desc *out_desc);

static void *g_libandroid      = nullptr;
static fn_AHardwareBuffer_lock        g_lock        = nullptr;
static fn_AHardwareBuffer_lockPlanes  g_lockPlanes  = nullptr;
static fn_AHardwareBuffer_unlock      g_unlock      = nullptr;
static fn_AHardwareBuffer_describe    g_describe    = nullptr;

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------
int frame_inject_init(void) {
    g_libandroid = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_libandroid) {
        g_libandroid = dlopen("libandroid.so", RTLD_NOW);
    }
    if (!g_libandroid) {
        LOGE("Cannot dlopen libandroid.so: %s", dlerror());
        return -1;
    }

    g_lock       = (fn_AHardwareBuffer_lock)
                   dlsym(g_libandroid, "AHardwareBuffer_lock");
    g_lockPlanes = (fn_AHardwareBuffer_lockPlanes)
                   dlsym(g_libandroid, "AHardwareBuffer_lockPlanes");
    g_unlock     = (fn_AHardwareBuffer_unlock)
                   dlsym(g_libandroid, "AHardwareBuffer_unlock");
    g_describe   = (fn_AHardwareBuffer_describe)
                   dlsym(g_libandroid, "AHardwareBuffer_describe");

    if (!g_lock || !g_unlock || !g_describe) {
        LOGE("Missing required AHardwareBuffer symbols");
        return -1;
    }

    if (!g_lockPlanes) {
        LOGW("AHardwareBuffer_lockPlanes unavailable — using plain lock fallback");
    }

    LOGI("frame_inject_init: OK (lockPlanes=%s)", g_lockPlanes ? "yes" : "no");
    return 0;
}

void frame_inject_destroy(void) {
    if (g_libandroid) {
        dlclose(g_libandroid);
        g_libandroid = nullptr;
        g_lock       = nullptr;
        g_lockPlanes = nullptr;
        g_unlock     = nullptr;
        g_describe   = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Helper: convert camera3_stream_buffer_t → AHardwareBuffer*
// ---------------------------------------------------------------------------
static inline AHardwareBuffer *to_ahwb(const camera3_stream_buffer_t *buf) {
    if (!buf || !buf->buffer || !(*buf->buffer)) return nullptr;
    return reinterpret_cast<AHardwareBuffer *>(
               const_cast<native_handle_t *>(*buf->buffer));
}

// ---------------------------------------------------------------------------
// YUV injection (NV12/NV21/I420/YUV_420_888 streams)
// ---------------------------------------------------------------------------
static bool inject_yuv(AHardwareBuffer *hwb,
                        uint32_t dst_w, uint32_t dst_h,
                        const FrameData *src) {
    if (g_lockPlanes) {
        AHardwareBuffer_Planes planes;
        int err = g_lockPlanes(hwb,
                               AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                               -1, nullptr,
                               &planes);
        if (err != 0) {
            LOGE("AHardwareBuffer_lockPlanes failed: %d", err);
            return false;
        }

        if (planes.planeCount < 2) {
            g_unlock(hwb, nullptr);
            return false;
        }

        uint8_t *dst_y      = (uint8_t *)planes.planes[0].data;
        int      dst_stride = (int)planes.planes[0].rowStride;

        if (planes.planeCount == 2) {
            // NV12 / NV21 (interleaved UV)
            uint8_t *dst_uv      = (uint8_t *)planes.planes[1].data;
            int      dst_ustride = (int)planes.planes[1].rowStride;

            libyuv::NV12Scale(
                src->y_plane,  (int)src->stride,
                src->uv_plane, (int)src->stride,
                (int)src->width, (int)src->height,
                dst_y,  dst_stride,
                dst_uv, dst_ustride,
                (int)dst_w, (int)dst_h,
                libyuv::kFilterLinear);
        } else {
            // I420 / YV12 — separate U and V planes.
            // Convert NV12 → I420 first (separate temp Y + U + V buffers),
            // then scale I420 → destination.
            uint8_t *dst_u  = (uint8_t *)planes.planes[1].data;
            uint8_t *dst_v  = (uint8_t *)planes.planes[2].data;
            int ustride = (int)planes.planes[1].rowStride;
            int vstride = (int)planes.planes[2].rowStride;

            int src_w = (int)src->width, src_h = (int)src->height;
            int uv_w  = (src_w + 1) / 2, uv_h = (src_h + 1) / 2;

            // Allocate separate temp buffers; never alias src pointers.
            std::vector<uint8_t> tmp_y((size_t)src_w * src_h);
            std::vector<uint8_t> tmp_u((size_t)uv_w  * uv_h);
            std::vector<uint8_t> tmp_v((size_t)uv_w  * uv_h);

            libyuv::NV12ToI420(
                src->y_plane,  (int)src->stride,
                src->uv_plane, (int)src->stride,
                tmp_y.data(),  src_w,   // dst Y (separate buffer — no aliasing)
                tmp_u.data(),  uv_w,
                tmp_v.data(),  uv_w,
                src_w, src_h);

            libyuv::I420Scale(
                tmp_y.data(), src_w,
                tmp_u.data(), uv_w,
                tmp_v.data(), uv_w,
                src_w, src_h,
                dst_y, dst_stride,
                dst_u, ustride,
                dst_v, vstride,
                (int)dst_w, (int)dst_h,
                libyuv::kFilterLinear);
        }

        g_unlock(hwb, nullptr);
        return true;

    } else {
        // Fallback path: plain lock — assume NV12 layout.
        void *vaddr = nullptr;
        int err = g_lock(hwb,
                         AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                         -1, nullptr,
                         &vaddr);
        if (err != 0 || !vaddr) {
            LOGE("AHardwareBuffer_lock failed: %d", err);
            return false;
        }

        AHardwareBuffer_Desc desc;
        g_describe(hwb, &desc);
        int stride = (int)desc.stride;

        uint8_t *dst_y  = (uint8_t *)vaddr;
        uint8_t *dst_uv = dst_y + (size_t)stride * dst_h;

        libyuv::NV12Scale(
            src->y_plane,  (int)src->stride,
            src->uv_plane, (int)src->stride,
            (int)src->width, (int)src->height,
            dst_y,  stride,
            dst_uv, stride,
            (int)dst_w, (int)dst_h,
            libyuv::kFilterLinear);

        g_unlock(hwb, nullptr);
        return true;
    }
}

// ---------------------------------------------------------------------------
// JPEG injection (BLOB/snapshot streams)
// ---------------------------------------------------------------------------
static bool inject_jpeg(AHardwareBuffer *hwb,
                         uint32_t dst_w, uint32_t dst_h,
                         const FrameData *src) {
    size_t scaled_y_sz  = (size_t)dst_w * dst_h;
    size_t scaled_uv_sz = scaled_y_sz / 2;
    std::vector<uint8_t> scaled_y(scaled_y_sz);
    std::vector<uint8_t> scaled_uv(scaled_uv_sz);

    libyuv::NV12Scale(
        src->y_plane,  (int)src->stride,
        src->uv_plane, (int)src->stride,
        (int)src->width, (int)src->height,
        scaled_y.data(),  (int)dst_w,
        scaled_uv.data(), (int)dst_w,
        (int)dst_w, (int)dst_h,
        libyuv::kFilterLinear);

    tjhandle tj = tjInitCompress();
    if (!tj) {
        LOGE("tjInitCompress failed");
        return false;
    }

    unsigned char *jpeg_buf = nullptr;
    unsigned long  jpeg_sz  = 0;
    const unsigned char *planes[2] = { scaled_y.data(), scaled_uv.data() };
    int strides[2] = { (int)dst_w, (int)dst_w };

    int rc = tjCompressFromYUVPlanes(tj,
                                     planes, (int)dst_w, strides, (int)dst_h,
                                     TJSAMP_420,
                                     &jpeg_buf, &jpeg_sz,
                                     85,
                                     TJFLAG_FASTDCT | TJFLAG_NOREALLOC);
    tjDestroy(tj);

    if (rc != 0) {
        LOGE("tjCompressFromYUVPlanes failed: %s", tjGetErrorStr2(nullptr));
        if (jpeg_buf) tjFree(jpeg_buf);
        return false;
    }

    void *vaddr = nullptr;
    int err = g_lock(hwb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &vaddr);
    if (err != 0 || !vaddr) {
        LOGE("lock for JPEG write failed: %d", err);
        tjFree(jpeg_buf);
        return false;
    }

    AHardwareBuffer_Desc desc;
    g_describe(hwb, &desc);
    size_t blob_size = (size_t)desc.stride;

    if (jpeg_sz <= blob_size - sizeof(uint32_t) * 2) {
        memcpy(vaddr, jpeg_buf, jpeg_sz);
        struct { uint16_t id; uint32_t size; } __attribute__((packed)) trailer;
        trailer.id   = 0x00FF;
        trailer.size = (uint32_t)jpeg_sz;
        uint8_t *trailer_ptr = (uint8_t *)vaddr + blob_size - sizeof(trailer);
        memcpy(trailer_ptr, &trailer, sizeof(trailer));
    } else {
        LOGW("JPEG too large (%lu) for blob buffer (%zu) — skipping", jpeg_sz, blob_size);
    }

    g_unlock(hwb, nullptr);
    tjFree(jpeg_buf);
    return true;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
bool frame_inject_one(const camera3_stream_buffer_t *buf,
                       StreamRole                     role,
                       const FrameData               *src) {
    // Guard: require that frame_inject_init() has been called successfully.
    if (!buf || !src || !g_libandroid) return false;

    // Wait out the acquire fence (if any) — max 10 ms.
    if (buf->acquire_fence >= 0) {
        extern int sync_wait(int fd, int timeout);
        if (sync_wait(buf->acquire_fence, 10) != 0) {
            LOGW("acquire_fence %d timed out — skipping frame", buf->acquire_fence);
            return false;
        }
    }

    AHardwareBuffer *hwb = to_ahwb(buf);
    if (!hwb) {
        LOGE("Cannot resolve AHardwareBuffer from camera3_stream_buffer");
        return false;
    }

    uint32_t dst_w = buf->stream->width;
    uint32_t dst_h = buf->stream->height;
    int      fmt   = buf->stream->format;

    switch (fmt) {
        case HAL_PIXEL_FORMAT_BLOB:
            return inject_jpeg(hwb, dst_w, dst_h, src);

        case HAL_PIXEL_FORMAT_RAW16:
            return false;

        case HAL_PIXEL_FORMAT_YCBCR_P010:
            LOGW("P010/HDR stream — injection not supported, skipping");
            return false;

        case HAL_PIXEL_FORMAT_YCBCR_420_888:
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        default:
            return inject_yuv(hwb, dst_w, dst_h, src);
    }
}
