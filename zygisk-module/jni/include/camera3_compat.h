// camera3_compat.h — Standalone-safe subset of AOSP camera3.h + gralloc.h
// Definitions taken verbatim from:
//   hardware/libhardware/include/hardware/camera3.h
//   hardware/libhardware/include/hardware/gralloc.h
//   system/core/include/system/graphics.h
#pragma once

#include <stdint.h>

// android_dataspace_t — from AOSP system/core/include/system/graphics.h.
// That header is an AOSP-internal path not shipped by the NDK, so we inline
// the one typedef we actually use here.  The values match the AOSP enum.
#ifndef ANDROID_DATASPACE_UNKNOWN
typedef int32_t android_dataspace_t;
#define ANDROID_DATASPACE_UNKNOWN   0
#define ANDROID_DATASPACE_JFIF      0x101
#define ANDROID_DATASPACE_V0_JFIF   0x101
#endif

// buffer_handle_t — available via the NDK at API 26+ through hardware_buffer.
// Forward-declared here so we don't drag in the full <android/hardware_buffer.h>
// which can conflict with other headers in non-Gradle builds.
// We only ever use it as an opaque pointer: buffer_handle_t *, so this is safe.
#ifndef ANDROID_HARDWARE_BUFFER_H
typedef struct native_handle* buffer_handle_t;
#endif

// camera_metadata_t — AOSP-internal opaque type (camera/CameraMetadata.h).
// The NDK does not expose this type directly; we only need it as an opaque
// pointer so a forward declaration is sufficient.
struct camera_metadata;
typedef struct camera_metadata camera_metadata_t;

// native_handle_t — <cutils/native_handle.h> is not in the NDK public headers.
// Forward-declare + typedef here; only used as a pointer so this is sufficient.
typedef struct native_handle native_handle_t;

// ---------------------------------------------------------------------------
// GRALLOC usage flags
// ---------------------------------------------------------------------------
#ifndef GRALLOC_USAGE_SW_READ_OFTEN
#define GRALLOC_USAGE_SW_READ_OFTEN    0x00000003U
#endif
#ifndef GRALLOC_USAGE_SW_WRITE_OFTEN
#define GRALLOC_USAGE_SW_WRITE_OFTEN   0x00000030U
#endif
#ifndef GRALLOC_USAGE_SW_WRITE_RARELY
#define GRALLOC_USAGE_SW_WRITE_RARELY  0x00000020U
#endif
#ifndef GRALLOC_USAGE_PROTECTED
#define GRALLOC_USAGE_PROTECTED        0x00004000U
#endif

// ---------------------------------------------------------------------------
// HAL pixel formats
// ---------------------------------------------------------------------------
#ifndef HAL_PIXEL_FORMAT_RGBA_8888
#define HAL_PIXEL_FORMAT_RGBA_8888    1
#endif
#ifndef HAL_PIXEL_FORMAT_BLOB
#define HAL_PIXEL_FORMAT_BLOB         0x21
#endif
#ifndef HAL_PIXEL_FORMAT_RAW16
#define HAL_PIXEL_FORMAT_RAW16        0x20
#endif
#ifndef HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
#define HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED 0x22
#endif
#ifndef HAL_PIXEL_FORMAT_YCBCR_420_888
#define HAL_PIXEL_FORMAT_YCBCR_420_888 0x23
#endif
#ifndef HAL_PIXEL_FORMAT_YCrCb_420_SP
#define HAL_PIXEL_FORMAT_YCrCb_420_SP  0x11
#endif
#ifndef HAL_PIXEL_FORMAT_YCBCR_P010
#define HAL_PIXEL_FORMAT_YCBCR_P010    0x36
#endif

// ---------------------------------------------------------------------------
// camera3_stream_type_t
// ---------------------------------------------------------------------------
typedef enum camera3_stream_type {
    CAMERA3_STREAM_OUTPUT       = 0,
    CAMERA3_STREAM_INPUT        = 1,
    CAMERA3_STREAM_BIDIRECTIONAL = 2,
} camera3_stream_type_t;

// ---------------------------------------------------------------------------
// camera3_stream_t
// ---------------------------------------------------------------------------
typedef struct camera3_stream {
    int                     stream_type;
    uint32_t                width;
    uint32_t                height;
    int                     format;
    uint32_t                usage;

    uint32_t                max_buffers;
    void                   *priv;

    android_dataspace_t     data_space;
    int32_t                 rotation;

    const char             *physical_camera_id;

    uint32_t                reserved[51];
} camera3_stream_t;

// ---------------------------------------------------------------------------
// camera3_stream_configuration_t
// ---------------------------------------------------------------------------
typedef struct camera3_stream_configuration {
    uint32_t             num_streams;
    camera3_stream_t   **streams;
    uint32_t             operation_mode;
    const camera_metadata_t *session_parameters;
} camera3_stream_configuration_t;

// ---------------------------------------------------------------------------
// camera3_buffer_status_t / camera3_stream_buffer_t
// ---------------------------------------------------------------------------
typedef enum camera3_buffer_status {
    CAMERA3_BUFFER_STATUS_OK    = 0,
    CAMERA3_BUFFER_STATUS_ERROR = 1,
} camera3_buffer_status_t;

typedef struct camera3_stream_buffer {
    camera3_stream_t   *stream;
    buffer_handle_t    *buffer;
    int                 status;
    int                 acquire_fence;
    int                 release_fence;
} camera3_stream_buffer_t;

// ---------------------------------------------------------------------------
// camera3_capture_result_t
// ---------------------------------------------------------------------------
typedef struct camera3_capture_result {
    uint32_t                        frame_number;
    const camera_metadata_t        *result;
    uint32_t                        num_output_buffers;
    const camera3_stream_buffer_t  *output_buffers;
    const camera3_stream_buffer_t  *input_buffer;
    uint32_t                        partial_result;
    uint32_t                        num_physcam_metadata;
    const char                    **physcam_ids;
    const camera_metadata_t       **physcam_metadata;
} camera3_capture_result_t;

// ---------------------------------------------------------------------------
// Forward-declare opaque types needed by camera3_callback_ops_t
// ---------------------------------------------------------------------------
struct camera3_notify_msg;
struct camera3_buffer_request;
struct camera3_stream_buffer_ret;
typedef int camera3_buffer_request_status_t;

// ---------------------------------------------------------------------------
// camera3_callback_ops_t
// ---------------------------------------------------------------------------
struct camera3_callback_ops {
    void (*process_capture_result)(const struct camera3_callback_ops *,
                                   const camera3_capture_result_t *);
    void (*notify)(const struct camera3_callback_ops *,
                   const struct camera3_notify_msg *);
    camera3_buffer_request_status_t (*request_stream_buffers)(
        const struct camera3_callback_ops *,
        uint32_t num_buffer_reqs,
        const camera3_buffer_request *buffer_reqs,
        uint32_t *num_returned_buf_reqs,
        camera3_stream_buffer_ret *returned_buf_reqs);
    void (*return_stream_buffers)(const struct camera3_callback_ops *,
                                  uint32_t num_buffers,
                                  const camera3_stream_buffer_t *const *buffers);
};
