// frame_producer.cpp — App-side frame pipeline (com.itsme.amkush process).
//
// Decodes video / RTSP / image sources using FFmpeg, writes raw NV12 frames
// into an Ashmem ring buffer, and sends the Ashmem fd to the cameraserver
// Zygisk hook via a Unix Domain Socket (abstract namespace, SCM_RIGHTS).
//
// Called from Kotlin via JNI:
//   NativeFrameProducer.nativeStart(sourcePath: String): Int
//   NativeFrameProducer.nativeStop()

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <android/log.h>
#include <android/sharedmem.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include <jni.h>

#define TAG "amkush/frame_producer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Ring buffer layout — must match frame_source.h in the Zygisk module
// ---------------------------------------------------------------------------
#define FRAME_SOURCE_MAGIC   0xAB7C5801U
#define FRAME_RING_SLOTS     3
#define AMKUSH_SOCKET_NAME   "\0amkush_frame_fd"

static const int MASTER_WIDTH  = 1280;
static const int MASTER_HEIGHT = 720;

typedef struct {
    uint32_t          magic;
    volatile uint32_t write_slot;
    uint32_t          master_width;
    uint32_t          master_height;
    uint32_t          slot_stride;
    uint8_t           _pad[44];
} FrameSourceHeader;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static int         g_ashmem_fd   = -1;
static void       *g_map_base    = MAP_FAILED;
static size_t      g_map_size    = 0;
static uint8_t    *g_slots_base  = nullptr;
static uint32_t    g_slot_stride = 0;

// ---------------------------------------------------------------------------
// Ashmem ring buffer
// ---------------------------------------------------------------------------
static int create_ashmem_ring(int w, int h) {
    uint32_t stride  = (uint32_t)((w + 63) & ~63);
    size_t   slot_sz = (size_t)stride * h * 3 / 2;
    size_t   total   = sizeof(FrameSourceHeader) + slot_sz * FRAME_RING_SLOTS;

    int fd = ASharedMemory_create("amkush_ring", total);
    if (fd < 0) {
        LOGE("ASharedMemory_create failed: %s", strerror(errno));
        return -1;
    }

    void *base = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        LOGE("mmap failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    FrameSourceHeader *hdr = (FrameSourceHeader *)base;
    hdr->magic         = FRAME_SOURCE_MAGIC;
    hdr->write_slot    = 0;
    hdr->master_width  = (uint32_t)w;
    hdr->master_height = (uint32_t)h;
    hdr->slot_stride   = stride;

    g_ashmem_fd   = fd;
    g_map_base    = base;
    g_map_size    = total;
    g_slots_base  = (uint8_t *)base + sizeof(FrameSourceHeader);
    g_slot_stride = stride;

    LOGI("Ashmem ring created: %dx%d stride=%u total=%zu fd=%d", w, h, stride, total, fd);
    return fd;
}

static void ring_write(const uint8_t *y_src, int y_stride,
                        const uint8_t *uv_src, int uv_stride,
                        int w, int h) {
    FrameSourceHeader *hdr = (FrameSourceHeader *)g_map_base;
    uint32_t next = (__atomic_load_n(&hdr->write_slot, __ATOMIC_RELAXED) + 1) % FRAME_RING_SLOTS;

    size_t   slot_sz = (size_t)g_slot_stride * h * 3 / 2;
    uint8_t *dst_y   = g_slots_base + next * slot_sz;
    uint8_t *dst_uv  = dst_y + (size_t)g_slot_stride * h;

    for (int row = 0; row < h; row++)
        memcpy(dst_y + row * g_slot_stride, y_src + row * y_stride, (size_t)w);
    for (int row = 0; row < h / 2; row++)
        memcpy(dst_uv + row * g_slot_stride, uv_src + row * uv_stride, (size_t)w);

    __atomic_store_n(&hdr->write_slot, next, __ATOMIC_RELEASE);
}

// ---------------------------------------------------------------------------
// IPC: send Ashmem fd to cameraserver hook via UDS + SCM_RIGHTS
// ---------------------------------------------------------------------------
static int send_fd_to_hook(int fd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, AMKUSH_SOCKET_NAME, sizeof(AMKUSH_SOCKET_NAME) - 1);
    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + sizeof(AMKUSH_SOCKET_NAME) - 1;

    if (connect(sock, (struct sockaddr *)&addr, addr_len) < 0) {
        LOGE("connect() to cameraserver hook failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    char buf[1] = {0};
    struct iovec iov = { buf, 1 };
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    ssize_t n = sendmsg(sock, &msg, 0);
    close(sock);

    if (n <= 0) {
        LOGE("sendmsg() failed: %s", strerror(errno));
        return -1;
    }
    LOGI("Sent Ashmem fd=%d to cameraserver hook", fd);
    return 0;
}

// ---------------------------------------------------------------------------
// FFmpeg decode thread (handles video files, RTSP streams, and images)
// ---------------------------------------------------------------------------
static atomic_bool g_running = false;
static pthread_t   g_thread;
static char        g_source[4096];

static void *decode_thread(void *) {
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext  *dec_ctx = nullptr;
    SwsContext      *sws_ctx = nullptr;
    AVFrame  *frame   = av_frame_alloc();
    AVFrame  *nv12_fr = av_frame_alloc();
    AVPacket *pkt     = av_packet_alloc();

    if (!frame || !nv12_fr || !pkt) {
        LOGE("av_frame/packet alloc failed");
        goto cleanup;
    }

    if (avformat_open_input(&fmt_ctx, g_source, nullptr, nullptr) < 0) {
        LOGE("avformat_open_input failed for: %s", g_source);
        goto cleanup;
    }
    avformat_find_stream_info(fmt_ctx, nullptr);

    {
        int video_stream = -1;
        const AVCodec *codec = nullptr;
        for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream = (int)i;
                codec = avcodec_find_decoder(fmt_ctx->streams[i]->codecpar->codec_id);
                break;
            }
        }
        if (video_stream < 0 || !codec) {
            LOGE("No video stream found in: %s", g_source);
            goto cleanup;
        }

        dec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream]->codecpar);
        avcodec_open2(dec_ctx, codec, nullptr);

        nv12_fr->format = AV_PIX_FMT_NV12;
        nv12_fr->width  = MASTER_WIDTH;
        nv12_fr->height = MASTER_HEIGHT;
        av_frame_get_buffer(nv12_fr, 64);

        sws_ctx = sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                                 MASTER_WIDTH, MASTER_HEIGHT, AV_PIX_FMT_NV12,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);

        bool is_static = (fmt_ctx->nb_streams == 1 &&
                          fmt_ctx->streams[video_stream]->codecpar->codec_id == AV_CODEC_ID_MJPEG);

        while (atomic_load(&g_running)) {
            int ret = av_read_frame(fmt_ctx, pkt);
            if (ret == AVERROR_EOF) {
                if (is_static) {
                    // Static image — keep the last written slot; no need to loop.
                    av_packet_unref(pkt);
                    while (atomic_load(&g_running)) usleep(50000);
                    break;
                }
                av_seek_frame(fmt_ctx, video_stream, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(dec_ctx);
                continue;
            }
            if (ret < 0) break;
            if (pkt->stream_index != video_stream) { av_packet_unref(pkt); continue; }

            avcodec_send_packet(dec_ctx, pkt);
            av_packet_unref(pkt);

            while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                sws_scale(sws_ctx,
                          (const uint8_t *const *)frame->data, frame->linesize,
                          0, dec_ctx->height,
                          nv12_fr->data, nv12_fr->linesize);

                ring_write(nv12_fr->data[0], nv12_fr->linesize[0],
                           nv12_fr->data[1], nv12_fr->linesize[1],
                           MASTER_WIDTH, MASTER_HEIGHT);

                av_frame_unref(frame);
            }
        }
    }

cleanup:
    if (sws_ctx)  sws_freeContext(sws_ctx);
    if (dec_ctx)  avcodec_free_context(&dec_ctx);
    if (fmt_ctx)  avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&nv12_fr);
    av_packet_free(&pkt);
    LOGI("decode_thread exited");
    return nullptr;
}

// ---------------------------------------------------------------------------
// JNI interface
// ---------------------------------------------------------------------------
extern "C" {

JNIEXPORT jint JNICALL
Java_com_itsme_amkush_hooks_NativeFrameProducer_nativeStart(JNIEnv *env,
                                                              jobject /*thiz*/,
                                                              jstring jSourcePath) {
    // Stop any running decode thread first.
    if (atomic_load(&g_running)) {
        atomic_store(&g_running, false);
        pthread_join(g_thread, nullptr);
    }

    // Release old mapping.
    if (g_map_base != MAP_FAILED) {
        munmap(g_map_base, g_map_size);
        g_map_base = MAP_FAILED;
    }
    if (g_ashmem_fd >= 0) {
        close(g_ashmem_fd);
        g_ashmem_fd = -1;
    }

    const char *src = env->GetStringUTFChars(jSourcePath, nullptr);
    strncpy(g_source, src, sizeof(g_source) - 1);
    g_source[sizeof(g_source) - 1] = '\0';
    env->ReleaseStringUTFChars(jSourcePath, src);

    int ashmem_fd = create_ashmem_ring(MASTER_WIDTH, MASTER_HEIGHT);
    if (ashmem_fd < 0) {
        LOGE("create_ashmem_ring failed");
        return -1;
    }

    // Send fd to cameraserver hook (non-fatal if hook is not yet running).
    if (send_fd_to_hook(ashmem_fd) != 0) {
        LOGW("Could not connect to cameraserver hook — hook may not be active");
    }

    atomic_store(&g_running, true);
    if (pthread_create(&g_thread, nullptr, decode_thread, nullptr) != 0) {
        LOGE("pthread_create failed: %s", strerror(errno));
        atomic_store(&g_running, false);
        return -1;
    }

    LOGI("NativeFrameProducer started for: %s", g_source);
    return 0;
}

JNIEXPORT void JNICALL
Java_com_itsme_amkush_hooks_NativeFrameProducer_nativeStop(JNIEnv * /*env*/,
                                                             jobject /*thiz*/) {
    atomic_store(&g_running, false);
    pthread_join(g_thread, nullptr);

    if (g_map_base != MAP_FAILED) {
        munmap(g_map_base, g_map_size);
        g_map_base = MAP_FAILED;
    }
    if (g_ashmem_fd >= 0) {
        close(g_ashmem_fd);
        g_ashmem_fd = -1;
    }
    g_slots_base  = nullptr;
    g_slot_stride = 0;

    LOGI("NativeFrameProducer stopped");
}

} // extern "C"
