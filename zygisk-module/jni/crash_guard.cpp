// crash_guard.cpp — sigaction + sigsetjmp safety net.
//
// Any CPU write to a protected or freed buffer triggers SIGSEGV/SIGBUS.
// Without a handler that intercepts the signal and longjmps out, the
// default disposition kills the cameraserver process, causing the phone
// to restart the camera stack (audible click / black screen flash).
//
// We register a custom SA_SIGINFO handler. Inside the frame injection
// callback we use sigsetjmp to save CPU state. If a fault fires, our
// handler calls siglongjmp back to that checkpoint, the hook skips
// synthetic injection for that frame, and calls the original function,
// keeping the camera service alive.

#include "crash_guard.h"

#include <signal.h>
#include <setjmp.h>
#include <android/log.h>
#include <string.h>
#include <stdatomic.h>

#define TAG "amkush/crash_guard"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)

static __thread sigjmp_buf  tls_checkpoint;
static __thread int         tls_active = 0;

static struct sigaction     prev_sigsegv;
static struct sigaction     prev_sigbus;

static void signal_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;

    if (tls_active) {
        const char *signame = (sig == SIGSEGV) ? "SIGSEGV" : "SIGBUS";
        LOGW("Caught %s at addr=%p during frame injection — recovering via longjmp",
             signame, info ? info->si_addr : nullptr);
        tls_active = 0;
        siglongjmp(tls_checkpoint, 1);
    }

    struct sigaction *prev = (sig == SIGSEGV) ? &prev_sigsegv : &prev_sigbus;
    if (prev->sa_flags & SA_SIGINFO) {
        prev->sa_sigaction(sig, info, ctx);
    } else if (prev->sa_handler == SIG_DFL || prev->sa_handler == SIG_IGN) {
        struct sigaction dfl;
        memset(&dfl, 0, sizeof(dfl));
        dfl.sa_handler = SIG_DFL;
        sigaction(sig, &dfl, nullptr);
        raise(sig);
    } else {
        prev->sa_handler(sig);
    }
}

int crash_guard_init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags     = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, &prev_sigsegv) != 0) {
        LOGE("Failed to install SIGSEGV handler");
        return -1;
    }
    if (sigaction(SIGBUS, &sa, &prev_sigbus) != 0) {
        LOGE("Failed to install SIGBUS handler");
        sigaction(SIGSEGV, &prev_sigsegv, nullptr);
        return -1;
    }

    LOGI("Crash guard installed (SIGSEGV + SIGBUS)");
    return 0;
}

void crash_guard_cleanup(void) {
    sigaction(SIGSEGV, &prev_sigsegv, nullptr);
    sigaction(SIGBUS,  &prev_sigbus,  nullptr);
    LOGI("Crash guard removed");
}

int crash_guard_enter(void) {
    tls_active = 1;
    int rc = sigsetjmp(tls_checkpoint, 1);
    if (rc != 0) {
        tls_active = 0;
    }
    return rc;
}

void crash_guard_exit(void) {
    tls_active = 0;
}
