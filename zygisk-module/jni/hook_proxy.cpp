// hook_proxy.cpp — Camera hook implementations.
//
// ┌──────────────────────────────────────────────────────────────────────────┐
// │  FRAME INJECTION PIPELINE                                                 │
// │                                                                           │
// │  App (FFmpeg) ──► Ashmem ring buffer ──► IPC socket ──►                  │
// │  ┌─────────────────────────────────────────────────────────┐              │
// │  │ processCaptureResult hook (running in cameraserver)     │              │
// │  │   for each output_buffer in result:                     │              │
// │  │     role = stream_map_get_role(stream)                  │              │
// │  │     if should_inject:                                   │              │
// │  │       frame = frame_source_get()                        │              │
// │  │       frame_inject_one(buffer, role, frame)             │              │
// │  │   call original_processCaptureResult(result)            │              │
// │  └─────────────────────────────────────────────────────────┘              │
// └──────────────────────────────────────────────────────────────────────────┘

#include "hook_proxy.h"
#include "symbol_resolver.h"
#include "stream_map.h"
#include "frame_source.h"
#include "frame_inject.h"
#include "crash_guard.h"
#include "include/camera3_compat.h"

#include <shadowhook.h>
#include <android/log.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#define TAG "amkush/hook_proxy"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static void *g_pcr_stub = nullptr;
static void *g_cs_stub  = nullptr;

static atomic_bool g_enabled = true;

static void *g_orig_pcr = nullptr;
static void *g_orig_cs  = nullptr;

// ---------------------------------------------------------------------------
// processCaptureResult proxy
// ---------------------------------------------------------------------------
typedef void (*PcrFn)(const camera3_capture_result_t *result);

static void my_processCaptureResult(const camera3_capture_result_t *result) {
    SHADOWHOOK_STACK_SCOPE();

    if (!result) goto call_original;

    if (atomic_load_explicit(&g_enabled, memory_order_relaxed) &&
        frame_source_ready()) {

        if (crash_guard_enter() != 0) {
            LOGW("processCaptureResult: crash guard triggered, skipping injection");
            goto call_original;
        }

        FrameData src;
        if (!frame_source_get(&src)) {
            crash_guard_exit();
            goto call_original;
        }

        for (uint32_t i = 0; i < result->num_output_buffers; i++) {
            const camera3_stream_buffer_t *buf = &result->output_buffers[i];
            if (!buf || !buf->stream) continue;

            StreamRole role = stream_map_get_role(buf->stream);

            if (!stream_map_should_inject(role, buf->status)) continue;

            bool ok = frame_inject_one(buf, role, &src);
            if (!ok) {
                LOGW("frame_inject_one failed for stream ptr=%p role=%d",
                     (void *)buf->stream, (int)role);
            }
        }

        crash_guard_exit();
    }

call_original:
    if (g_orig_pcr) {
        ((PcrFn)g_orig_pcr)(result);
    }
}

// ---------------------------------------------------------------------------
// configureStreams proxy
// ---------------------------------------------------------------------------
typedef int (*CsFn)(void *dev, camera3_stream_configuration_t *cfg);

static int my_configureStreams(void *dev, camera3_stream_configuration_t *cfg) {
    SHADOWHOOK_STACK_SCOPE();

    if (cfg) {
        stream_map_rebuild(cfg);
    }

    if (g_orig_cs) {
        return ((CsFn)g_orig_cs)(dev, cfg);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------
int hook_proxy_install(void) {
    int r = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, /*debuggable=*/false);
    if (r != 0) {
        int err = shadowhook_get_errno();
        if (err != 1) {
            LOGE("shadowhook_init failed: %s", shadowhook_to_errmsg(err));
            return -1;
        }
        LOGI("shadowhook_init deferred (pending ELF load)");
    }

    ResolvedSymbol pcr = resolve_process_capture_result();
    ResolvedSymbol cs  = resolve_configure_streams();

    if (!pcr.ptr) {
        LOGE("Cannot resolve processCaptureResult — hook aborted");
        return -1;
    }

    LOGI("processCaptureResult resolved: %p (%s)", pcr.ptr, pcr.source);

    g_pcr_stub = shadowhook_hook_sym_addr(
        pcr.ptr,
        (void *)my_processCaptureResult,
        &g_orig_pcr);

    if (!g_pcr_stub) {
        int err = shadowhook_get_errno();
        LOGE("Failed to hook processCaptureResult: %s", shadowhook_to_errmsg(err));
        return -1;
    }
    LOGI("processCaptureResult hooked (stub=%p orig=%p)", g_pcr_stub, g_orig_pcr);

    if (cs.ptr) {
        LOGI("configureStreams resolved: %p (%s)", cs.ptr, cs.source);
        g_cs_stub = shadowhook_hook_sym_addr(
            cs.ptr,
            (void *)my_configureStreams,
            &g_orig_cs);
        if (!g_cs_stub) {
            int err = shadowhook_get_errno();
            LOGW("configureStreams hook failed (non-fatal): %s",
                 shadowhook_to_errmsg(err));
        } else {
            LOGI("configureStreams hooked (stub=%p orig=%p)", g_cs_stub, g_orig_cs);
        }
    } else {
        LOGW("configureStreams not found — using format-only stream classification");
    }

    return 0;
}

void hook_proxy_uninstall(void) {
    if (g_pcr_stub) {
        shadowhook_unhook(g_pcr_stub);
        g_pcr_stub = nullptr;
        LOGI("processCaptureResult hook removed");
    }
    if (g_cs_stub) {
        shadowhook_unhook(g_cs_stub);
        g_cs_stub = nullptr;
        LOGI("configureStreams hook removed");
    }
    stream_map_clear();
}

bool hook_proxy_is_active(void) {
    return g_pcr_stub != nullptr;
}

void hook_proxy_set_enabled(bool enabled) {
    atomic_store_explicit(&g_enabled, enabled, memory_order_relaxed);
    LOGI("Injection %s", enabled ? "ENABLED" : "DISABLED");
}
