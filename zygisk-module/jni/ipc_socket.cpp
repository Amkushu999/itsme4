// ipc_socket.cpp — Unix Domain Socket listener (cameraserver side).
//
// The app creates the Ashmem ring buffer, fills it with video frames,
// and sends the Ashmem fd over this abstract-namespace UDS.
// We receive the fd and hand it to frame_source_init().
// The listener stays running — the app may re-connect (e.g., after a
// hot-swap to a different video source) to replace the current ring buffer.

#include "ipc_socket.h"
#include "frame_source.h"

#include <android/log.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>

#define TAG "amkush/ipc_socket"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static int         g_server_fd = -1;
static pthread_t   g_thread;
static atomic_bool g_running   = false;

static int recv_fd(int sock) {
    char         buf[1];
    struct iovec iov = { buf, 1 };
    char         cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    ssize_t n = recvmsg(sock, &msg, 0);
    if (n <= 0) return -1;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) return -1;

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

static void *listener_thread(void *) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, AMKUSH_SOCKET_NAME, sizeof(AMKUSH_SOCKET_NAME) - 1);
    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path)
                       + sizeof(AMKUSH_SOCKET_NAME) - 1;

    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        LOGE("socket() failed: %s", strerror(errno));
        return nullptr;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(g_server_fd, (struct sockaddr *)&addr, addr_len) < 0) {
        LOGE("bind() failed: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return nullptr;
    }

    if (listen(g_server_fd, 4) < 0) {
        LOGE("listen() failed: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return nullptr;
    }

    LOGI("Listening on abstract socket '%s'", AMKUSH_SOCKET_NAME + 1);

    while (atomic_load(&g_running)) {
        int client = accept(g_server_fd, nullptr, nullptr);
        if (client < 0) {
            if (atomic_load(&g_running)) {
                LOGE("accept() failed: %s", strerror(errno));
            }
            break;
        }

        int ashmem_fd = recv_fd(client);
        close(client);

        if (ashmem_fd >= 0) {
            LOGI("Received Ashmem fd=%d from app — initializing frame source", ashmem_fd);
            if (frame_source_init(ashmem_fd) != 0) {
                LOGE("frame_source_init failed for fd=%d", ashmem_fd);
            }
        } else {
            LOGE("Failed to receive fd from client");
        }
    }

    return nullptr;
}

int ipc_socket_start(void) {
    atomic_store(&g_running, true);
    if (pthread_create(&g_thread, nullptr, listener_thread, nullptr) != 0) {
        LOGE("Failed to create listener thread: %s", strerror(errno));
        atomic_store(&g_running, false);
        return -1;
    }
    LOGI("IPC socket listener started");
    return 0;
}

void ipc_socket_stop(void) {
    atomic_store(&g_running, false);
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }
    pthread_join(g_thread, nullptr);
    LOGI("IPC socket listener stopped");
}
