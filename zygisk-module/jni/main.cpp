// main.cpp — Zygisk module entry point.
//
// Zygisk lifecycle (from zygisk.hpp):
//
//   1. Magisk loads this .so into Zygote.
//   2. onLoad() is called. We store the Api pointer but do nothing heavy
//      (we are inside Zygote, not cameraserver yet).
//   3. preAppSpecialize() is called before every process fork.
//      We check if the target process is "cameraserver". If not, we do
//      nothing (DLCLOSE this module for that process to reduce footprint).
//   4. postAppSpecialize() is called after the fork settles.
//      We are now INSIDE cameraserver. We install hooks and start IPC.
//
// The Zygisk module .so files must be placed at:
//   /data/adb/modules/<module-id>/zygisk/arm64-v8a.so
//   /data/adb/modules/<module-id>/zygisk/x86_64.so   (for emulator)

#include <zygisk.hpp>
#include <android/log.h>
#include <string.h>
#include <jni.h>

#include "hook_proxy.h"
#include "frame_source.h"
#include "frame_inject.h"
#include "ipc_socket.h"
#include "crash_guard.h"

#define TAG "amkush/main"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// ---------------------------------------------------------------------------
// AmkushModule
// ---------------------------------------------------------------------------
class AmkushModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("AmkushModule loaded into Zygote (build " __DATE__ " " __TIME__ ")");
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args) return;

        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        bool is_camera = (name && strstr(name, "cameraserver") != nullptr);
        env->ReleaseStringUTFChars(args->nice_name, name);

        if (!is_camera) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGI("Matched target process: cameraserver — will install hooks post-specialize");
        target_matched = true;
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (!target_matched) return;

        LOGI("postAppSpecialize: inside cameraserver — initializing hook pipeline");

        if (crash_guard_init() != 0) {
            LOGE("crash_guard_init failed — proceeding without crash recovery");
        }

        if (frame_inject_init() != 0) {
            LOGE("frame_inject_init failed — injection disabled");
        }

        if (hook_proxy_install() != 0) {
            LOGE("hook_proxy_install failed — virtual camera inactive");
            crash_guard_cleanup();
            return;
        }

        if (ipc_socket_start() != 0) {
            LOGE("ipc_socket_start failed — no frame source available");
        }

        LOGI("amkush virtual camera ACTIVE in cameraserver");
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api    *api           = nullptr;
    JNIEnv *env           = nullptr;
    bool    target_matched = false;
};

// ---------------------------------------------------------------------------
// Companion process (reserved for future privileged operations)
// ---------------------------------------------------------------------------
static void companion_handler(int client_fd) {
    (void)client_fd;
}

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------
REGISTER_ZYGISK_MODULE(AmkushModule)
REGISTER_ZYGISK_COMPANION(companion_handler)
